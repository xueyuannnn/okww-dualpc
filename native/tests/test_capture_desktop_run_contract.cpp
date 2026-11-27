#ifndef Q_MOC_RUN
#include "capture_desktop_run_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class CaptureDesktopRunContracts final : public QObject {
    Q_OBJECT
  private slots:
    void capture_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<bool>("dxgi");
        for (int scene = 0; scene < 1024; ++scene)
            for (bool dxgi : {true, false})
                QTest::newRow(qPrintable(QString("%1-%2").arg(scene).arg(dxgi))) << scene << dxgi;
    }
    void capture() {
        QFETCH(int, scene);
        QFETCH(bool, dxgi);
        QCOMPARE(capture_desktop_run_test::outcome(scene, dxgi, true),
                 capture_desktop_run_test::outcome(scene, dxgi, false));
    }
    void recoveryThreshold() {
        for (i64 base : {INT64_C(0), INT64_C(0x123456789abcde00)})
            for (i64 elapsed : {INT64_C(119999999999), INT64_C(120000000000), INT64_C(120000000001)})
                QCOMPARE(bool(rust::okww_rs_capture_recovery_expired(base + elapsed, base)),
                         elapsed > INT64_C(120000000000));
    }
    void selectedWindowPublishesFramesAfterResize() {
        for (bool dxgi : {true, false}) {
            const auto replay = capture_desktop_run_test::resizeOutcome(dxgi);
            QVERIFY(replay["error"].toString().isEmpty());
            QVERIFY(replay["recovery"].toString().isEmpty());
            bool sawOriginal = false, sawResized = false;
            for (const auto frame : replay["trace"].toArray()) {
                const auto entry = frame.toObject();
                sawOriginal |= entry["width"].toInt() == 1280 && entry["height"].toInt() == 720;
                sawResized |= entry["width"].toInt() == 1600 && entry["height"].toInt() == 900;
            }
            QVERIFY(sawOriginal);
            QVERIFY(sawResized);
        }
    }
    void dxgiReopensDuplicationWhenFirstFrameIsMissing() {
        const auto replay = capture_desktop_run_test::firstFrameRecoveryOutcome();
        QVERIFY(replay["error"].toString().isEmpty());
        QVERIFY(replay["recovery"].toString().isEmpty());
        int sessions = 0, published = 0;
        for (const auto value : replay["trace"].toArray()) {
            const auto event = value.toObject();
            sessions += event.contains("duplication");
            if (event.contains("publish")) {
                QCOMPARE(sessions, 2);
                ++published;
            }
        }
        QCOMPARE(sessions, 2);
        QVERIFY(published > 0);
    }
};
QTEST_GUILESS_MAIN(CaptureDesktopRunContracts)
#include "test_capture_desktop_run_contract.moc"
