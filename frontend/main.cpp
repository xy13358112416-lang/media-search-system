#include "medialibrary.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDialog>
#include <QDir>
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

class LibraryWindow final : public QWidget {
public:
    explicit LibraryWindow(const MediaLibrary& library) : library_(library)
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
        toolbar->addStretch();
        toolbar->addWidget(new QLabel(QStringLiteral("发行日期：从近到远")));
        toolbar->addWidget(count_);
        layout->addLayout(toolbar);

        filter_->addItem(QStringLiteral("全部类型"), QString());
        for (const auto& type : library_.fileTypes())
            filter_->addItem(type, type);

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
        refresh();
    }

private:
    // Apply the selected file type and display the date order returned by the library.
    void refresh()
    {
        const QSignalBlocker blockSelection(table_);
        table_->clearSelection();
        records_ = library_.records(filter_->currentData().toString());
        table_->setRowCount(static_cast<int>(records_.size()));
        for (int row = 0; row < table_->rowCount(); ++row) {
            const auto& record = records_.at(row);
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
        table_->setCurrentCell(0, 0);
        showDetail(0);
    }

    // Update the details from the record associated with the current table row.
    void showDetail(int row)
    {
        if (row < 0 || row >= records_.size()) {
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

    const MediaLibrary& library_;      // Loaded business service, owned by main.
    QList<MediaRecord> records_;       // Visible records in table row order.
    QList<QLabel*> detailValues_;      // Detail value labels in form order.
    QComboBox* filter_ = new QComboBox; // File type filter with an empty value for all types.
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

    const QString csvPath = parser.isSet(csvOption)
        ? parser.value(csvOption)
        : QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("media_records.csv"));
    MediaLibrary library(csvPath, password);
    if (!showLogin(library))
        return 0;

    const auto result = library.load();
    if (!result.success) {
        QMessageBox::critical(nullptr, QStringLiteral("媒体库加载失败"), result.error);
        return 1;
    }

    LibraryWindow window(library);
    window.show();
    return app.exec();
}
