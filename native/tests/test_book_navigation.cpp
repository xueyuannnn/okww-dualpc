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
    void uuWeeklyGuideAndChallengeTransition() {
        const auto folder = qEnvironmentVariable("OKWW_UU_REPLAY");
        if (folder.isEmpty()) QSKIP("Set OKWW_UU_REPLAY to the captured UU test frames");
        const auto guide = readImage(folder + "/weekly-live.png");
        const auto challenge = readImage(folder + "/weekly-difficulty.png");
        const auto map = readImage(folder + "/weekly-map.png");
        QVERIFY(!guide.empty() && !challenge.empty() && !map.empty());
        const QString root = OKWW_SOURCE_ROOT;
        Ocr ocr(root + "/assets/ocr/ppocrv6-small");
        Templates templates(root + "/assets/coco_annotations.json");
        QTemporaryDir data;
        Profile profile(data.path(), root + "/native/resources/catalog.json");
        BookClock clock;
        ExecutionGate gate;
        Execution execution(gate, gate.start(), clock);
        const QSize size(guide.cols, guide.rows);
        auto config = Config::defaults();
        config.set("capture.output_width", size.width());
        config.set("capture.output_height", size.height());
        config.set("kmbox.desktop_width", size.width());
        config.set("kmbox.desktop_height", size.height());
        QPoint mouse, selected;
        double clickedAt = 0.;
        Input input(config, size, true, [&](const QJsonObject &packet) {
            if (packet["op"] == "absolute") mouse = {packet["x"].toInt(), packet["y"].toInt()};
            if (packet["op"] == "button" && !packet["down"].toBool()) {
                selected = mouse;
                clickedAt = clock.now();
            }
            return QJsonObject{};
        }, execution);
        TaskContext context(execution, input, templates, profile, [&] {
            if (selected.isNull()) return guide;
            return clock.now() - clickedAt < 2. ? map : challenge;
        }, [&](const auto &frame, QRect area, double threshold) { return ocr.read(frame, area, threshold); });
        context.task = "FarmEchoTask2";
        context.streamedCapture = true;
        context.refresh();
        const auto rows = readBookRows(context);
        for (const auto &row : rows) qInfo() << row.title << row.button.box;
        QVERIFY(rows.size() >= 3);
        QVERIFY(rows[0].title.contains("定序"));
        QVERIFY(rows[1].title.contains("失坠"));
        Navigation navigation(context);
        try {
            QVERIFY(navigation.selectBookEntry(2, 9));
        } catch (const std::exception &error) {
            QFAIL(error.what());
        }
        QVERIFY(std::abs(selected.y() / double(size.height()) - .526) < .015);
        QVERIFY(clock.now() - clickedAt >= 2.);
    }
    void reportedWeeklyGuideKeepsOnlyChallengeButtons() {
        const auto folder = qEnvironmentVariable("OKWW_WEEKLY_GUIDE_REPLAY");
        if (folder.isEmpty()) QSKIP("Set OKWW_WEEKLY_GUIDE_REPLAY to the saved guide diagnostics");
        const auto original = readImage(folder + "/steps/03/screenshot.png");
        const auto scrolled = readImage(folder + "/steps/01/screenshot.png");
        QVERIFY(!original.empty() && !scrolled.empty());
        const QString root = OKWW_SOURCE_ROOT;
        Ocr ocr(root + "/assets/ocr/ppocrv6-small");
        for (int mode = 0; mode < 4; ++mode) {
            const QSize size = mode == 0 ? QSize(1280, 720) : QSize(original.cols, original.rows);
            QTemporaryDir data;
            Templates templates(root + "/assets/coco_annotations.json");
            Profile profile(data.path(), root + "/native/resources/catalog.json");
            BookClock clock;
            ExecutionGate gate;
            Execution execution(gate, gate.start(), clock);
            auto config = Config::defaults();
            config.set("capture.output_width", size.width()); config.set("capture.output_height", size.height());
            config.set("kmbox.desktop_width", size.width()); config.set("kmbox.desktop_height", size.height());
            QPoint mouse, selected;
            Input input(config, size, true, [&](const QJsonObject &packet) {
                if (packet["op"] == "absolute") mouse = {packet["x"].toInt(), packet["y"].toInt()};
                if (packet["op"] == "button" && !packet["down"].toBool()) selected = mouse;
                return QJsonObject{};
            }, execution);
            cv::Mat page, next(size.height(), size.width(), CV_8UC3, cv::Scalar(36, 46, 59));
            const auto teamIcon = templates.pattern("team_start_challenge", size);
            const auto teamBox = templates.box("team_start_challenge", size);
            teamIcon.copyTo(next(cv::Rect(teamBox.x(), teamBox.y(), teamIcon.cols, teamIcon.rows)));
            cv::resize(scrolled, page, {size.width(), size.height()});
            TaskContext context(execution, input, templates, profile, [&] {
                return selected.isNull() ? page : next;
            }, [&](const auto &frame, QRect area, double threshold) { return ocr.read(frame, area, threshold); });
            context.streamedCapture = mode < 2;
            context.localDesktopGame = mode == 1;
            context.browserCapture = mode == 3;
            context.refresh();
            auto rows = readBookRows(context);
            QCOMPARE(rows.size(), 4);
            QVERIFY(rows[1].title.contains("失坠"));
            QVERIFY(rows[2].title.contains("虚妄"));
            QCOMPARE(bookListBoundary(context), 0);
            cv::resize(original, page, {size.width(), size.height()});
            context.refresh();
            rows = readBookRows(context);
            QCOMPARE(rows.size(), 4);
            QVERIFY(rows[0].title.contains("定序"));
            QVERIFY(rows[1].title.contains("失坠"));
            QCOMPARE(bookListBoundary(context), 1);
            Navigation nav(context);
            QVERIFY(nav.selectBookEntry(2, 9));
            QVERIFY(std::abs(selected.y() / double(size.height()) - .526) < .015);
        }
    }
    void observedGuideScreenshot() {
        const auto path=qEnvironmentVariable("OKWW_BOOK_REPLAY");
        if(path.isEmpty())QSKIP("Set OKWW_BOOK_REPLAY to an observed guide screenshot");
        const auto original=readImage(path);QVERIFY(!original.empty());
        const QString root=OKWW_SOURCE_ROOT;
        auto models=root+"/assets/ocr/ppocrv5";
        if(!QFile::exists(models+"/det/det.onnx"))models=root+"/.venv/Lib/site-packages/onnxocr/models/ppocrv5";
        Ocr ocr(models);
        for(int quality:{100,35}) {
            cv::Mat frame=original.clone();
            if(quality!=100) {
                std::vector<uchar> encoded;cv::imencode(".jpg",frame,encoded,{cv::IMWRITE_JPEG_QUALITY,quality});
                frame=cv::imdecode(encoded,cv::IMREAD_COLOR);
            }
            const QSize size(frame.cols,frame.rows);
            QTemporaryDir data;Templates templates(root+"/assets/coco_annotations.json");
            Profile profile(data.path(),root+"/native/resources/catalog.json");
            BookClock clock;ExecutionGate gate;Execution execution(gate,gate.start(),clock);
            auto config=Config::defaults();
            config.set("capture.output_width",size.width());config.set("capture.output_height",size.height());
            config.set("kmbox.desktop_width",size.width());config.set("kmbox.desktop_height",size.height());
            QPoint mouse,selected;
            Input input(config,size,true,[&](auto q){
                if(q["op"]=="absolute")mouse={q["x"].toInt(),q["y"].toInt()};
                if(q["op"]=="button" && !q["down"].toBool())selected=mouse;
                return QJsonObject{};
            },execution);
            TaskContext context(execution,input,templates,profile,[&]{
                if(selected.isNull())return frame;
                cv::Mat next(size.height(),size.width(),CV_8UC3,cv::Scalar(35,43,52));
                auto icon=templates.pattern("team_start_challenge",size);auto box=templates.box("team_start_challenge",size);
                icon.copyTo(next(cv::Rect(box.x(),box.y(),icon.cols,icon.rows)));return next;
            },[&](const auto &image,QRect region,double threshold){return ocr.read(image,region,threshold);});
            context.streamedCapture=true;context.refresh();
            const auto rows=readBookRows(context);
            for(const auto &row:rows)qInfo()<<quality<<row.title<<row.button.box<<row.button.confidence;
            QCOMPARE(rows.size(),4);
            for(const auto &row:rows)QVERIFY(row.button.center().x()<frame.cols*.94);
            Navigation navigation(context);QVERIFY(navigation.selectBookEntry(2,21,{4,5,5,7}));
            QVERIFY(std::abs(selected.y()/double(frame.rows)-.472)<.025);
            QVERIFY(selected.x()/double(frame.cols)<.94);
        }
    }
    void rowIdentitySeparatesStationaryOcrNoiseFromDifferentDestinations() {
        cv::Mat identity(48,192,CV_8UC1);cv::RNG(42).fill(identity,cv::RNG::UNIFORM,0,255);
        BookRow left{{},identity,"山麓回音|声骸奖励"},right{{},identity.clone(),"山麓回音"};
        QVERIFY(bookRowSimilarity(left,right)>=.86);
        right.title="声骸奖励|山麓回音";
        QVERIFY(bookRowSimilarity(left,right)>=.86);
        right.title="另一处无音区|声骸奖励";
        QCOMPARE(bookRowSimilarity(left,right),0.);
        QVERIFY(bookRowSimilarity(left,right,true)>=.90);
        cv::RNG(99).fill(right.identity,cv::RNG::UNIFORM,0,255);
        QCOMPARE(bookRowSimilarity(left,right,true),0.);
    }
    void destination_data() {
        QTest::addColumn<int>("height");QTest::addColumn<int>("serial");QTest::addColumn<bool>("ocr");QTest::addColumn<bool>("tail");
        QTest::addColumn<int>("ocrNoise");
        for(int h:{360,720,1080,1440})for(int n:{1,4,5,9,17,21})
            QTest::newRow(qPrintable(QString("%1-%2").arg(h).arg(n)))<<h<<n<<(n%2!=0)<<false<<0;
        for(int n:{1,2,3})QTest::newRow(qPrintable(QString("tail-%1").arg(n)))<<720<<n<<true<<true<<0;
        for(int noise:{1,2,3,4})for(int n:{1,9,21})
            QTest::newRow(qPrintable(QString("remote-ocr-%1-%2").arg(noise).arg(n)))<<1080<<n<<true<<false<<noise;
        QTest::newRow("remote-small-wheel-sixth") << 720 << 6 << true << false << 5;
    }
    void destination() {
        QJsonObject failure;
        try {
        QFETCH(int,height);QFETCH(int,serial);QFETCH(bool,ocr);QFETCH(bool,tail);
        QFETCH(int,ocrNoise);
        const QSize size(height*16/9,height);
        const QString root=OKWW_SOURCE_ROOT;
        QTemporaryDir data;Templates templates(root+"/assets/coco_annotations.json");
        Profile profile(data.path(),root+"/native/resources/catalog.json");
        BookClock clock;ExecutionGate gate;Execution execution(gate,gate.start(),clock);
        double offset=height*1.1,scrolledAt=-10;int selected=0,upward=0;QPoint mouse;
        const auto rowY=[&](int index){
            const double rebound=ocrNoise==4 && clock.time-scrolledAt<.6 ? height*.018 : 0.;
            return height*(.29+index*.14+(index/5)*.028)-offset+rebound;
        };
        auto config=Config::defaults();config.set("capture.output_width",size.width());config.set("capture.output_height",height);
        config.set("kmbox.desktop_width",size.width());config.set("kmbox.desktop_height",height);
        Input input(config,size,true,[&](QJsonObject q){
            if(q["op"]=="absolute")mouse={q["x"].toInt(),q["y"].toInt()};
            if(q["op"]=="wheel") {
                // The rewards strip consumes the wheel; only the name column scrolls the list here.
                if(mouse.x() < size.width()*.46 || mouse.x() > size.width()*.61)
                    return QJsonObject{};
                const int steps=q["steps"].toInt();
                if(steps>50){++upward;offset=0.;scrolledAt=clock.time;}
                else offset=std::clamp(offset-steps*height*(ocrNoise==5?.006:.045),0.,height*2.362);
            }
            if(q["op"]=="button" && !q["down"].toBool() && mouse.x()>size.width()*.84)
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
                paint("team_start_challenge",templates.box("team_start_challenge",size).topLeft());return frame;
            }
            paint("gray_book_boss",templates.box("gray_book_boss",size).topLeft());
            cv::rectangle(frame,cv::Rect(qRound(size.width()*.972),qRound(height*(offset==0?.195:offset>=height*2.361?.817:.4)),
                std::max(2,qRound(size.width()*.004)),qRound(height*.06)),cv::Scalar(240,240,240),-1);
            const auto button=templates.pattern("boss_proceed",size);
            for(int i=0;i<21;++i) {
                const int y=qRound(rowY(i));
                if(y<height*.215 || y>height*.865)continue;
                cv::rectangle(frame, {qRound(size.width() * .823), y - qRound(height * .035),
                    qRound(size.width() * .021), qRound(height * .070)}, cv::Scalar(215, 215, 215), -1);
                cv::putText(frame,"Boss "+std::to_string(i+1),{qRound(size.width()*.47),y-qRound(height*.045)},
                    cv::FONT_HERSHEY_SIMPLEX,height/1100.,cv::Scalar(230,230,230),std::max(1,height/650));
                cv::RNG random(i+153);
                for(int k=0;k<16;++k)cv::rectangle(frame,
                    cv::Rect(qRound(size.width()*(.42+(k%8)*.008)),y-qRound(height*(.09-(k/8)*.02)),
                             std::max(2,height/95),std::max(2,height/95)),
                    cv::Scalar(random.uniform(40,250),random.uniform(40,250),random.uniform(40,250)),-1);
                if(ocrNoise!=3)paint("boss_proceed",{qRound(size.width()*.935-button.cols/2.),y-button.rows/2});
            }
            if(height>=720) {
                std::vector<uchar> encoded;cv::imencode(".jpg",frame,encoded,{cv::IMWRITE_JPEG_QUALITY,48});
                frame=cv::imdecode(encoded,cv::IMREAD_COLOR);
            }
            return frame;
        };
        int observations=0;
        TaskContext context(execution,input,templates,profile,render,[&](const auto &,QRect,double){
            QVector<TextLine> text;if(!ocr || selected)return text;
            ++observations;
            for(int i=0;i<21;++i){const auto y=rowY(i);if(y<height*.215 || y>height*.865)continue;
                const bool typo=ocrNoise==2 && observations%2;
                text.append({QString(typo ? "B0ss%1" : "Boss%1").arg(i+1),{qRound(size.width()*.47),qRound(y-height*.055),
                    qRound(size.width()*.10),std::max(4,height/60)},.95});
                if(ocrNoise==1 && observations%2)text.append({"Reward description",
                    {qRound(size.width()*.47),qRound(y-height*.025),qRound(size.width()*.2),std::max(4,height/60)},.72});
                if(ocrNoise==3)text.append({i%2 ? "直接挑战" : "前往",
                    {qRound(size.width()*.87),qRound(y-height*.014),qRound(size.width()*.045),qRound(height*.028)},.99});
            }
            if(ocrNoise==1 && observations%3)std::reverse(text.begin(),text.end());
            return text;
        });
        context.task="FarmEchoTask";context.streamedCapture=true;
        context.diagnostic=[&](QString,QJsonObject value){failure=value;failure["offset"]=offset;};
        context.refresh();
        Navigation navigation(context);
        QVERIFY(navigation.selectBookEntry(serial,tail?3:21,{4,5,5,7},tail));
        QCOMPARE(selected,tail?22-serial:serial);if(!tail)QVERIFY(upward>=1);
        if(ocrNoise==3)QVERIFY(mouse.x()<size.width()*.92);
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
