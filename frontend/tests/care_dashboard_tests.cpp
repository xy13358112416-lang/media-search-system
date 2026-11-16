#include "care_dashboard.h"
#include "library_style.h"
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QtTest>

namespace {
QPushButton* button(QWidget* parent, const QString& name)
{
    for (auto* entry : parent->findChildren<QPushButton*>())
        if (entry->text() == name)
            return entry;
    return nullptr;
}
}

class CareDashboardTests : public QObject {
    Q_OBJECT
private slots:
    void careSurfacesStayLightWithDarkSystemPalette()
    {
        const auto original = qApp->palette();
        const auto originalStyleSheet = qApp->styleSheet();
        const auto restore = qScopeGuard([&] {
            qApp->setStyleSheet(originalStyleSheet);
            qApp->setPalette(original);
        });
        qApp->setStyle(QStringLiteral("Fusion"));
        auto dark = original;
        dark.setColor(QPalette::Window, QColor("#474747"));
        dark.setColor(QPalette::Base, QColor("#292929"));
        dark.setColor(QPalette::Button, QColor("#474747"));
        qApp->setPalette(dark);
        qApp->setStyleSheet(libraryStyle());

        bool exercised = false;
        QTimer::singleShot(0, this, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] { dialog->reject(); });
            auto* tabs = dialog->findChild<QTabWidget*>("careDashboardTabs");
            QVERIFY(tabs);
            for (int index = 0; index < tabs->count(); ++index) {
                tabs->setCurrentIndex(index);
                auto* page = tabs->currentWidget();
                QVERIFY(page);
                const auto panel = page->grab().toImage();
                QCOMPARE(panel.pixelColor(4, panel.height() / 2), QColor("#f8faff"));
                for (auto* list : page->findChildren<QListWidget*>()) {
                    if (!list->isVisible())
                        continue;
                    const auto body = list->viewport()->grab().toImage();
                    QCOMPARE(body.pixelColor(body.width() / 2, body.height() - 12), QColor("#ffffff"));
                }
            }
            exercised = true;
        });
        showCareDemonstration(nullptr, care_contract::BrowseScene::Neutral);
        QVERIFY(exercised);
    }

    void demoPagesRenderAndHandleUserActions()
    {
        bool exercised = false;
        QTimer::singleShot(0, this, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] { dialog->reject(); });
            auto* tabs = dialog->findChild<QTabWidget*>("careDashboardTabs");
            QVERIFY(tabs);
            QCOMPARE(tabs->count(), 4);

            auto* follow = tabs->currentWidget();
            auto* input = follow->findChild<QLineEdit*>();
            QVERIFY(input);
            input->setText(QStringLiteral("Asquith"));
            auto* search = button(follow, QStringLiteral("查找人物"));
            QVERIFY(search);
            search->click();
            auto* toggle = button(follow, QStringLiteral("关注"));
            QVERIFY(toggle && toggle->isEnabled());
            toggle->click();
            QCOMPARE(toggle->text(), QStringLiteral("取消关注"));

            tabs->setCurrentIndex(1);
            auto* trending = tabs->currentWidget()->findChild<QTableWidget*>("careTrendingTable");
            QVERIFY(trending);
            QCOMPARE(trending->rowCount(), 1);
            QCOMPARE(trending->item(0, 2)->text(), QStringLiteral("12"));

            tabs->setCurrentIndex(2);
            auto* footprint = tabs->currentWidget();
            auto* report = footprint->findChild<QLabel*>("careStatus");
            QVERIFY(report && report->text().contains(QStringLiteral("含模拟数据")));
            QVERIFY(footprint->findChildren<QTableWidget*>().size() == 3);
            auto* clearHistory = button(footprint, QStringLiteral("清理历史"));
            QVERIFY(clearHistory);
            QLabel* counts = nullptr;
            for (auto* label : footprint->findChildren<QLabel*>())
                if (label->accessibleName() == QStringLiteral("足迹统计"))
                    counts = label;
            QVERIFY(counts && counts->text().contains(QStringLiteral("浏览 3 次")));
            const auto answerHistory = [this](QMessageBox::StandardButton choice) {
                QTimer::singleShot(0, this, [choice] {
                    auto* confirmation = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                    QVERIFY(confirmation);
                    confirmation->button(choice)->click();
                });
            };
            answerHistory(QMessageBox::No);
            clearHistory->click();
            QVERIFY(counts->text().contains(QStringLiteral("浏览 3 次")));
            answerHistory(QMessageBox::Yes);
            clearHistory->click();
            QVERIFY(counts->text().contains(QStringLiteral("浏览 0 次")));

            tabs->setCurrentIndex(3);
            auto* recommendation = tabs->currentWidget();
            QVERIFY(!button(recommendation, QStringLiteral("清理历史")));
            auto* results = recommendation->findChild<QListWidget*>("careTemporalList");
            QVERIFY(results);
            auto* cancel = button(recommendation, QStringLiteral("取消"));
            QVERIFY(cancel && cancel->isEnabled());
            cancel->click();
            QTest::qWait(300);
            QCOMPARE(results->count(), 0);
            auto* generate = button(recommendation, QStringLiteral("生成推荐"));
            QVERIFY(generate);
            generate->click();
            QTRY_COMPARE(results->count(), 1);
            results->setCurrentRow(0);
            auto* exclude = button(recommendation, QStringLiteral("不感兴趣"));
            QVERIFY(exclude && exclude->isEnabled());
            exclude->click();
            QCOMPARE(results->count(), 0);
            auto* viewExcluded = button(recommendation, QStringLiteral("查看已排除作品"));
            QVERIFY(viewExcluded);
            viewExcluded->click();
            QCOMPARE(results->count(), 1);
            results->setCurrentRow(0);
            auto* unexclude = button(recommendation, QStringLiteral("撤销不感兴趣"));
            QVERIFY(unexclude && unexclude->isEnabled());
            unexclude->click();
            QCOMPARE(results->count(), 0);
            exercised = true;
        });
        showCareDemonstration(nullptr, care_contract::BrowseScene::Relax);
        QVERIFY(exercised);
    }
};

QTEST_MAIN(CareDashboardTests)
#include "care_dashboard_tests.moc"
