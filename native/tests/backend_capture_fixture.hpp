#pragma once
#include "backend.hpp"

namespace okww {
// Exercise the real input guard and watchdog with published capture frames,
// without opening a device or sending any Windows/USB input.
struct BackendCaptureFixture {
    static void heartbeatStall(Backend &backend,double gapMs=3310.) {
        std::lock_guard lock(backend.mutex_);
        backend.heartbeat_=nowNs()-i64(gapMs*1e6);
        backend.disarmFaultLocked("heartbeat_timeout","任务心跳超过 1500 毫秒未更新");
    }
    static void expireHeartbeatRecovery(Backend &backend) {
        std::lock_guard lock(backend.mutex_);
        backend.heartbeatFaultAt_=nowNs()-10001000000LL;
    }
    Backend backend{false};
    explicit BackendCaptureFixture(Config config = Config::defaults()) {
        backend.start(config, true);
        std::lock_guard lock(backend.mutex_);
        backend.capture_ = std::make_unique<Capture>(backend.cfg_);
        backend.simulation_ = false;
        backend.simulated_.reset();
        publish();
    }
    void publish(double ageMs = 0) {
        std::lock_guard lock(backend.mutex_);
        const QByteArray bytes(24, '\x80');
        backend.capture_->publish(reinterpret_cast<const uint8_t *>(bytes.constData()), size_t(bytes.size()),
                                  4, 4, 4, PixelFormat::NV12, false, nowNs() - i64(ageMs * 1e6));
    }
    void stopCapture() {
        std::lock_guard lock(backend.mutex_);
        backend.capture_->stop();
    }
    void expireRecovery() {
        std::lock_guard lock(backend.mutex_);
        backend.captureRecoveryBegan_ = nowNs() - Backend::captureRecoveryTimeoutNs;
    }
    QVector<QByteArray> history() {
        std::lock_guard lock(backend.mutex_);
        return backend.box_->history();
    }
};
} // namespace okww
