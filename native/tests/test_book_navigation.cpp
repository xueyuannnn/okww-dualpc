#include "runtime/navigation.hpp"
#include "runtime/book_rows.hpp"
#include <QtTest>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
using namespace okww;
using namespace okww::runtime;
struct BookClock final : Clock {
    double time=0;
    double now() const override { return time; }
    void sleep(double seconds) override { time+=seconds; }
};
class BookNavigationTests final : public QObject {
    Q_OBJECT
  private slots:
    void destination_data() {
        QTest::addColumn<int>("height");QTest::addColumn<int>("serial");QTest::addColumn<bool>("ocr");QTest::addColumn<bool>("tail");
        for(int h:{360,720,1080,1440})for(int n:{1,4,5,9,17,21})
            QTest::newRow(qPrintable(QString("%1-%2").arg(h).arg(n)))<<h<<n<<(n%2!=0)<<false;
        for(int n:{1,2,3})QTest::newRow(qPrintable(QString("tail-%1").arg(n)))<<720<<n<<true<<true;
    }
    void destination() {
        QJsonObject failure;
        try {
        QFETCH(int,height);QFETCH(int,serial);QFETCH(bool,ocr);QFETCH(bool,tail);
        const QSize size(height*16/9,height);
        const QString root=OKWW_SOURCE_ROOT;
        QTemporaryDir data;Templates templates(root+"/assets/coco_annotations.json");
        Profile profile(data.path(),root+"/native/resources/catalog.json");
        BookClock clock;ExecutionGate gate;Execution execution(gate,gate.start(),clock);
        double offset=height*1.1;int selected=0,upward=0;QPoint mouse;
        const auto rowY=[&](int index){return height*(.29+index*.14+(index/5)*.028)-offset;};
        auto config=Config::defaults();config.set("capture.output_width",size.width());config.set("capture.output_height",height);
        config.set("kmbox.desktop_width",size.width());config.set("kmbox.desktop_height",height);
        Input input(config,size,true,[&](QJsonObject q){
            if(q["op"]=="absolute")mouse={q["x"].toInt(),q["y"].toInt()};
            if(q["op"]=="wheel") {
                const int steps=q["steps"].toInt();
                if(steps>0){++upward;offset=0.;}
                else offset=std::min(height*2.362,offset-steps*height*.045);
            }
            if(q["op"]=="button" && !q["down"].toBool() && mouse.x()>size.width()*.9)
                for(int i=0;i<21;++i)if(std::abs(mouse.y()-rowY(i))<height*.025)selected=i+1;
            return QJsonObject{};
        },execution);
        const auto render=[&] {
            cv::Mat frame(height,size.width(),CV_8UC3,cv::Scalar(35,43,52));
            const auto paint=[&](const QString &name,QPoint pos){
                auto pattern=templates.pattern(name,size);
                pattern.copyTo(frame(cv::Rect(pos.x(),pos.y(),pattern.cols,pattern.rows)));
            };
            if(selected) {
                paint("team_close",templates.box("team_close",size).topLeft());return frame;
            }
            paint("gray_book_boss",templates.box("gray_book_boss",size).topLeft());
            cv::rectangle(frame,cv::Rect(qRound(size.width()*.972),qRound(height*(offset==0?.195:.4)),
                std::max(2,qRound(size.width()*.004)),qRound(height*.06)),cv::Scalar(240,240,240),-1);
            const auto button=templates.pattern("boss_proceed",size);
            for(int i=0;i<21;++i) {
                const int y=qRound(rowY(i));
                if(y<height*.215 || y>height*.865)continue;
                cv::putText(frame,"Boss "+std::to_string(i+1),{qRound(size.width()*.47),y-qRound(height*.045)},
                    cv::FONT_HERSHEY_SIMPLEX,height/1100.,cv::Scalar(230,230,230),std::max(1,height/650));
                cv::RNG random(i+153);
                for(int k=0;k<16;++k)cv::rectangle(frame,
                    cv::Rect(qRound(size.width()*(.42+(k%8)*.008)),y-qRound(height*(.09-(k/8)*.02)),
                             std::max(2,height/95),std::max(2,height/95)),
                    cv::Scalar(random.uniform(40,250),random.uniform(40,250),random.uniform(40,250)),-1);
                paint("boss_proceed",{qRound(size.width()*.935-button.cols/2.),y-button.rows/2});
            }
            if(height>=720) {
                std::vector<uchar> encoded;cv::imencode(".jpg",frame,encoded,{cv::IMWRITE_JPEG_QUALITY,48});
                frame=cv::imdecode(encoded,cv::IMREAD_COLOR);
            }
            return frame;
        };
        TaskContext context(execution,input,templates,profile,render,[&](const auto &,QRect,double){
            QVector<TextLine> text;if(!ocr || selected)return text;
            for(int i=0;i<21;++i){const auto y=rowY(i);if(y<height*.215 || y>height*.865)continue;
                text.append({QString("Boss%1").arg(i+1),{qRound(size.width()*.47),qRound(y-height*.055),
                    qRound(size.width()*.10),std::max(4,height/60)},.95});}
            return text;
        });
        context.task="FarmEchoTask";context.streamedCapture=true;
        context.diagnostic=[&](QString,QJsonObject value){failure=value;failure["offset"]=offset;};
        context.refresh();
        Navigation navigation(context);
        QVERIFY(navigation.selectBookEntry(serial,tail?3:21,{4,5,5,7},tail));
        QCOMPARE(selected,tail?22-serial:serial);if(!tail)QVERIFY(upward>=1);
        } catch(const std::exception &e) { QFAIL(qPrintable(QString::fromUtf8(e.what())+' '+QString::fromUtf8(QJsonDocument(failure).toJson(QJsonDocument::Compact)))); }
    }
    void existingGuideIsKeptDuringPreparation() {
        const QString root=OKWW_SOURCE_ROOT;QTemporaryDir data;
        Templates templates(root+"/assets/coco_annotations.json");Profile profile(data.path(),root+"/native/resources/catalog.json");
        BookClock clock;ExecutionGate gate;Execution execution(gate,gate.start(),clock);
        auto config=Config::defaults();config.set("capture.output_width",1280);config.set("capture.output_height",720);
        config.set("kmbox.desktop_width",1280);config.set("kmbox.desktop_height",720);
        QJsonArray commands;Input input(config,{1280,720},true,[&](auto q){commands.append(q);return QJsonObject{};},execution);
        cv::Mat frame(720,1280,CV_8UC3,cv::Scalar(21,33,43));
        auto icon=templates.pattern("gray_book_boss",{1280,720});auto box=templates.box("gray_book_boss",{1280,720});
        icon.copyTo(frame(cv::Rect(box.x(),box.y(),icon.cols,icon.rows)));
        const QRect category(220,510,100,24);
        TaskContext context(execution,input,templates,profile,[&]{return frame;},[&](const auto &,auto,double){
            return QVector<TextLine>{{"战歌重奏",category,.95}};
        });
        context.refresh();Navigation navigation(context);navigation.ensureMain(1,true,true);
        QVERIFY(commands.empty());navigation.openBook("gray_book_boss");
        for(const auto &q:commands)QVERIFY(q.toObject()["op"]!="key");
        commands={};navigation.bookCategory("zhange",.1);
        bool observed=false;
        for(const auto &q:commands)if(q.toObject()["op"]=="absolute") {
            QCOMPARE(q.toObject()["x"].toInt(),category.center().x()+1);
            observed=true;
        }
        QVERIFY(observed);
    }
};
QTEST_GUILESS_MAIN(BookNavigationTests)
#include "test_book_navigation.moc"
