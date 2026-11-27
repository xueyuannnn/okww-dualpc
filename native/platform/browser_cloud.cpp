#include "browser_cloud.hpp"
#include <QPainter>
#include <QSettings>
#include <winhttp.h>

namespace okww {
namespace {
const QString cloudUrl = "https://mc.kurogames.com/cloud/#/";
const QSize viewport(1920, 1080);

QString browserProfile(const QString &kind) {
    const QString suffix = kind == "chrome" ? "-Chrome" : "";
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/OKWW-DualPC/CloudBrowser" + suffix;
}
QString defaultBrowserAssociation() {
    const auto base = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\");
    for (const auto &protocol : {"https", "http"}) {
        const auto progId = QSettings(base + protocol + "\\UserChoice", QSettings::NativeFormat).value("ProgId").toString();
        if (!progId.isEmpty()) return progId;
    }
    return {};
}
QString browserExecutable(const QString &name, const QString &suffix) {
    const auto key = QString("Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\%1").arg(name);
    QStringList candidates;
    for (const auto &hive : {"HKEY_CURRENT_USER\\", "HKEY_LOCAL_MACHINE\\"})
        candidates << QSettings(hive + key, QSettings::NativeFormat).value(".").toString();
    for (const auto &base : {"ProgramFiles(x86)", "ProgramFiles", "LOCALAPPDATA"})
        if (!qEnvironmentVariableIsEmpty(base)) candidates << qEnvironmentVariable(base) + suffix + name;
    for (auto path : candidates) {
        path = path.trimmed();
        if (path.size() > 1 && path.front() == '"' && path.back() == '"') path = path.mid(1, path.size() - 2);
        if (QFileInfo(path).isAbsolute() && QFileInfo(path).isFile()) return QDir::toNativeSeparators(path);
    }
    return {};
}
bool cloudPage(const QUrl &url) {
    return url.scheme() == "https" && url.host() == "mc.kurogames.com" && url.port(443) == 443 &&
           (url.path() == "/cloud" || url.path().startsWith("/cloud/"));
}
bool cloudTarget(const QJsonObject &page) {
    return page["type"] == "page" && cloudPage(QUrl(page["url"].toString()));
}

// Read the decoded track without exporting credentials, opening a page-side socket,
// or polling the desktop. Page screenshots cover login and menus before the stream.
const QString installTap = QString::fromUtf8(R"JS(
(() => {
  if (location.origin !== 'https://mc.kurogames.com' || (location.pathname !== '/cloud' && !location.pathname.startsWith('/cloud/'))) return;
  // Adapted from ok-script 3.0.14 BrowserCaptureMethod. A background cloud
  // tab must not turn a browser pointer-lock rejection into an in-game Escape.
  if (!window.__okwwRequestPointerLock) {
    let locked = null;
    const changed = () => document.dispatchEvent(new Event('pointerlockchange'));
    window.__okwwRequestPointerLock = function () {
      if (locked !== this) { locked = this; changed(); }
      return Promise.resolve();
    };
    Object.defineProperty(Document.prototype, 'pointerLockElement', {
      configurable: true, get() { return locked; }
    });
    Element.prototype.requestPointerLock = window.__okwwRequestPointerLock;
    Document.prototype.exitPointerLock = function () {
      if (locked) { locked = null; changed(); }
    };
  }
  for (const element of document.querySelectorAll('video, canvas')) {
    if (Object.prototype.hasOwnProperty.call(element, 'requestPointerLock'))
      element.requestPointerLock = window.__okwwRequestPointerLock;
  }
  if (window.__okwwReadCloudFrame) return;
  const peers = [];
  const Native = window.RTCPeerConnection;
  if (Native) {
    const registered = new Proxy(Native, {construct(target, args, type) {
      const connection = Reflect.construct(target, args, type);
      peers.push(new WeakRef(connection));
      return connection;
    }});
    if (window.webkitRTCPeerConnection === Native) window.webkitRTCPeerConnection = registered;
    window.RTCPeerConnection = registered;
  }
  const area = el => { const r = el.getBoundingClientRect(); return r.width * r.height; };
  const live = track => track && track.kind === 'video' && track.readyState === 'live';
  const surface = document.createElement('canvas');
  const ctx = surface.getContext('2d', {alpha: false});
  let tap = null;
  const close = item => {
    item.reader.cancel().catch(() => {});
    item.copy.stop();
    if (item.frame) item.frame.close();
    item.frame = null;
  };
  window.__okwwReadCloudFrame = () => {
    let video = null, track = null;
    for (const element of document.querySelectorAll('video')) {
      const current = element.srcObject instanceof MediaStream && element.srcObject.getVideoTracks().find(live);
      if (current && element.videoWidth && area(element) > (video ? area(video) : 0)) {
        video = element;
        track = current;
      }
    }
    if (!track) {
      let pixels = -1;
      for (let i = peers.length - 1; i >= 0; --i) {
        const pc = peers[i].deref();
        if (!pc || pc.signalingState === 'closed') { peers.splice(i, 1); continue; }
        for (const receiver of pc.getReceivers()) {
          if (!live(receiver.track) || receiver.track.muted) continue;
          const settings = receiver.track.getSettings();
          const size = (settings.width || 0) * (settings.height || 0);
          if (size > pixels) { pixels = size; track = receiver.track; }
        }
      }
    }
    if (tap && (!track || tap.track !== track || tap.ended)) { close(tap); tap = null; }
    if (!track || typeof MediaStreamTrackProcessor !== 'function') return {state: 'page'};
    if (!tap) {
      const copy = track.clone();
      let reader;
      try { reader = new MediaStreamTrackProcessor({track: copy}).readable.getReader(); }
      catch (_) { copy.stop(); return {state: 'page'}; }
      const item = tap = {track, copy, reader, frame: null, version: 0, sent: 0, ended: false};
      (async () => {
        try {
          for (;;) {
            const {value, done} = await reader.read();
            if (done) break;
            if (item !== tap) { value.close(); break; }
            if (item.frame) item.frame.close();
            item.frame = value;
            ++item.version;
          }
        } catch (_) {} finally { item.ended = true; }
      })();
    }
    if (!tap.frame || tap.sent === tap.version) return {state: 'waiting'};
    let element = video;
    if (!element) {
      for (const candidate of document.querySelectorAll('canvas, video'))
        if (area(candidate) > (element ? area(element) : 0)) element = candidate;
    }
    const rect = element ? element.getBoundingClientRect() : {x: 0, y: 0, width: innerWidth, height: innerHeight};
    let {x, y, width, height} = rect;
    if (width < 32 || height < 32) return {state: 'page'};
    const frame = tap.frame;
    if (video) {
      let fit = getComputedStyle(video).objectFit;
      const fw = frame.displayWidth, fh = frame.displayHeight;
      if (fit === 'scale-down') fit = fw <= width && fh <= height ? 'none' : 'contain';
      if (fit !== 'fill') {
        const scale = fit === 'none' ? 1 : (fit === 'cover' ? Math.max : Math.min)(width / fw, height / fh);
        width = fw * scale; height = fh * scale;
        x += (rect.width - width) / 2; y += (rect.height - height) / 2;
      }
    }
    // Encode at the source size, and scale only once when composing the viewport.
    const scale = Math.min(1, 1920 / frame.displayWidth, 1080 / frame.displayHeight);
    const encodedWidth = Math.max(1, Math.round(frame.displayWidth * scale));
    const encodedHeight = Math.max(1, Math.round(frame.displayHeight * scale));
    if (surface.width !== encodedWidth) surface.width = encodedWidth;
    if (surface.height !== encodedHeight) surface.height = encodedHeight;
    ctx.drawImage(frame, 0, 0, surface.width, surface.height);
    tap.sent = tap.version;
    return {state: 'video', jpg: surface.toDataURL('image/jpeg', .92).split(',')[1],
            x, y, width, height, clip: [rect.x, rect.y, rect.width, rect.height]};
  };
  window.__okwwCloseCloudCapture = () => { if (tap) { close(tap); tap = null; } };
})();
)JS");

using Http = std::unique_ptr<void, decltype(&WinHttpCloseHandle)>;
Http http(HINTERNET handle) { return Http(handle, &WinHttpCloseHandle); }

struct WebSocketApi {
    using Upgrade = decltype(&WinHttpWebSocketCompleteUpgrade);
    using Send = decltype(&WinHttpWebSocketSend);
    using Receive = decltype(&WinHttpWebSocketReceive);
    HMODULE module = GetModuleHandleW(L"winhttp.dll");
    Upgrade upgrade = reinterpret_cast<Upgrade>(GetProcAddress(module, "WinHttpWebSocketCompleteUpgrade"));
    Send send = reinterpret_cast<Send>(GetProcAddress(module, "WinHttpWebSocketSend"));
    Receive receive = reinterpret_cast<Receive>(GetProcAddress(module, "WinHttpWebSocketReceive"));
    bool available() const { return upgrade && send && receive; }
};
const WebSocketApi &webSocketApi() { static const WebSocketApi api; return api; }

Http request(HINTERNET connection, const QString &path, bool upgrade = false, QString *error = nullptr,
             const wchar_t *verb = L"GET") {
    const auto failure = [error](const QString &operation, DWORD code) {
        if (error) *error = QString("%1（%2）").arg(operation).arg(code);
        return http(nullptr);
    };
    auto req = http(WinHttpOpenRequest(connection, verb, path.toStdWString().c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
    if (!req) return failure("无法创建浏览器连接请求", GetLastError());
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(req.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)))
        return failure("无法设置浏览器连接选项", GetLastError());
    if (upgrade && !WinHttpSetOption(req.get(), WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0))
        return failure("无法建立浏览器页面连接", GetLastError());
    if (!WinHttpSendRequest(req.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req.get(), nullptr))
        return failure("浏览器本地接口未响应", GetLastError());
    DWORD status = 0, bytes = sizeof(status);
    if (!WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status,
                             &bytes, nullptr))
        return failure("无法读取浏览器本地接口响应", GetLastError());
    if (status != (upgrade ? 101u : 200u))
        return failure(upgrade ? "浏览器拒绝页面连接，HTTP" : "浏览器本地接口返回 HTTP", status);
    return req;
}
QJsonDocument response(HINTERNET connection, const QString &path, QString *error = nullptr) {
    const auto req = request(connection, path, false, error);
    if (!req) return {};
    QByteArray data;
    char chunk[4096];
    DWORD length = 0;
    while (WinHttpReadData(req.get(), chunk, sizeof(chunk), &length) && length) {
        data.append(chunk, length);
        if (data.size() > 1024 * 1024) return {};
    }
    return QJsonDocument::fromJson(data);
}
Http debuggerHttp() {
    auto session = http(WinHttpOpen(L"OKWW Cloud Browser", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0));
    if (session) WinHttpSetTimeouts(session.get(), 500, 500, 1000, 1000);
    return session;
}
Http browserConnection(HINTERNET httpSession, const QString &profile, QString &error) {
    error = "浏览器尚未创建本地连接";
    if (!httpSession) return http(nullptr);
    QFile active(profile + "/DevToolsActivePort");
    if (!active.open(QIODevice::ReadOnly)) return http(nullptr);
    const auto lines = active.read(256).replace('\r', "").split('\n');
    // Chromium replaces this file when its debugging listener starts. Keeping
    // it open during HTTP retries can prevent that replacement on Windows.
    active.close();
    bool valid = false;
    const int port = lines.value(0).toInt(&valid);
    const auto identity = lines.value(1);
    error = "浏览器本地连接记录无效";
    if (!valid || port < 1 || port > 65535 || !identity.startsWith("/devtools/browser/")) return http(nullptr);
    error = "无法连接专用浏览器，请确认窗口仍然打开";
    auto conn = http(WinHttpConnect(httpSession, L"127.0.0.1", INTERNET_PORT(port), 0));
    if (!conn) return http(nullptr);
    const auto version = response(conn.get(), "/json/version", &error);
    if (version.isNull()) return http(nullptr);
    if (QUrl(version.object()["webSocketDebuggerUrl"].toString()).path() != QString::fromLatin1(identity)) {
        error = "浏览器本地接口与当前专用窗口不匹配";
        return http(nullptr);
    }
    error.clear();
    return conn;
}

int modifier(int code) {
    if (code == 224 || code == 228) return 2;
    if (code == 225 || code == 229) return 8;
    if (code == 226 || code == 230) return 1;
    if (code == 227 || code == 231) return 4;
    return 0;
}
QJsonObject browserKey(int hid, bool down, int modifiers) {
    QString code, key;
    int vk = 0;
    if (hid >= 4 && hid <= 29) {
        vk = 'A' + hid - 4;
        code = "Key" + QString(QChar(vk));
        key = QChar(vk + ((modifiers & 8) ? 0 : 32));
    } else if (hid >= 30 && hid <= 39) {
        vk = '0' + (hid - 29) % 10;
        key = QChar(vk);
        code = "Digit" + key;
        if (modifiers & 8) key = QLatin1String(")!@#$%^&*(").at(vk - '0');
    } else if (hid >= 58 && hid <= 69) {
        vk = VK_F1 + hid - 58;
        code = key = "F" + QString::number(vk - VK_F1 + 1);
    } else if (hid >= 224 && hid <= 231) {
        const int index = (hid - 224) % 4;
        constexpr int vks[]{VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN};
        vk = vks[index];
        key = QStringList{"Control", "Shift", "Alt", "Meta"}.at(index);
        code = key + (hid < 228 ? "Left" : "Right");
    } else {
        switch (hid) {
        case 40: vk = VK_RETURN; code = key = "Enter"; break;
        case 41: vk = VK_ESCAPE; code = key = "Escape"; break;
        case 42: vk = VK_BACK; code = key = "Backspace"; break;
        case 43: vk = VK_TAB; code = key = "Tab"; break;
        case 44: vk = VK_SPACE; code = "Space"; key = " "; break;
        case 45: vk = VK_OEM_MINUS; code = "Minus"; key = "-"; break;
        case 46: vk = VK_OEM_PLUS; code = "Equal"; key = "="; break;
        case 47: vk = VK_OEM_4; code = "BracketLeft"; key = "["; break;
        case 48: vk = VK_OEM_6; code = "BracketRight"; key = "]"; break;
        case 49: vk = VK_OEM_5; code = "Backslash"; key = "\\"; break;
        case 51: vk = VK_OEM_1; code = "Semicolon"; key = ";"; break;
        case 52: vk = VK_OEM_7; code = "Quote"; key = "'"; break;
        case 53: vk = VK_OEM_3; code = "Backquote"; key = "`"; break;
        case 54: vk = VK_OEM_COMMA; code = "Comma"; key = ","; break;
        case 55: vk = VK_OEM_PERIOD; code = "Period"; key = "."; break;
        case 56: vk = VK_OEM_2; code = "Slash"; key = "/"; break;
        case 57: vk = VK_CAPITAL; code = key = "CapsLock"; break;
        case 73: vk = VK_INSERT; code = key = "Insert"; break;
        case 74: vk = VK_HOME; code = key = "Home"; break;
        case 75: vk = VK_PRIOR; code = key = "PageUp"; break;
        case 76: vk = VK_DELETE; code = key = "Delete"; break;
        case 77: vk = VK_END; code = key = "End"; break;
        case 78: vk = VK_NEXT; code = key = "PageDown"; break;
        case 79: vk = VK_RIGHT; code = key = "ArrowRight"; break;
        case 80: vk = VK_LEFT; code = key = "ArrowLeft"; break;
        case 81: vk = VK_DOWN; code = key = "ArrowDown"; break;
        case 82: vk = VK_UP; code = key = "ArrowUp"; break;
        default: fail(QString("浏览器键鼠不支持此按键码：%1").arg(hid));
        }
    }
    QJsonObject event{{"type", down ? "rawKeyDown" : "keyUp"}, {"key", key}, {"code", code},
                      {"windowsVirtualKeyCode", vk}, {"nativeVirtualKeyCode", vk}, {"modifiers", modifiers}};
    if (hid >= 224 && hid <= 231) event["location"] = hid < 228 ? 1 : 2;
    if (down && (key.size() == 1 || hid == 40) && !(modifiers & 7)) {
        const auto printable = hid == 40 ? QString("\r") : key;
        event["type"] = "keyDown";
        event["text"] = printable;
        event["unmodifiedText"] = printable;
    }
    return event;
}
} // namespace

