#ifndef Q_MOC_RUN
#include "controller_input_policy_replay.hpp"
#endif
#include <QtTest>
using namespace okww;
class ControllerInputContracts final : public QObject {
    Q_OBJECT
  private slots:
    void input_data() {
        QTest::addColumn<int>("scene");
        QTest::addColumn<int>("method");
        for (int scene = 0; scene < 1024; ++scene)
            for (int method = 0; method < 5; ++method)
                QTest::newRow(qPrintable(QString("%1-%2").arg(scene).arg(method))) << scene << method;
    }
    void input() {
        QFETCH(int, scene);
        QFETCH(int, method);
        QCOMPARE(controller_input_test::outcome(scene, method, true),
                 controller_input_test::outcome(scene, method, false));
    }
    void mapPoint() {
        for (int scene = 0; scene < 4096; ++scene) {
            const int width = (scene * 17) % 7680 + 1, height = (scene * 13) % 4320 + 1;
            const int x = scene % width, y = (scene * 7) % height;
            const QRect rect(scene % 2 ? -1920 : 333, -27, 2560, 1440);
            const QPoint expected(rect.x() + int(int64_t(x) * rect.width() / width),
                                  rect.y() + int(int64_t(y) * rect.height() / height));
            std::array<int32_t, 2> actual{};
            QCOMPARE(rust::okww_rs_controller_map_point(x, y, width, height, rect.x(), rect.y(), rect.width(),
                                                        rect.height(), actual.data()),
                     1);
            QCOMPARE(QPoint(actual[0], actual[1]), expected);
        }
    }
    void recognitionPointFollowsTheResizedWindowClientArea() {
        const QSize recognition(1920, 1080);
        const QPoint point(480, 810);
        for (const QRect client : {QRect(300, 50, 1280, 720), QRect(-1700, 40, 1600, 900),
                                   QRect(120, 80, 2560, 1440), QRect(0, 0, 3840, 2160)}) {
            std::array<int32_t, 2> mapped{};
            QVERIFY(rust::okww_rs_controller_map_point(point.x(), point.y(), recognition.width(),
                                                       recognition.height(), client.x(), client.y(),
                                                       client.width(), client.height(), mapped.data()));
            QCOMPARE(QPoint(mapped[0], mapped[1]),
                     client.topLeft() + QPoint(client.width() / 4, client.height() * 3 / 4));
        }
    }
};
QTEST_GUILESS_MAIN(ControllerInputContracts)
#include "test_controller_input_contract.moc"
