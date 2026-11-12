#include <QTableWidget>
#include <QElapsedTimer>
#include <QStyledItemDelegate>
class CountingDelegate:public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    mutable quint64 hints=0;
    QSize sizeHint(const QStyleOptionViewItem& o,const QModelIndex& i) const override {
        ++hints;return QStyledItemDelegate::sizeHint(o,i);
    }
};
class CountingTable:public QTableWidget {
public:
    CountingDelegate* counter=new CountingDelegate(this);
    mutable quint64 columnCalls=0,columnNs=0;
    quint64 dataCalls=0,dataNs=0;
    CountingTable() { setItemDelegate(counter); }
    void resetCounters() { columnCalls=columnNs=dataCalls=dataNs=counter->hints=0; }
    void postLayout() { scheduleDelayedItemsLayout(); }
protected:
    int sizeHintForColumn(int column) const override {
        ++columnCalls;QElapsedTimer timer;timer.start();
        int result=QTableWidget::sizeHintForColumn(column);
        columnNs+=timer.nsecsElapsed();return result;
    }
    void dataChanged(const QModelIndex& a,const QModelIndex& b,const QList<int>& roles={}) override {
        ++dataCalls;QElapsedTimer timer;timer.start();
        QTableWidget::dataChanged(a,b,roles);
        dataNs+=timer.nsecsElapsed();
    }
};
#define QTableWidget CountingTable
#define main originalAppMain
#include "../../frontend/main.cpp"
#undef main
#undef QTableWidget
#include <QTemporaryDir>
#include <QTextStream>
int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setStyle("Fusion");app.setStyleSheet(libraryStyle());
    QTextStream out(stdout);QTemporaryDir dir;
    const auto path=dir.filePath("records.csv");
    if(!QFile::copy(QString::fromLocal8Bit(argv[1]),path)) return 2;
    MediaLibrary lib(path,"test");if(!lib.load().success) return 3;
    LibraryWindow window(lib);window.show();app.processEvents();
    auto* table=static_cast<CountingTable*>(window.findChild<QTableWidget*>());
    auto* tabs=window.findChild<QTabBar*>();
    QElapsedTimer timer;timer.start();SearchTrace trace;
    const auto ids=lib.search({},trace);
    out<<"baseline rows="<<lib.records().size()<<" empty_search_ms="<<timer.nsecsElapsed()/1000000.0
       <<" search_results="<<ids.size()<<"\n";out.flush();
    const auto changeTab=[&](int index,const char* name,bool posted=false) {
        const int before=table->rowCount();
        table->resetCounters();if(posted) table->postLayout();
        timer.start();tabs->setCurrentIndex(index);
        const auto synchronous=timer.elapsed();
        app.processEvents();
        out<<name<<" rows="<<before<<"->"<<table->rowCount()<<" sync_ms="<<synchronous
           <<" total_ms="<<timer.elapsed()<<" dataChanged="<<table->dataCalls
           <<" columnCalls="<<table->columnCalls<<" column_ms="<<table->columnNs/1000000.0
           <<" itemHints="<<table->counter->hints<<"\n";out.flush();
    };
    changeTab(1,"media_to_search");
    changeTab(0,"search_to_media");
    changeTab(1,"media_to_search_posted_layout",true);
    changeTab(2,"search_to_recommend");
    changeTab(1,"recommend_to_search");
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    app.processEvents();
    changeTab(0,"search_to_media_fixed_width");
}

