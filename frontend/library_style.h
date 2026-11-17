#pragma once

#include <QApplication>
#include <QPalette>
#include <QStyleHints>

inline QString libraryStyle()
{
    return QString::fromLatin1(R"qss(
        QWidget { color: #182942; font-size: 14px; }
        QWidget#libraryWindow, QDialog { background: #f1f5fb; }
        QTabWidget::pane { background: #f1f5fb; border: 1px solid #d7e1ef;
                           border-radius: 8px; }
        QStackedWidget { background: #f1f5fb; }
        QScrollArea { background: #f1f5fb; }
        QFrame#banner { background: #173253; border-radius: 14px; }
        QLabel#appTitle { color: #ffffff; font-size: 27px; font-weight: 700; }
        QLabel#dialogTitle { color: #173253; font-size: 23px; font-weight: 700; }
        QLabel#sectionTitle { color: #173253; font-size: 17px; font-weight: 700; }
        QLabel#muted, QLabel#assistantStatus { color: #536680; }
        QLabel#fieldLabel { color: #536680; }
        QLabel#resultCount { color: #244c8d; background: #e3edff; border-radius: 9px;
                             padding: 5px 11px; font-weight: 700; }
        QFrame#tracePanel { background: #eaf1fc; border: 1px solid #d4e2f8; border-radius: 10px; }
        QLabel#traceStep { background: #ffffff; color: #314b70; border: 1px solid #d4e2f8;
                           border-radius: 8px; padding: 7px 10px; }
        QProgressBar { background: #e1eaf6; border: none; border-radius: 7px; min-height: 14px; }
        QProgressBar::chunk { border-radius: 7px; }
        QProgressBar#linearBar::chunk { background: #667b98; }
        QProgressBar#fixedBar::chunk { background: #4177c0; }
        QProgressBar#shortBar::chunk { background: #007c70; }
        QFrame#recommendCard { background: #ffffff; border: 1px solid #d7e1ef; border-radius: 12px; }
        QFrame#detailCard { background: #ffffff; border: 1px solid #d7e1ef; border-radius: 12px; }
        QWidget#detailBody { background: #ffffff; }
        QScrollArea#detailScroll { border: none; background: transparent; }
        QTabBar::tab { background: #e6edf7; color: #314766; padding: 9px 22px;
                       margin-right: 6px; border-radius: 8px; font-weight: 600; }
        QTabBar::tab:last { margin-right: 0; }
        QTabBar::tab:hover { background: #dce8fb; }
        QTabBar::tab:selected { color: #ffffff; background: #244f91; }
        QLineEdit, QComboBox, QAbstractSpinBox, QTextEdit, QPlainTextEdit {
            background: #ffffff; border: 1px solid #c9d6e8;
            border-radius: 8px; padding: 7px 10px; min-height: 20px;
            selection-background-color: #244f91; selection-color: #ffffff;
        }
        QLineEdit:focus, QComboBox:focus, QAbstractSpinBox:focus,
        QTextEdit:focus, QPlainTextEdit:focus { border-color: #315fa4; }
        QLineEdit:disabled, QComboBox:disabled, QAbstractSpinBox:disabled,
        QTextEdit:disabled, QPlainTextEdit:disabled { background: #eaf1fc; color: #536680; }
        QComboBox { padding-right: 22px; combobox-popup: 0; }
        QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right;
                              width: 24px; border: none; background: transparent; }
        QComboBox::down-arrow { image: url(:/chevron-down.png); width: 16px; height: 16px; }
        QComboBox QAbstractItemView, QAbstractItemView#keywordSuggestions {
            background-color: #ffffff; color: #182942;
            border: 1px solid #c9d6e8; padding: 3px; outline: 0;
            selection-background-color: #244f91; selection-color: #ffffff;
        }
        QComboBox QAbstractItemView::item, QAbstractItemView#keywordSuggestions::item { min-height: 26px; padding: 3px 8px; }
        QComboBox QAbstractItemView::item:hover:!selected,
        QAbstractItemView#keywordSuggestions::item:hover:!selected {
            background-color: #eaf1fc; color: #182942;
        }
        QComboBox QAbstractItemView::item:selected,
        QAbstractItemView#keywordSuggestions::item:selected {
            background-color: #244f91; color: #ffffff;
        }
        QPushButton { background: #ffffff; border: 1px solid #c9d6e8;
                      border-radius: 8px; padding: 8px 14px; font-weight: 600; }
        QPushButton:hover { background: #eaf1fc; border-color: #9cb5dc; }
        QPushButton:focus { border: 1px solid #315fa4; }
        QPushButton:pressed { background: #dce8fa; }
        QPushButton#primary { color: #ffffff; background: #244f91; border-color: #244f91; }
        QPushButton#primary:hover { background: #1a3e75; }
        QPushButton#primary:focus { border: 1px solid #98b7ff; }
        QPushButton#avatarButton { background: #173253; border: 1px solid transparent;
                                   border-radius: 22px; padding: 0; }
        QPushButton#avatarButton:hover, QPushButton#avatarButton:focus { border-color: #98b7ff; }
        QPushButton#danger { color: #9d2c41; }
        QPushButton:disabled, QPushButton#danger:disabled { color: #7d8ba0; background: #f2f5f9; }
        QTableView, QListView, QTreeView { background: #ffffff; alternate-background-color: #f5f8fd;
                       border: 1px solid #d7e1ef; border-radius: 8px; gridline-color: #d7e1ef;
                       selection-background-color: #dceaff; selection-color: #143362; }
        QHeaderView, QTableCornerButton::section { background: #e7eef9; border: none; }
        QHeaderView::section { background: #e7eef9; color: #27476e; border: none;
                                padding: 9px 8px; font-weight: 700; }
        QListWidget { padding: 4px; }
        QListWidget#onlineWarehouseList::item, QListWidget#onlineWarehouseItems::item,
        QListWidget#onlineWarehouseRecommendations::item, QListWidget#careTemporalList::item { padding: 6px; }
        QSplitter::handle { background: transparent; }
    )qss");
}

inline void applyLibraryStyle(QApplication& app)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    app.styleHints()->setColorScheme(Qt::ColorScheme::Light);
#endif
    app.setStyle(QStringLiteral("Fusion"));

    // Fusion still uses the application palette for surfaces without a stylesheet rule.
    const QColor surface(QStringLiteral("#f1f5fb"));
    const QColor text(QStringLiteral("#182942"));
    const QColor muted(QStringLiteral("#536680"));
    QPalette palette(QColor(Qt::white), surface);
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#f5f8fd")));
    palette.setColor(QPalette::ToolTipBase, surface);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::PlaceholderText, muted);
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#244f91")));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, QColor(QStringLiteral("#244f91")));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, muted);
    palette.setColor(QPalette::Disabled, QPalette::Text, muted);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, muted);
    app.setPalette(palette);
    app.setStyleSheet(libraryStyle());
}