QString cloudBrowserChoice(QString preference, QString systemAssociation, bool chromeInstalled, bool edgeInstalled) {
    if (preference == "chrome") return chromeInstalled ? preference : QString{};
    if (preference == "edge") return edgeInstalled ? preference : QString{};
    if (edgeInstalled && systemAssociation.startsWith("MSEdge", Qt::CaseInsensitive)) return "edge";
    if (chromeInstalled) return "chrome";
    return edgeInstalled ? QString("edge") : QString{};
}

namespace {
struct BrowserInstallation {
    QString path;
    QString name;
    QString profile;
};

BrowserInstallation resolveBrowser(const QString &preference) {
    if (!webSocketApi().available()) fail("网页云游戏需要 Windows 10 或更新版本");
    const auto chrome = browserExecutable("chrome.exe", "/Google/Chrome/Application/");
    const auto edge = browserExecutable("msedge.exe", "/Microsoft/Edge/Application/");
    const auto kind = cloudBrowserChoice(preference, defaultBrowserAssociation(), !chrome.isEmpty(), !edge.isEmpty());
    if (kind.isEmpty()) {
        if (preference == "chrome") fail("未找到 Google Chrome，请安装 Chrome 或更改网页云游戏浏览器");
        if (preference == "edge") fail("未找到 Microsoft Edge，请安装 Edge 或更改网页云游戏浏览器");
        fail("未找到 Google Chrome 或 Microsoft Edge，无法打开网页云游戏");
    }
    return {kind == "chrome" ? chrome : edge, kind == "chrome" ? "Chrome" : "Edge", browserProfile(kind)};
}

bool profileInUse(const QString &profile) {
    const auto path = QDir::toNativeSeparators(profile);
    if (FindWindowExW(HWND_MESSAGE, nullptr, nullptr, path.toStdWString().c_str())) return true;
    // Chromium holds this file exclusively while the dedicated profile is in use.
    const auto lockPath = (path + "\\lockfile").toStdWString();
    const HANDLE lock = CreateFileW(lockPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_SHARING_VIOLATION;
    CloseHandle(lock);
    return false;
}
} // namespace

CloudPageAction cloudPageAction(bool endpointReady, bool hasGamePage, bool profileBusy, bool recentlyOpened) {
    if (endpointReady && hasGamePage) return CloudPageAction::Reuse;
    if (recentlyOpened || (!endpointReady && profileBusy)) return CloudPageAction::Wait;
    return endpointReady ? CloudPageAction::OpenTab : CloudPageAction::Launch;
}

namespace {
void ensureCloudPage(const BrowserInstallation &browser) {
    static std::mutex launchMutex;
    static QHash<QString, i64> lastOpen;
    std::lock_guard lock(launchMutex);
    const auto session = debuggerHttp();
    QString error;
    const auto connection = browserConnection(session.get(), browser.profile, error);
    QJsonObject game;
    if (connection) {
        const auto pages = response(connection.get(), "/json/list", &error);
        if (!pages.isArray()) return;
        for (const auto &entry : pages.array()) {
            if (cloudTarget(entry.toObject())) { game = entry.toObject(); break; }
        }
    }
    const auto prior = lastOpen.value(browser.profile, -1);
    const auto action = cloudPageAction(bool(connection), !game.isEmpty(),
                                        !connection && profileInUse(browser.profile),
                                        prior >= 0 && nowNs() - prior < 10000000000LL);
    if (action == CloudPageAction::Reuse || action == CloudPageAction::Wait) return;
    if (action == CloudPageAction::OpenTab) {
        const auto path = "/json/new?" + QString::fromLatin1(QUrl::toPercentEncoding(cloudUrl));
        if (!request(connection.get(), path, false, &error, L"PUT")) fail(error);
        lastOpen[browser.profile] = nowNs();
        return;
    }
    if (!QDir().mkpath(browser.profile)) fail("无法创建云游戏浏览器配置目录");
    if (profileInUse(browser.profile)) return;
    const auto staleRecord=browser.profile+"/DevToolsActivePort";
    if(QFile::exists(staleRecord) && !QFile::remove(staleRecord))
        fail("无法清理过期的浏览器连接记录，请关闭专用云游戏浏览器后重试；登录状态会保留");
    const QStringList args{"--user-data-dir=" + QDir::toNativeSeparators(browser.profile), "--remote-debugging-port=0", "--remote-debugging-address=127.0.0.1",
                           "--window-size=1920,1080", "--force-color-profile=srgb", "--force-device-scale-factor=1",
                           "--disable-background-timer-throttling", "--disable-renderer-backgrounding",
                           "--disable-backgrounding-occluded-windows", "--disable-features=CalculateNativeWinOcclusion",
                           "--disable-direct-composition", "--webrtc-ip-handling-policy=default",
                           "--no-first-run", "--no-default-browser-check", cloudUrl};
    if (!QProcess::startDetached(browser.path, args)) fail("无法启动专用 " + browser.name + " 窗口");
    lastOpen[browser.profile] = nowNs();
}
} // namespace

void CloudBrowser::prepare(const QString &preference, bool openIfNeeded) {
    const auto browser = resolveBrowser(preference);
    name_ = browser.name;
    profile_ = browser.profile;
    if (openIfNeeded) ensureCloudPage(browser);
    else if (!QFileInfo::exists(profile_ + "/DevToolsActivePort"))
        fail("请先点击“打开云游戏浏览器”，在专用 " + name_ + " 登录后再点“开始”。");
}

struct CloudBrowser::Session {
    Http httpSession = debuggerHttp(), connection = http(nullptr), socket = http(nullptr);
    QString rootFrame;
    i64 lastVideoFrame = 0, lastPageFrame = 0;
    int id = 0, buttons = 0, modifiers = 0;
    QPoint point{viewport.width() / 2, viewport.height() / 2};
    std::set<int> keys;

