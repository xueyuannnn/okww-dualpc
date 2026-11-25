#pragma once
#include "common.hpp"

namespace okww {
QString cloudBrowserChoice(QString preference, QString systemAssociation, bool chromeInstalled, bool edgeInstalled);
enum class CloudPageAction { Reuse, Wait, OpenTab, Launch };
CloudPageAction cloudPageAction(bool endpointReady, bool hasGamePage, bool profileBusy, bool recentlyOpened);

class CloudBrowser {
    struct Session;
    std::unique_ptr<Session> session_;
    std::mutex mutex_;
    QString profile_;
    QString name_;
    std::atomic<bool> ready_{false};
    std::atomic<bool> video_{false};

  public:
    CloudBrowser();
    ~CloudBrowser();
    void prepare(const QString &preference, bool openIfNeeded);
    QImage frame();
    void move(int x, int y, int width = 0, int height = 0);
    void button(const QString &name, bool down);
    void key(int code, bool down);
    void wheel(int steps);
    void release() noexcept;
    bool ready() const { return ready_; }
    bool video() const { return video_; }
};
} // namespace okww
