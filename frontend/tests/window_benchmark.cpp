#include "window_test_support.h"
#include <QElapsedTimer>
#include <QTextStream>

using namespace UiTest;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    applyLibraryStyle(app);
    if (argc < 2 || argc > 3) return 2;
    Fixture f(QString::fromLocal8Bit(argv[1]));
    QFile log;
    if (argc == 3) {
        log.setFileName(QString::fromLocal8Bit(argv[2]));
        if (!log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return 4;
    } else if (!log.open(stdout, QIODevice::WriteOnly)) return 4;
    // A log path also works when the launcher has no attached Windows console.
    QTextStream out(&log);
    out << "rows=" << f.table->rowCount() << " Qt=" << qVersion()
        << " platform=" << QGuiApplication::platformName() << " runs=5\n";
    QList<double> outward, inward, invalid;
    auto changeTab = [&](int tab, const char* name, int run, QList<double>& times) {
        f.resetCounters();
        QElapsedTimer timer;
        timer.start();
        f.tabs->setCurrentIndex(tab);
        const auto synchronous = timer.nsecsElapsed() / 1e6;
        finishLayout(f.table);
        const auto total = timer.nsecsElapsed() / 1e6;
        times.append(total);
        out << name << " run=" << run << " sync_ms=" << synchronous << " total_ms=" << total
            << " columnCalls=" << f.table->columnCalls << " itemHints=" << f.table->counter->hints << '\n';
        out.flush();
    };
    for (int run = 1; run <= 5; ++run) {
        changeTab(1, "media_to_search", run, outward);
        changeTab(0, "search_to_media", run, inward);
    }
    for (int run = 1; run <= 5; ++run) {
        const auto csvBefore = readFile(f.csvPath);
        const auto sequenceBefore = readFile(f.csvPath + QStringLiteral(".id-sequence"));
        bool errorSeen = false;
        bool formKept = false;
        double promptMs = 0;
        QElapsedTimer timer;
        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            f.fillAddForm(dialog, QStringLiteral("213"));
            QTimer::singleShot(0, [&] {
                if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    promptMs = timer.nsecsElapsed() / 1e6;
                    errorSeen = true;
                    message->accept();
                }
            });
            f.resetCounters();
            timer.start();
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
            formKept = dialog->isVisible() && addInput(dialog, QStringLiteral("文件类型"))->text() == QStringLiteral("213");
            dialog->reject();
        });
        button(f.window.get(), QStringLiteral("新增"))->click();
        finishLayout(f.table);
        invalid.append(promptMs);
        out << "invalid_save run=" << run << " prompt_ms=" << promptMs
            << " addCalls=" << f.library.addCalls << " loadCalls=" << f.library.loadCalls
            << " dataChanged=" << f.table->dataCalls << " columnCalls=" << f.table->columnCalls
            << " itemHints=" << f.table->counter->hints
            << " errorSeen=" << errorSeen << " formKept=" << formKept
            << " csvUnchanged=" << (readFile(f.csvPath) == csvBefore)
            << " sequenceUnchanged=" << (readFile(f.csvPath + QStringLiteral(".id-sequence")) == sequenceBefore) << '\n';
        out.flush();
        if (!errorSeen || !formKept) return 3;
    }
    const auto median = [](QList<double> times) {
        std::sort(times.begin(), times.end());
        return times.at(times.size() / 2);
    };
    out << "median media_to_search_ms=" << median(outward) << " search_to_media_ms=" << median(inward)
        << " invalid_prompt_ms=" << median(invalid) << '\n';
}