    QJsonObject command(const QString &method, const QJsonObject &params = {}) {
        auto payload = QJsonDocument(QJsonObject{{"id", ++id}, {"method", method}, {"params", params}}).toJson(QJsonDocument::Compact);
        const auto &api = webSocketApi();
        if (!socket || api.send(socket.get(), WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, payload.data(), DWORD(payload.size())))
            fail("网页云游戏连接已断开：" + method);
        QByteArray message;
        char chunk[65536];
        const auto deadline = nowNs() + 2000000000LL;
        while (nowNs() < deadline) {
            DWORD length = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
            if (api.receive(socket.get(), chunk, sizeof(chunk), &length, &type) || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
                break;
            message.append(chunk, length);
            if (message.size() > 12 * 1024 * 1024) break;
            if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) continue;
            const auto result = QJsonDocument::fromJson(message).object();
            message.clear();
            if (result["method"] == "Page.frameNavigated") {
                const auto frame = result["params"].toObject()["frame"].toObject();
                if (frame["id"].toString() == rootFrame && !cloudPage(QUrl(frame["url"].toString())))
                    fail("浏览器已离开云·鸣潮页面");
            }
            if (result["id"].toInt() != id) continue;
            if (result.contains("error"))
                fail(QString("浏览器指令失败：%1（%2）").arg(method).arg(result["error"].toObject()["code"].toInt()));
            return result["result"].toObject();
        }
        fail("网页云游戏浏览器响应超时：" + method);
    }

