#include "medialibrary.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <QComboBox>

namespace {

QString displayText(const QString& value)
{
    return value.isEmpty() ? QStringLiteral("—") : value;
}

// Display a record's date using the same format in the list and the details.
QString displayDate(const QDate& date)
{
    return displayText(date.toString(Qt::ISODate));
}

// Verify the entered password before opening the media library.
bool showLogin(const MediaLibrary& library)
{
    QDialog dialog;
    dialog.setWindowTitle(QStringLiteral("媒体库登录"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* title = new QLabel(QStringLiteral("多媒体文件管理及检索系统"));
    auto* password = new QLineEdit;
    password->setEchoMode(QLineEdit::Password);
    password->setPlaceholderText(QStringLiteral("请输入系统口令"));
    password->setAccessibleName(QStringLiteral("系统口令"));
    auto* error = new QLabel;
    error->setStyleSheet(QStringLiteral("color: #b42318;"));
    error->hide();
    auto* login = new QPushButton(QStringLiteral("登录"));
    login->setDefault(true);
    layout->addWidget(title);
    layout->addWidget(password);
    layout->addWidget(error);
    layout->addWidget(login);
    dialog.setMinimumWidth(340);
    password->setFocus();

    QObject::connect(login, &QPushButton::clicked, &dialog, [&] {
        if (library.authenticate(password->text())) {
            dialog.accept();
            return;
        }
        error->setText(QStringLiteral("口令错误，请重新输入。"));
        error->show();
        password->selectAll();
        password->setFocus();
    });
    QObject::connect(password, &QLineEdit::textEdited, error, &QWidget::hide);
    return dialog.exec() == QDialog::Accepted;
}

// Split the form's list fields and discard empty entries.
QStringList splitEntries(const QString& text)
{
    QStringList entries;
    for (const auto& part : text.split(QChar(u'、'))) {
        const QString entry = part.trimmed();
        if (!entry.isEmpty())
            entries.append(entry);
    }
    return entries;
}

// Parse a nonnegative count without narrowing the supported 64-bit range.
bool readCount(const QString& text, quint64& value)
{
    const QString number = text.trimmed();
    bool ok = false;
    value = number.toULongLong(&ok);
    return ok && !number.startsWith(QLatin1Char('-'));
}

class LibraryWindow final : public QWidget {
public:
    explicit LibraryWindow(MediaLibrary& library) : library_(library)
    {
        setWindowTitle(QStringLiteral("多媒体文件管理及检索系统"));
        resize(1080, 600);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 20, 20, 20);
        layout->setSpacing(12);

        auto* toolbar = new QHBoxLayout;
        auto* filterLabel = new QLabel(QStringLiteral("文件类型"));
        filterLabel->setBuddy(filter_);
        toolbar->addWidget(filterLabel);
        toolbar->addWidget(filter_);
        toolbar->addWidget(add_);
        toolbar->addWidget(remove_);
        toolbar->addStretch();
        toolbar->addWidget(new QLabel(QStringLiteral("发行日期：从近到远")));
        toolbar->addWidget(count_);
        layout->addLayout(toolbar);

        table_->setColumnCount(4);
        table_->setHorizontalHeaderLabels({QStringLiteral("文件名"), QStringLiteral("类型"),
                                           QStringLiteral("导演"), QStringLiteral("发行日期")});
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table_->verticalHeader()->hide();
        table_->setAlternatingRowColors(true);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);

        auto* detailPane = new QWidget;
        detailPane->setMinimumWidth(280);
        auto* form = new QFormLayout(detailPane);
        const QStringList labels{QStringLiteral("编号"), QStringLiteral("文件名"),
                                 QStringLiteral("文件类型"), QStringLiteral("文件大小"),
                                 QStringLiteral("导演"), QStringLiteral("演员"),
                                 QStringLiteral("浏览次数"), QStringLiteral("发行日期"),
                                 QStringLiteral("所获奖项"), QStringLiteral("类别")};
        for (const auto& label : labels) {
            auto* value = new QLabel;
            value->setTextFormat(Qt::PlainText);
            value->setWordWrap(true);
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            detailValues_.append(value);
            form->addRow(label, value);
        }

        auto* splitter = new QSplitter;
        splitter->addWidget(table_);
        splitter->addWidget(detailPane);
        splitter->setStretchFactor(0, 3);
        splitter->setStretchFactor(1, 1);
        layout->addWidget(splitter);

        connect(filter_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
        connect(table_, &QTableWidget::currentCellChanged, this,
                [this](int row) { showDetail(row); });
        connect(add_, &QPushButton::clicked, this, [this] { addRecord(); });
        connect(remove_, &QPushButton::clicked, this, [this] { removeRecord(); });
        refreshTypes();
    }

private:
    // Collect a record and keep the form open if validation or saving fails.
    void addRecord()
    {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("新增媒体记录"));
        dialog.setMinimumWidth(420);
        auto* form = new QFormLayout(&dialog);

        QLineEdit name, type, size(QStringLiteral("0")), director, actors;
        QLineEdit views(QStringLiteral("0")), date, awards, category;
        type.setPlaceholderText(QStringLiteral("例如 MP4"));
        date.setPlaceholderText(QStringLiteral("YYYY-MM-DD"));
        const QString listHint = QStringLiteral("多项用顿号「、」分隔");
        actors.setPlaceholderText(listHint);
        awards.setPlaceholderText(listHint);
        form->addRow(QStringLiteral("文件名"), &name);
        form->addRow(QStringLiteral("文件类型"), &type);
        form->addRow(QStringLiteral("文件大小（字节）"), &size);
        form->addRow(QStringLiteral("导演"), &director);
        form->addRow(QStringLiteral("演员"), &actors);
        form->addRow(QStringLiteral("浏览次数"), &views);
        form->addRow(QStringLiteral("发行日期"), &date);
        form->addRow(QStringLiteral("所获奖项"), &awards);
        form->addRow(QStringLiteral("类别"), &category);

        QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        buttons.button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
        buttons.button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
        form->addRow(&buttons);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        // Keep the generated ID stable while the user corrects the form or retries.
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            MediaRecord record;
            record.id = id;
            record.fileName = name.text().trimmed();
            record.fileType = type.text().trimmed().toUpper();
            record.director = director.text().trimmed();
            record.actors = splitEntries(actors.text());
            record.awards = splitEntries(awards.text());
            record.category = category.text().trimmed();

            const QString dateText = date.text().trimmed();
            record.releaseDate = QDate::fromString(dateText, Qt::ISODate);
            if (record.fileName.isEmpty() || record.fileType.isEmpty() || record.fileType.contains('.')) {
                QMessageBox::warning(&dialog, QStringLiteral("检查输入"),
                                     QStringLiteral("请填写文件名和文件类型，类型无需带点。"));
                return;
            }
            if (!record.releaseDate.isValid() || record.releaseDate.toString(Qt::ISODate) != dateText) {
                QMessageBox::warning(&dialog, QStringLiteral("检查输入"),
                                     QStringLiteral("请输入有效的发行日期，格式为 YYYY-MM-DD。"));
                return;
            }
            if (!readCount(size.text(), record.fileSize) || !readCount(views.text(), record.viewCount)) {
                QMessageBox::warning(&dialog, QStringLiteral("检查输入"),
                                     QStringLiteral("文件大小和浏览次数须填写有效的非负整数。"));
                return;
            }

            const auto result = library_.add(record);
            if (!result.success) {
                recoverAfterError(&dialog, QStringLiteral("新增失败"), result.error);
                MediaRecord saved;
                if (!library_.find(id, saved))
                    return;
            }

            // If another file type was selected, switch to the new record's type.
            const QString activeType = filter_->currentData().toString();
            refreshTypes(activeType.isEmpty() ? QString() : record.fileType, id);
            dialog.accept();
        });
        name.setFocus();
        dialog.exec();
    }

    // Confirm the selected record and delete it by ID through the business layer.
    void removeRecord()
    {
        const int row = table_->currentRow();
        if (row < 0 || row >= records_.size())
            return;
        const auto record = records_.at(row);
        QMessageBox confirmation(QMessageBox::Question, QStringLiteral("删除媒体记录"),
                                 QStringLiteral("确定删除「%1」的记录吗？").arg(record.fileName),
                                 QMessageBox::Yes | QMessageBox::Cancel, this);
        confirmation.setTextFormat(Qt::PlainText);
        confirmation.button(QMessageBox::Yes)->setText(QStringLiteral("删除"));
        confirmation.button(QMessageBox::Cancel)->setText(QStringLiteral("取消"));
        confirmation.setDefaultButton(QMessageBox::Cancel);
        if (confirmation.exec() != QMessageBox::Yes)
            return;

        const auto result = library_.remove(record.id);
        if (!result.success) {
            recoverAfterError(this, QStringLiteral("删除失败"), result.error);
            return;
        }
        refreshTypes(filter_->currentData().toString());
    }

    // Reload the current snapshot in case a write succeeded before indexing failed.
    void recoverAfterError(QWidget* parent, const QString& title, const QString& error)
    {
        const auto reload = library_.load();
        refreshTypes(filter_->currentData().toString());
        QMessageBox message(QMessageBox::Warning, title,
                            reload.success ? error : error + QStringLiteral("\n重新加载失败：") + reload.error,
                            QMessageBox::Ok, parent);
        message.setTextFormat(Qt::PlainText);
        message.exec();
    }

    // Restore the filter if the type still exists, or show all records.
    void refreshTypes(const QString& type = {}, const QString& selectedId = {})
    {
        const QSignalBlocker blockFilter(filter_);
        filter_->clear();
        filter_->addItem(QStringLiteral("全部类型"), QString());
        for (const auto& availableType : library_.fileTypes())
            filter_->addItem(availableType, availableType);
        const int index = filter_->findData(type);
        filter_->setCurrentIndex(index < 0 ? 0 : index);
        refresh(selectedId);
    }
    // Apply the selected file type and display the date order returned by the library.
    void refresh(const QString& selectedId = {})
    {
        const QSignalBlocker blockSelection(table_);
        table_->clearSelection();
        records_ = library_.records(filter_->currentData().toString());
        table_->setRowCount(static_cast<int>(records_.size()));
        int selectedRow = 0;
        for (int row = 0; row < table_->rowCount(); ++row) {
            const auto& record = records_.at(row);
            if (record.id == selectedId)
                selectedRow = row;
            const QStringList values{record.fileName, record.fileType, record.director,
                                     displayDate(record.releaseDate)};
            for (int column = 0; column < values.size(); ++column)
                table_->setItem(row, column, new QTableWidgetItem(displayText(values.at(column))));
        }

        count_->setText(records_.isEmpty() ? QStringLiteral("暂无媒体记录")
                                          : QStringLiteral("共 %1 条").arg(records_.size()));
        if (records_.isEmpty()) {
            showDetail(-1);
            return;
        }
        table_->setCurrentCell(selectedRow, 0);
        showDetail(selectedRow);
    }

    // Update the details from the record associated with the current table row.
    void showDetail(int row)
    {
        remove_->setEnabled(row >= 0 && row < records_.size());
        if (!remove_->isEnabled()) {
            for (auto* value : detailValues_)
                value->setText(displayText({}));
            return;
        }

        const auto& record = records_.at(row);
        const QLocale locale;
        const QStringList values{record.id, record.fileName, record.fileType,
                                 QStringLiteral("%1 字节").arg(locale.toString(record.fileSize)),
                                 record.director, record.actors.join(QStringLiteral("、")),
                                 locale.toString(record.viewCount), displayDate(record.releaseDate),
                                 record.awards.join(QStringLiteral("、")), record.category};
        for (qsizetype i = 0; i < values.size(); ++i)
            detailValues_.at(i)->setText(displayText(values.at(i)));
    }

    MediaLibrary& library_;            // Loaded business service, owned by main.
    QList<MediaRecord> records_;       // Visible records in table row order.
    QList<QLabel*> detailValues_;      // Detail value labels in form order.
    QComboBox* filter_ = new QComboBox; // File type filter with an empty value for all types.
    QPushButton* add_ = new QPushButton(QStringLiteral("新增")); // Open the record form.
    QPushButton* remove_ = new QPushButton(QStringLiteral("删除")); // Delete the selected row.
    QLabel* count_ = new QLabel;       // Visible record count or empty state.
    QTableWidget* table_ = new QTableWidget; // Read-only list of filtered records.
};

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("多媒体文件管理及检索系统"));
    parser.addHelpOption();
    const QCommandLineOption csvOption(QStringLiteral("csv"), QStringLiteral("媒体 CSV 文件路径"),
                                       QStringLiteral("path"));
    parser.addOption(csvOption);
    parser.process(app);

    const QString password = qEnvironmentVariable("MEDIA_LIBRARY_PASSWORD");
    if (password.isEmpty()) {
        QMessageBox::warning(nullptr, QStringLiteral("缺少系统口令"),
                             QStringLiteral("请设置 MEDIA_LIBRARY_PASSWORD 后重新启动。"));
        return 1;
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const bool useDefaultCsv = !parser.isSet(csvOption);
    const QString csvPath = useDefaultCsv ? appDir.filePath(QStringLiteral("media_records.csv"))
                                          : parser.value(csvOption);
    MediaLibrary library(csvPath, password);
    if (!showLogin(library))
        return 0;

    // Initialize the writable demo file once. Subsequent builds only copy the seed.
    if (useDefaultCsv && !QFile::exists(csvPath)) {
        QFile seed(appDir.filePath(QStringLiteral("media_records_sample.csv")));
        if (!seed.copy(csvPath) && !QFile::exists(csvPath)) {
            QMessageBox::critical(nullptr, QStringLiteral("无法初始化媒体库"), seed.errorString());
            return 1;
        }
    }

    const auto result = library.load();
    if (!result.success) {
        QMessageBox::critical(nullptr, QStringLiteral("媒体库加载失败"), result.error);
        return 1;
    }

    LibraryWindow window(library);
    window.show();
    return app.exec();
}
