#pragma once

#include "medialibrary.h"
#include "account_history_service.h"
#include <QTableWidget>
#include <QStyledItemDelegate>

// Count calls while still executing the real business service and Qt widgets.
class ObservedLibrary : public MediaLibrary {
public:
    using MediaLibrary::MediaLibrary;
    int addCalls = 0;
    int loadCalls = 0;
    RepositoryResult addNew(MediaRecord& record) {
        ++addCalls;
        return MediaLibrary::addNew(record);
    }
    RepositoryResult load() {
        ++loadCalls;
        return MediaLibrary::load();
    }
};

class SizeCountingDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    mutable quint64 hints = 0;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        ++hints;
        return QStyledItemDelegate::sizeHint(option, index);
    }
};

class ObservedTable : public QTableWidget {
public:
    SizeCountingDelegate* counter = new SizeCountingDelegate(this);
    mutable quint64 columnCalls = 0;
    quint64 dataCalls = 0;
    ObservedTable() { setItemDelegate(counter); }
    void resetCounters() { columnCalls = dataCalls = counter->hints = 0; }
protected:
    int sizeHintForColumn(int column) const override {
        ++columnCalls;
        return QTableWidget::sizeHintForColumn(column);
    }
    void dataChanged(const QModelIndex& first, const QModelIndex& last,
                     const QList<int>& roles = {}) override {
        ++dataCalls;
        QTableWidget::dataChanged(first, last, roles);
    }
};

// Load externally implemented business declarations before replacing MediaLibrary:
// otherwise benchmark TUs declare AccountHistoryServiceImpl(ObservedLibrary&),
// which does not match the constructor exported by media_business.
// The window currently lives in main.cpp. Include it in this separate executable
// to drive its real callbacks without exposing test-only APIs in production.
#define MediaLibrary ObservedLibrary
#define QTableWidget ObservedTable
#define main mediaApplicationMain
#include "../main.cpp"
#undef main
#undef QTableWidget
#undef MediaLibrary

#include <QTemporaryDir>
#include <QTimer>
#include <memory>
#include <stdexcept>

namespace {
namespace UiTest {
inline void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

inline QByteArray readFile(const QString& path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "cannot read fixture file");
    return file.readAll();
}

inline void finishLayout(ObservedTable* table) {
    // Include posted layouts, header resize timers and rendering in measurements.
    for (int pass = 0; pass < 3; ++pass) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }
    for (int column = 0; column < table->columnCount(); ++column)
        table->horizontalHeader()->sectionSize(column);
    QCoreApplication::processEvents();
}

inline QPushButton* button(QWidget* parent, const QString& text) {
    for (auto* candidate : parent->findChildren<QPushButton*>())
        if (candidate->text() == text)
            return candidate;
    throw std::runtime_error("button not found");
}

inline QLineEdit* searchInput(QWidget* parent, const QString& labelText) {
    for (auto* label : parent->findChildren<QLabel*>())
        if (label->text() == labelText)
            if (auto* input = qobject_cast<QLineEdit*>(label->buddy()))
                return input;
    throw std::runtime_error("search input not found");
}

inline QWidget* formField(QWidget* parent, const QString& labelText) {
    auto* form = qobject_cast<QFormLayout*>(parent->layout());
    check(form != nullptr, "form layout not found");
    for (int row = 0; row < form->rowCount(); ++row) {
        auto* labelItem = form->itemAt(row, QFormLayout::LabelRole);
        auto* label = labelItem ? qobject_cast<QLabel*>(labelItem->widget()) : nullptr;
        if (label && label->text() == labelText)
            return form->itemAt(row, QFormLayout::FieldRole)->widget();
    }
    throw std::runtime_error("form field not found");
}

inline QLineEdit* addInput(QDialog* dialog, const QString& label) {
    auto* input = qobject_cast<QLineEdit*>(formField(dialog, label));
    check(input != nullptr, "add input not found");
    return input;
}

struct Fixture {
    QTemporaryDir directory;
    QString csvPath = directory.filePath("records.csv");
    ObservedLibrary library{csvPath, QStringLiteral("test")};
    std::unique_ptr<LibraryWindow> window;
    ObservedTable* table = nullptr;
    QTabBar* tabs = nullptr;

    explicit Fixture(const QString& seed) {
        check(directory.isValid() && QFile::copy(seed, csvPath), "cannot copy fixture CSV");
        check(library.load().success, "cannot load fixture CSV");
        window = std::make_unique<LibraryWindow>(library);
        window->show();
        table = static_cast<ObservedTable*>(window->findChild<QTableWidget*>());
        tabs = window->findChild<QTabBar*>();
        check(table && tabs, "table or tabs not found");
        finishLayout(table);
        resetCounters();
    }

    void resetCounters() {
        library.addCalls = library.loadCalls = 0;
        table->resetCounters();
    }

    QString detail(const QString& label) const {
        auto* body = window->findChild<QWidget*>(QStringLiteral("detailBody"));
        return qobject_cast<QLabel*>(formField(body, label))->text();
    }

    void fillAddForm(QDialog* dialog, const QString& type) {
        addInput(dialog, QStringLiteral("文件名"))->setText(QStringLiteral("回归新增.mp4"));
        addInput(dialog, QStringLiteral("文件类型"))->setText(type);
        addInput(dialog, QStringLiteral("导演"))->setText(QStringLiteral("回归导演"));
        addInput(dialog, QStringLiteral("演员"))->setText(QStringLiteral("回归演员"));
        addInput(dialog, QStringLiteral("发行日期"))->setText(QStringLiteral("2026-01-01"));
    }
};
} // namespace UiTest
} // namespace