    void attach(const QString &profile) {
        QString error;
        connection = browserConnection(httpSession.get(), profile, error);
        if (!connection) fail(error);
        const auto targetList = response(connection.get(), "/json/list", &error);
        if (!targetList.isArray()) fail(error.isEmpty() ? "无法读取浏览器网页列表" : error);
        error = "专用浏览器已打开，正在等待云·鸣潮网页载入";
        const auto pages = targetList.array();
        for (const auto &entry : pages) {
            const auto page = entry.toObject();
            if (!cloudTarget(page)) continue;
            const auto path = QUrl(page["webSocketDebuggerUrl"].toString()).path();
            if (!path.startsWith("/devtools/page/")) continue;
            const auto req = request(connection.get(), path, true, &error);
            if (!req) continue;
            socket = http(webSocketApi().upgrade(req.get(), 0));
            if (!socket) {
                error = QString("无法完成网页连接握手（%1）").arg(GetLastError());
                continue;
            }
            DWORD timeout = 950;
            WinHttpSetOption(socket.get(), WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
            command("Page.enable");
            const auto main = command("Page.getFrameTree")["frameTree"].toObject()["frame"].toObject();
            if (!cloudPage(QUrl(main["url"].toString()))) continue;
            rootFrame = main["id"].toString();
            command("Emulation.setDeviceMetricsOverride", {{"width", viewport.width()}, {"height", viewport.height()},
                     {"screenWidth", viewport.width()}, {"screenHeight", viewport.height()}, {"deviceScaleFactor", 1}, {"mobile", false}});
            command("Page.addScriptToEvaluateOnNewDocument", {{"source", installTap}});
            evaluate(installTap);
            return;
        }
        fail(error);
    }
    QJsonObject evaluate(const QString &script) {
        return command("Runtime.evaluate", {{"expression", script}, {"returnByValue", true}})["result"].toObject()["value"].toObject();
    }
    void mouse(const QString &type, const QString &button = "none", int delta = 0) {
        QJsonObject params{{"type", type}, {"x", point.x()}, {"y", point.y()}, {"button", button},
                           {"buttons", buttons}, {"modifiers", modifiers}};
        if (type == "mouseWheel") { params["deltaX"] = 0; params["deltaY"] = delta; }
        if (type == "mousePressed" || type == "mouseReleased") params["clickCount"] = 1;
        command("Input.dispatchMouseEvent", params);
    }
    void button(const QString &name, bool down) {
        const int mask = name == "left" ? 1 : name == "right" ? 2 : name == "middle" ? 4 : 0;
        if (!mask) fail("浏览器键鼠不支持此鼠标键");
        const int prior = buttons;
        buttons = down ? (buttons | mask) : (buttons & ~mask);
        if (prior != buttons) mouse(down ? "mousePressed" : "mouseReleased", name);
    }
    void key(int code, bool down) {
        if (down == keys.contains(code)) return;
        const auto mask = modifier(code);
        const auto counterpart = code < 228 ? code + 4 : code - 4;
        const auto next = down ? modifiers | mask :
            mask && keys.contains(counterpart) ? modifiers : modifiers & ~mask;
        const auto params = browserKey(code, down, next);
        if (down) keys.insert(code); else keys.erase(code);
        modifiers = next;
        command("Input.dispatchKeyEvent", params);
    }
    void release() {
        for (const auto code : std::set<int>(keys)) key(code, false);
        for (const auto &name : {"left", "right", "middle"}) button(name, false);
    }
};

CloudBrowser::CloudBrowser() = default;
CloudBrowser::~CloudBrowser() {
    release();
    if (session_) try {
        session_->evaluate("window.__okwwCloseCloudCapture?.()");
        session_->command("Emulation.clearDeviceMetricsOverride");
    } catch (...) {}
}
QImage CloudBrowser::frame() {
    std::lock_guard lock(mutex_);
    try {
        if (!session_) {
            auto session = std::make_unique<Session>();
            session->attach(profile_);
            session_ = std::move(session);
        }
        const auto packet = session_->evaluate("window.__okwwReadCloudFrame?.() ?? {state: 'page'}");
        const auto mode = packet["state"].toString();
        const bool videoFrame = mode == "video";
        const auto now = nowNs();
        if (!videoFrame) {
            // Match the upstream fallback when the video pauses or is replaced.
            // Give normal gaps room so page capture cannot slow down input.
            if (mode == "waiting" && session_->lastVideoFrame && now - session_->lastVideoFrame < 250000000LL)
                return {};
            if (session_->lastPageFrame && now - session_->lastPageFrame < 120000000LL)
                return {};
        }
        const auto bytes = videoFrame ? packet["jpg"].toString() :
            session_->command("Page.captureScreenshot", {{"format", "png"}, {"fromSurface", true}})["data"].toString();
        auto picture = QImage::fromData(QByteArray::fromBase64(bytes.toLatin1()), videoFrame ? "JPEG" : "PNG");
        if (picture.isNull()) fail("网页已连接，但浏览器返回了无效画面");
        if (videoFrame) session_->lastVideoFrame = now;
        else session_->lastPageFrame = now;
        video_ = videoFrame;
        ready_ = true;
        if (!video_) return picture.size() == viewport ? picture : picture.scaled(viewport);
        const QRectF rect(packet["x"].toDouble(), packet["y"].toDouble(), packet["width"].toDouble(), packet["height"].toDouble());
        const auto clip = packet["clip"].toArray();
        QImage image(viewport, QImage::Format_RGB32);
        image.fill(Qt::black);
        QPainter paint(&image);
        if (clip.size() == 4) paint.setClipRect(QRectF(clip[0].toDouble(), clip[1].toDouble(), clip[2].toDouble(), clip[3].toDouble()));
        paint.setRenderHint(QPainter::SmoothPixmapTransform);
        paint.drawImage(rect, picture);
        paint.end();
        return image;
    } catch (const std::exception &error) {
        ready_ = false;
        video_ = false;
        if (session_) try { session_->release(); } catch (...) {}
        session_.reset();
        fail(name_ + " · " + errorText(error));
    }
}
void CloudBrowser::move(int x, int y, int width, int height) {
    std::lock_guard lock(mutex_);
    if (!ready_ || !session_) fail("网页云游戏画面尚未就绪");
    const auto dst = width > 0 && height > 0 ? QPoint(int(x * (double(viewport.width()) / width)), int(y * (double(viewport.height()) / height))) :
        session_->point + QPoint(x, y);
    session_->point = {std::clamp(dst.x(), 0, viewport.width() - 1), std::clamp(dst.y(), 0, viewport.height() - 1)};
    session_->mouse("mouseMoved");
}
void CloudBrowser::button(const QString &name, bool down) {
    std::lock_guard lock(mutex_);
    if (!ready_ || !session_) fail("网页云游戏画面尚未就绪");
    session_->button(name, down);
}
void CloudBrowser::key(int code, bool down) {
    std::lock_guard lock(mutex_);
    if (!ready_ || !session_) fail("网页云游戏画面尚未就绪");
    session_->key(code, down);
}
void CloudBrowser::wheel(int steps) {
    std::lock_guard lock(mutex_);
    if (!ready_ || !session_) fail("网页云游戏画面尚未就绪");
    session_->mouse("mouseWheel", "none", -steps * 100);
}
void CloudBrowser::release() noexcept {
    std::lock_guard lock(mutex_);
    if (session_) try { session_->release(); } catch (...) {}
}
} // namespace okww
