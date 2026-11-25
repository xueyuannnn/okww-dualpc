#include "navigation.hpp"
#include "combat_vision.hpp"
#include "workflow_abi.hpp"
#include "book_rows.hpp"
namespace okww::runtime {
namespace {
const QStringList guideTabs{"gray_book_quest", "gray_book_boss", "gray_book_weekly", "gray_book_all_monsters"};
const QHash<QString, QString> guideCategories{
    {"ningsu", "凝素领域|凝素領域|Forgery\\s*Challenge"},
    {"moni", "模拟领域|模擬領域|Simulation\\s*Challenge"},
    {"qiangdi", "讨伐强敌|討伐強敵|Boss\\s*Challenge"},
    {"wuyin", "无音区|無音區|无音清剿|無音清剿|Tacet\\s*(?:Suppression|Fields?)"},
    {"zhange", "战歌重奏|戰歌重奏|Weekly\\s*Challenge"},
    {"canxiang", "残像聚落|殘像聚落|Tacet\\s*Discord\\s*Colon(?:y|ies)"},
    {"mengyan", "特异梦魇|特異夢魘|Special\\s*Nightmare"}};

std::optional<TextLine> guideCategoryLabel(TaskContext &c, const QString &name) {
    return matchingText(c.text(.08, .18, .37, .90, .65), "^(?:" + guideCategories.value(name) + ")$");
}
QRect guideCategorySample(TaskContext &c, const QString &name) {
    const auto label = guideCategoryLabel(c, name);
    if (!label) return {};
    const auto y = label->center().y() / c.frame.rows;
    return gameBox(c.frame, .28, y - .012, .32, y + .012);
}
bool confirmedCategory(const cv::Mat &frame, const QRect &area) {
    if (area.isEmpty() || !QRect(0, 0, frame.cols, frame.rows).contains(area)) return false;
    cv::Mat bright;
    cv::inRange(frame(cv::Rect(area.x(), area.y(), area.width(), area.height())),
                cv::Scalar(145, 145, 145), cv::Scalar(255, 255, 255), bright);
    return cv::countNonZero(bright) >= double(bright.total()) * .62;
}

[[noreturn]] void navigationError(int reason, const QString &name = {}) {
    switch (reason) {
    case 0:
        throw NavigationFailure("等待队伍界面超时");
    case 1:
        throw NavigationFailure("未识别到对话框按钮");
    case 2:
        throw NavigationFailure("退出副本后未确认回到大世界");
    case 3:
        throw NavigationFailure("请从游戏内队伍界面开始任务");
    case 4:
        throw NavigationFailure("未找到索拉指南分类，请核对指南快捷键");
    case 5:
        throw NavigationFailure("索拉指南未保持打开，未继续操作菜单");
    case 6:
        throw NavigationFailure("敌迹搜索参数无效");
    case 9:
        throw NavigationFailure("地图上未识别到回血传送点");
    case 10:
        throw NavigationFailure("未识别到回血传送按钮");
    case 11:
        throw NavigationFailure("探测目标后地图未打开");
    case 12:
        throw NavigationFailure("未识别到传送按钮");
    case 13:
        fail("当前挑战等级无法掉落声骸，请提高 Boss 等级");
    case 14:
        throw NavigationFailure("未识别到开始挑战按钮");
    case 15:
        throw NavigationFailure("图鉴序号或滚动范围无效");
    case 16:
        throw NavigationFailure("图鉴分组数量无效");
    case 17:
        throw NavigationFailure("图鉴分组高度超出范围");
    case 18:
        throw NavigationFailure("未找到图鉴前往按钮");
    case 19:
        throw NavigationFailure("滚动后未找到图鉴前往按钮");
    case 20:
        throw NavigationFailure("前往副本后未识别到目标界面");
    case 21:
        throw NavigationFailure("仍在队伍界面，未打开索拉指南。请先在游戏内确认可以用指南快捷键打开");
    case 7:
        throw NavigationFailure("敌迹探寻列表中找不到 " + name + "，请确认已解锁及游戏语言");
    case 8:
        throw NavigationFailure("未知图鉴分类：" + name);
    default:
        fail("导航错误编号无效");
    }
}
struct NavigationBridge {
    TaskContext &c;
    QString name = "gray_book_all_monsters", step, expected, normalized, fingerprint, previous;
    QStringList names;
    MatchOptions options;
    QVector<int> structure;
    std::function<void()> operation;
    std::function<bool()> condition;
    QJsonArray catalog;
    std::optional<Match> found, tab, best, item, next;
    std::optional<TextLine> label;
    TextLine resultLine;
    QPointF point;
    QVector<TextLine> lines;
    QVector<Match> rows;
    QVector<BookRow> bookRows, previousBookRows;
    QRect expectedBookCategory;
    QJsonArray bookObservations;
    QVector<Match> waypoints;
    QMap<QString, std::optional<Match>> guideMarkers;
    uint64_t guideFrameVersion = 0;
    QJsonObject guideProbe;
    std::optional<bool> lastGuideVisible;
    int guideKeyAttempts = 0;

    QRect guideSidebar() const {
        return c.templates.box("box_gray_book", {c.frame.cols, c.frame.rows});
    }
    std::optional<Match> guideMarker(const QString &feature) {
        if (guideFrameVersion != c.frameVersion) {
            guideMarkers.clear();
            guideFrameVersion = c.frameVersion;
        }
        if (const auto it = guideMarkers.constFind(feature); it != guideMarkers.cend())
            return it.value();

        const auto box = c.templates.box(feature, {c.frame.cols, c.frame.rows});
        const auto area = box.adjusted(-box.width() / 2, -box.height() * 2 / 3,
                                       box.width() / 2, box.height() * 2 / 3) & guideSidebar();
        QStringList variants{feature};
        if (feature == "gray_book_boss")
            variants << "gray_book_boss_highlight";
        std::optional<Match> match;
        for (const auto &variant : variants) {
            for (const auto scale : {1., .9, 1.1, .8, 1.2, .72, 1.3}) {
                const auto candidate = c.find(variant, {.threshold = .76, .search = area,
                    .grayscale = true, .templateScale = scale, .blurSigma = .5});
                if (candidate && (!match || candidate->confidence > match->confidence))
                    match = candidate;
                if (match && match->confidence >= .95)
                    break;
            }
        }
        guideMarkers.insert(feature, match);
        return match;
    }
    bool bookVisible() {
        const bool teamVisible = c.inTeam();
        const bool local = c.streamedCapture && c.localDesktopGame;
        if (teamVisible && !local)
            return false;
        if (!teamVisible && c.findAny(guideTabs, {.threshold = .75, .search = guideSidebar(), .grayscale = true}))
            return true;
        if (!c.streamedCapture && !c.browserCapture)
            return false;
        int markers = 0;
        for (const auto &feature : guideTabs) {
            const auto marker = guideMarker(feature);
            if (marker && (!teamVisible || marker->confidence >= .85) && ++markers >= 2)
                return true;
        }
        return false;
    }
    std::optional<Match> findBookTab(double threshold) {
        auto match = c.find(name, {.threshold = threshold, .search = guideSidebar(), .grayscale = true});
        const bool teamVisible = c.streamedCapture && c.localDesktopGame && c.inTeam();
        guideProbe = {{"frame_width", c.frame.cols}, {"frame_height", c.frame.rows},
                      {"standard_match", bool(match)}, {"team_hud_visible", teamVisible}};
        if ((match && !teamVisible) || (!c.streamedCapture && !c.browserCapture) || !guideTabs.contains(name))
            return match;

        match = guideMarker(name);
        guideProbe.insert("scaled_match", bool(match));
        if (match && (!teamVisible || match->confidence >= .85)) {
            guideProbe.insert("scaled_score", match->confidence);
            for (const auto &feature : guideTabs) {
                if (feature == name)
                    continue;
                const auto peer = guideMarker(feature);
                if (peer && (!teamVisible || peer->confidence >= .85)) {
                    guideProbe.insert("other_marker", feature);
                    return match;
                }
            }
        }
        return std::nullopt;
    }

    QString key(int index) {
        const QStringList keys{"esc", "alt", "ctrl", "a", "backspace"};
        if (index == 5)
            return c.hotkey("Guidebook Key", c.hotkey("索拉指南", "f2"));
        return keys.at(index);
    }
    std::array<double, 3> run(rust::WorkflowMethod method, std::initializer_list<double> args = {}) {
        return rust::workflowRun(
            method, std::span<const double>(args.begin(), args.size()),
            [&](rust::WorkflowOp op, const double *a, double *out) {
                switch (op) {
                case rust::WorkflowOp::Now: {
                    out[0] = c.execution.now();
                    break;
                }
                case rust::WorkflowOp::Checkpoint: {
                    c.execution.checkpoint();
                    break;
                }
                case rust::WorkflowOp::Refresh: {
                    c.refresh();
                    break;
                }
                case rust::WorkflowOp::ExecutionWait: {
                    c.execution.wait(a[0]);
                    break;
                }
                case rust::WorkflowOp::Sleep: {
                    c.sleep(a[0]);
                    break;
                }
                case rust::WorkflowOp::InvalidCondition: {
                    fail("等待条件参数无效");
                    break;
                }
                case rust::WorkflowOp::InCombat: {
                    out[0] = c.state->inCombat;
                    break;
                }
                case rust::WorkflowOp::CanBackToWorld:
                    out[0] = c.canBackToWorld();
                    break;
                case rust::WorkflowOp::DesktopCapture:
                    out[0] = c.streamedCapture || c.browserCapture;
                    break;
                case rust::WorkflowOp::LocalDesktopGuide:
                    out[0] = c.streamedCapture && c.localDesktopGame;
                    break;
                case rust::WorkflowOp::RetryingStep: {
                    out[0] = c.state->retryingStep;
                    break;
                }
                case rust::WorkflowOp::SetRetryingStep: {
                    c.state->retryingStep = a[0] != 0;
                    break;
                }
                case rust::WorkflowOp::RecoveryEnabled: {
                    out[0] = c.profile.value("Task Recovery", "Resume Interrupted Tasks", true).toBool();
                    break;
                }
                case rust::WorkflowOp::CatalogCount: {
                    catalog = c.profile.catalog()["tasks"].toArray();
                    out[0] = double(catalog.size());
                    break;
                }
                case rust::WorkflowOp::IsCurrentTrigger: {
                    const auto item = catalog.at(int(a[0])).toObject();
                    out[0] = item["id"].toString() == c.task && item["trigger"].toBool();
                    break;
                }
                case rust::WorkflowOp::RecoveryLimit: {
                    out[0] = c.profile.value("Task Recovery", "Recovery Retry Limit", 3).toInt();
                    break;
                }
                case rust::WorkflowOp::Operation: {
                    operation();
                    break;
                }
                case rust::WorkflowOp::Condition: {
                    out[0] = condition();
                    break;
                }
                case rust::WorkflowOp::HasInfo: {
                    out[0] = bool(c.info);
                    break;
                }
                case rust::WorkflowOp::RecoveryNotice: {
                    const QStringList labels{"返回队伍界面", "返回大世界", "打开索拉指南", "点击传送",
                                             "开始挑战"};
                    const QString label = a[0] < 0 ? step : labels.at(int(a[0]));
                    c.info("中断自动继续", QString("%1 · %2/%3").arg(label).arg(int(a[1])).arg(int(a[2])));
                    break;
                }
                case rust::WorkflowOp::RealmIcon: {
                    out[0] =
                        bool(c.find("illusive_realm_exit", {.threshold = a[0], .filter = Filter::White}));
                    break;
                }
                case rust::WorkflowOp::WorldIcon: {
                    out[0] = bool(c.find("world_earth_icon", {.threshold = a[0], .filter = Filter::White}));
                    break;
                }
                case rust::WorkflowOp::InTeam: {
                    out[0] = c.inTeam();
                    break;
                }
                case rust::WorkflowOp::Key: {
                    const auto index = int(a[0]);
                    const bool localGuideKey = index == 5 && c.streamedCapture && c.localDesktopGame;
                    if (index == 5)
                        ++guideKeyAttempts;
                    c.key(key(index), a[1], localGuideKey ? .12 : .02);
                    break;
                }
                case rust::WorkflowOp::KeyDown: {
                    c.input.keyDown(key(int(a[0])));
                    break;
                }
                case rust::WorkflowOp::KeyUp: {
                    c.input.keyUp(key(int(a[0])));
                    break;
                }
                case rust::WorkflowOp::ReleaseInput: {
                    c.input.release();
                    break;
                }
                case rust::WorkflowOp::ClickRelative: {
                    c.clickRelative(a[0], a[1], a[2]);
                    break;
                }
                case rust::WorkflowOp::Scroll: {
                    c.input.scroll(int(a[0]), QPointF(c.frame.cols * a[1], c.frame.rows * a[2]));
                    break;
                }
                case rust::WorkflowOp::FindConfirm: {
                    const auto names = a[0] != 0 ? QStringList{"confirm_btn_hcenter_vcenter",
                                                               "confirm_btn_highlight_hcenter_vcenter"}
                                                 : QStringList{"cancel_button_hcenter_vcenter",
                                                               "cancel_button_highlight_hcenter_vcenter"};
                    found = c.findAny(names);
                    out[0] = found.has_value();
                    break;
                }
                case rust::WorkflowOp::ClickFound: {
                    c.click(*found, a[0]);
                    break;
                }
                case rust::WorkflowOp::FindNamed: {
                    found = c.findAny(names, options);
                    out[0] = found.has_value();
                    break;
                }
                case rust::WorkflowOp::FindExit: {
                    found = c.findAny({"gray_confirm_exit_button"}, {.threshold = a[0]});
                    out[0] = found.has_value();
                    break;
                }
                case rust::WorkflowOp::ClickExitLeft: {
                    c.click(QPointF(found->box.x() - found->box.width(), found->center().y()), a[0]);
                    break;
                }
                case rust::WorkflowOp::LoggedIn: {
                    out[0] = c.state->loggedIn;
                    break;
                }
                case rust::WorkflowOp::SetLoggedIn: {
                    c.state->loggedIn = a[0] != 0;
                    break;
                }
                case rust::WorkflowOp::Login: {
                    out[0] = c.login();
                    break;
                }
                case rust::WorkflowOp::MonthlyCard: {
                    out[0] = c.monthlyCard();
                    break;
                }
                case rust::WorkflowOp::ClearTab: {
                    tab.reset();
                    break;
                }
                case rust::WorkflowOp::BookVisible:
                    lastGuideVisible = bookVisible();
                    out[0] = *lastGuideVisible;
                    break;
                case rust::WorkflowOp::FindBookTab: {
                    tab = findBookTab(a[0]);
                    out[0] = tab.has_value();
                    break;
                }
                case rust::WorkflowOp::ClickTab: {
                    c.click(*tab, a[0]);
                    break;
                }
                case rust::WorkflowOp::NormalizedNameEmpty: {
                    out[0] = normalizeNavigationText(name).isEmpty();
                    break;
                }
                case rust::WorkflowOp::BuildExpectedName: {
                    expected = normalizeNavigationText(name);
                    break;
                }
                case rust::WorkflowOp::ReadMonsterText: {
                    lines = c.text(.1, .18, .365, .85, a[0]);
                    out[0] = double(lines.size());
                    break;
                }
                case rust::WorkflowOp::ClearFingerprint: {
                    fingerprint.clear();
                    break;
                }
                case rust::WorkflowOp::NormalizeMonsterLine: {
                    normalized = normalizeNavigationText(lines.at(int(a[0])).text);
                    break;
                }
                case rust::WorkflowOp::MonsterNameMatches: {
                    out[0] = normalized == expected;
                    break;
                }
                case rust::WorkflowOp::ClickMonsterLine: {
                    resultLine = lines.at(int(a[0]));
                    c.click(resultLine, a[1]);
                    break;
                }
                case rust::WorkflowOp::MonsterLineY: {
                    out[0] = lines.at(int(a[0])).box.y();
                    break;
                }
                case rust::WorkflowOp::PixelRound: {
                    out[0] = pixelRound(a[0]);
                    break;
                }
                case rust::WorkflowOp::AppendFingerprint: {
                    fingerprint += normalized + ":" + QString::number(int(a[0])) + "|";
                    break;
                }
                case rust::WorkflowOp::FingerprintEmpty: {
                    out[0] = fingerprint.isEmpty();
                    break;
                }
                case rust::WorkflowOp::FingerprintMatches: {
                    out[0] = fingerprint == previous;
                    break;
                }
                case rust::WorkflowOp::StoreFingerprint: {
                    previous = fingerprint;
                    break;
                }
                case rust::WorkflowOp::CategoryKind: {
                    out[0] =
                        QStringList{"ningsu", "moni", "qiangdi", "wuyin", "zhange", "canxiang", "mengyan"}
                            .indexOf(name);
                    break;
                }
                case rust::WorkflowOp::FindBookCategory: {
                    label=guideCategoryLabel(c,name);
                    found=label ? std::optional<Match>{{"book_category",label->box,label->confidence}} : std::nullopt;
                    out[0]=found.has_value();
                    break;
                }
                case rust::WorkflowOp::WaypointCandidates: {
                    waypoints.clear();
                    const auto area = gameBox(c.frame, .1, .1, .9, .9);
                    for (const auto &name : {"map_way_point", "map_way_point_big"})
                        waypoints += c.templates.find(c.frame, name, {.threshold = .6, .search = area});
                    out[0] = waypoints.size();
                    break;
                }
                case rust::WorkflowOp::WaypointGeometry: {
                    const auto point = waypoints.at(int(a[0])).center();
                    out[0] = point.x(); out[1] = point.y();
                    break;
                }
                case rust::WorkflowOp::WaypointFrameSize:
                    out[0] = c.frame.cols; out[1] = c.frame.rows;
                    break;
                case rust::WorkflowOp::SelectWaypointCandidate:
                    best = waypoints.at(int(a[0]));
                    break;
                case rust::WorkflowOp::CloseWaypointSelection:
                    c.clickRelative(.06, .5, 1.);
                    break;
                case rust::WorkflowOp::ClearWaypoint: {
                    best.reset();
                    break;
                }
                case rust::WorkflowOp::FindWaypoint: {
                    const QStringList names{"map_way_point", "map_way_point_big"};
                    item = c.find(names.at(int(a[0])),
                                  {.threshold = a[1], .search = gameBox(c.frame, .1, .1, .9, .9)});
                    out[0] = item.has_value();
                    break;
                }
                case rust::WorkflowOp::WaypointExists: {
                    out[0] = best.has_value();
                    break;
                }
                case rust::WorkflowOp::ItemConfidence: {
                    out[0] = item->confidence;
                    break;
                }
                case rust::WorkflowOp::WaypointConfidence: {
                    out[0] = best->confidence;
                    break;
                }
                case rust::WorkflowOp::StoreWaypoint: {
                    best = item;
                    break;
                }
                case rust::WorkflowOp::ClickWaypoint: {
                    c.click(*best, a[0]);
                    break;
                }
                case rust::WorkflowOp::FindTravel: {
                    found = c.findAny({"gray_teleport"});
                    out[0] = found.has_value();
                    break;
                }
                case rust::WorkflowOp::FindWaypointPopup: {
                    found =
                        c.find("map_way_point", {.search = c.templates.box("map_way_point_pop_up_box",
                                                                           {c.frame.cols, c.frame.rows})});
                    out[0] = found.has_value();
                    break;
                }
                case rust::WorkflowOp::ClickTravelRight: {
                    c.click(QPointF(found->box.x() + found->box.width() * 1.5, found->center().y()), a[0]);
                    break;
                }
                case rust::WorkflowOp::DefaultBookName: {
                    name = "gray_book_all_monsters";
                    break;
                }
                case rust::WorkflowOp::LanguageKind: {
                    out[0] = QStringList{"en_US", "zh_TW"}.indexOf(c.gameLanguage);
                    break;
                }
                case rust::WorkflowOp::CrownlessName: {
                    const QStringList names{"Crownless", "無冠者", "无冠者"};
                    name = names.at(a[0] < 0 ? 2 : int(a[0]));
                    break;
                }
                case rust::WorkflowOp::TravelButton: {
                    out[0] = c.travelButton();
                    break;
                }
                case rust::WorkflowOp::FindStartChallenge: {
                    found = c.find("team_start_challenge");
                    out[0] = found.has_value();
                    break;
                }
                case rust::WorkflowOp::StoreMatchPoint: {
                    point = found->center();
                    break;
                }
                case rust::WorkflowOp::FindChallengeText: {
                    label = matchingText(c.text(.64, .82, .99, .98),
                                         "^(?:开启挑战|开始挑战|開啟挑戰|開始挑戰|Start\\s*Challenge)$");
                    out[0] = label.has_value();
                    break;
                }
                case rust::WorkflowOp::StoreTextPoint: {
                    point = label->center();
                    break;
                }
                case rust::WorkflowOp::NoEchoDrop: {
                    out[0] = bool(matchingText(
                        c.text(.22, .4, .78, .57),
                        "无法掉落任何声骸|無法掉落任何聲骸|(?:no|cannot\\s+drop)\\s+(?:any\\s+)?echo"));
                    break;
                }
                case rust::WorkflowOp::AdvancedSkillMode: {
                    out[0] = c.setting("Advanced Skill Material Mode", false).toBool();
                    break;
                }
                case rust::WorkflowOp::ClickPoint: {
                    c.click(point, a[0]);
                    break;
                }
                case rust::WorkflowOp::SkipConfirm: {
                    out[0] = c.skipConfirm();
                    break;
                }
                case rust::WorkflowOp::StructureCount: {
                    out[0] = double(structure.size());
                    break;
                }
                case rust::WorkflowOp::StructureValue: {
                    out[0] = structure.at(int(a[0]));
                    break;
                }
                case rust::WorkflowOp::BookScrollTop: {
                    const auto bars = colorRectangles(c.frame, {{190, 190, 190}, {255, 255, 255}},
                                                      c.frame.cols * 5. / 2560, c.frame.rows * 10. / 1440, -1,
                                                      gameBox(c.frame, .969, .191, .978, .271), .8);
                    out[0] = bars.empty() ? 424. / 2160 : double(bars.front().box.y()) / c.frame.rows;
                    break;
                }
                case rust::WorkflowOp::ReadProceedRows: {
                    c.refresh();
                    if (!expectedBookCategory.isEmpty() && !confirmedCategory(c.frame, expectedBookCategory))
                        throw NavigationFailure("当前已离开目标副本分类，暂停滚动和选行");
                    bookRows=readBookRows(c);
                    rows.clear();
                    for(const auto &row:bookRows)rows.append(row.button);
                    QJsonArray observation;
                    for(const auto &row:bookRows)observation.append(QJsonObject{{"title",row.title},
                        {"y",row.button.center().y()/c.frame.rows},{"confidence",row.button.confidence}});
                    bookObservations.append(observation);
                    if(bookObservations.size()>12)bookObservations.removeFirst();
                    out[0] = double(rows.size());
                    break;
                }
                case rust::WorkflowOp::BookAtTop:
                    out[0]=bookListAtTop(c,int(a[0]));
                    break;
                case rust::WorkflowOp::StoreBookRows:
                    previousBookRows=bookRows;
                    break;
                case rust::WorkflowOp::CompareBookRows:
                    out[0]=bookRowSimilarity(previousBookRows.at(int(a[0])),bookRows.at(int(a[1])),a[2]!=0);
                    out[1]=(previousBookRows.at(int(a[0])).button.center().y()-bookRows.at(int(a[1])).button.center().y())/c.frame.rows;
                    out[2]=previousBookRows.at(int(a[0])).title.isEmpty() && bookRows.at(int(a[1])).title.isEmpty() &&
                        previousBookRows.at(int(a[0])).button.center().y()<c.frame.rows*.28 &&
                        bookRows.at(int(a[1])).button.center().y()<c.frame.rows*.28;
                    break;
                case rust::WorkflowOp::BookSelectionFailure: {
                    QJsonArray observed;
                    for(const auto &row:bookRows)observed.append(QJsonObject{{"title",row.title},
                        {"button",QJsonArray{row.button.box.x(),row.button.box.y(),row.button.box.width(),row.button.box.height()}}});
                    QJsonArray comparisons;
                    for(const auto &previousRow:previousBookRows) {
                        QJsonArray scores;
                        for(const auto &row:bookRows)scores.append(QJsonObject{
                            {"visual",bookRowVisualSimilarity(previousRow,row)},
                            {"identity",bookRowSimilarity(previousRow,row)}});
                        comparisons.append(scores);
                    }
                    const QString reason=int(a[2])==0 ? "列表复位后未稳定" : int(a[2])==2 ? "滚动前后未确认重叠行" : "目标行未稳定";
                    if(c.diagnostic)c.diagnostic("索拉指南选行未确认",{{"requested_index",a[0]},
                        {"total",a[1]},{"rows",observed},{"reason",a[2]},
                        {"reason_text",reason},{"observations",bookObservations},{"comparisons",comparisons}});
                    throw NavigationFailure("索拉指南目标行未确认，已保留当前页面与选行记录");
                }
                case rust::WorkflowOp::LastRowAbove: {
                    out[0] = rows.back().box.y() < c.frame.rows * a[0];
                    break;
                }
                case rust::WorkflowOp::ClickProceedRow: {
                    if(c.info)c.info("目标副本",QString(a[2]<0 ? "索拉指南末尾第 %1 项 · %2" : "索拉指南第 %1 项 · %2")
                        .arg(int(std::abs(a[2]))).arg(bookRows.at(int(a[0])).title));
                    c.click(rows.at(int(a[0])), a[1]);
                    break;
                }
                case rust::WorkflowOp::FindNextInterface: {
                    next = c.findAny({"fast_travel_custom", "gray_teleport", "remove_custom", "team_start_challenge", "team_close"});
                    out[0] = next.has_value();
                    break;
                }
                case rust::WorkflowOp::WeeklyTeleport: {
                    out[0] = c.setting("Teleport to Boss").toString() == "Weekly Challenge";
                    break;
                }
                case rust::WorkflowOp::EarlyTeleportWarning: {
                    QString joined;
                    for (const auto &line : c.text(.23, .4, .78, .57))
                        joined += line.text;
                    joined = normalizeNavigationText(joined).replace("劇情", "剧情");
                    out[0] = QRegularExpression(
                                 "提前到[达達]目[标標]位置可能影[响響]剧情[体體][验驗].*是否[确確][认認]前往")
                                 .match(joined)
                                 .hasMatch();
                    break;
                }
                case rust::WorkflowOp::NextIsTeamClose: {
                    out[0] = next->name == "team_close" || next->name == "team_start_challenge";
                    break;
                }
                case rust::WorkflowOp::Throw: {
                    navigationError(int(a[0]), name);
                    break;
                }
                default:
                    fail("Rust 导航宿主操作无效");
                }
            });
    }
};
} // namespace
QString normalizeNavigationText(const QString &value) {
    auto out = value.toCaseFolded();
    out.remove(QRegularExpression("\\s+"));
    return out;
}
double bookScrollPosition(int serial, int total, const QVector<int> &structure, double top) {
    const std::array args{double(serial), double(total), top};
    return rust::workflowRun(rust::WorkflowMethod::BookScroll, args,
                             [&](rust::WorkflowOp op, const double *a, double *out) {
                                 switch (op) {
                                 case rust::WorkflowOp::StructureCount:
                                     out[0] = double(structure.size());
                                     break;
                                 case rust::WorkflowOp::StructureValue:
                                     out[0] = structure.at(int(a[0]));
                                     break;
                                 case rust::WorkflowOp::Throw:
                                     navigationError(int(a[0]));
                                     break;
                                 default:
                                     fail("Rust 图鉴滚动宿主操作无效");
                                 }
                             })[0];
}
int Navigation::retryLimit() const {
    NavigationBridge b{c_};
    return int(b.run(rust::WorkflowMethod::RetryLimit)[0]);
}
void Navigation::retry(const QString &step, const std::function<void()> &operation) {
    NavigationBridge b{c_};
    b.step = step;
    b.operation = operation;
    b.run(rust::WorkflowMethod::Retry);
}
bool Navigation::observe(const std::function<bool()> &condition, double timeout, double settle) {
    NavigationBridge b{c_};
    b.condition = condition;
    return b.run(rust::WorkflowMethod::Observe, {timeout, settle})[0] != 0;
}
std::optional<Match> Navigation::waitMatch(const QStringList &names, double timeout, MatchOptions options,
                                           double settle) {
    NavigationBridge b{c_};
    b.names = names;
    b.options = options;
    if (b.run(rust::WorkflowMethod::WaitMatch, {timeout, settle})[0] == 0)
        return {};
    return b.found;
}
bool Navigation::inRealm() {
    NavigationBridge b{c_};
    return b.run(rust::WorkflowMethod::InRealm)[0] != 0;
}
bool Navigation::inWorld() {
    NavigationBridge b{c_};
    return b.run(rust::WorkflowMethod::InWorld)[0] != 0;
}
bool Navigation::waitTeam(double timeout, bool required, bool back) {
    NavigationBridge b{c_};
    return b.run(rust::WorkflowMethod::WaitTeam, {timeout, double(required), double(back)})[0] != 0;
}
void Navigation::confirmDialog(bool right) {
    NavigationBridge b{c_};
    b.run(rust::WorkflowMethod::Confirm, {double(right)});
}
void Navigation::exitRealm(double timeout) {
    NavigationBridge b{c_};
    b.run(rust::WorkflowMethod::ExitRealm, {timeout});
}
void Navigation::ensureMain(double timeout, bool back, bool allowBook) {
    NavigationBridge b{c_};
    b.run(rust::WorkflowMethod::EnsureMain, {timeout, double(back), double(allowBook)});
}
void Navigation::ensureBookPage(const QString &feature, const QString &label,
                                const std::function<bool()> &confirmed,
                                const std::function<void()> &select,
                                const std::function<void()> &failure) {
    rust::workflowRun(rust::WorkflowMethod::VerifiedBookPage,{},[&](rust::WorkflowOp op,const double *a,double *out) {
        switch(op) {
        case rust::WorkflowOp::Refresh:c_.refresh();break;
        case rust::WorkflowOp::Checkpoint:c_.execution.checkpoint();break;
        case rust::WorkflowOp::Now:out[0]=c_.execution.now();break;
        case rust::WorkflowOp::ExecutionWait:c_.execution.wait(a[0]);break;
        case rust::WorkflowOp::Sleep:c_.sleep(a[0]);break;
        case rust::WorkflowOp::GuidePageMatches:out[0]=confirmed();break;
        case rust::WorkflowOp::GuidePageOpen:openBook(feature,true);break;
        case rust::WorkflowOp::GuidePageSelect:if(select)select();break;
        case rust::WorkflowOp::GuidePageRetryInfo:
            if(c_.info)c_.info("页面切换",QString("等待%1未完成，重新选择标签（%2/2）").arg(label).arg(int(a[0])));
            break;
        case rust::WorkflowOp::GuidePageFailure:
            if(failure)failure();
            if(c_.diagnostic)c_.diagnostic("指南页面未切换",{{"expected_page",label},{"tab",feature},{"attempts",3}});
            throw NavigationFailure("未确认已进入"+label+"，已保留当前页面与识别记录");
        default:fail("Rust 指南页面确认宿主无效");
        }
    });
}
Match Navigation::openBook(const QString &feature, bool reuseExisting) {
    NavigationBridge b{c_};
    b.name = feature;
    try {
        b.run(rust::WorkflowMethod::OpenBook, {double(reuseExisting)});
    } catch (const NavigationFailure &error) {
        if (c_.diagnostic) {
            QJsonObject details{{"tab", feature}, {"guide_hotkey", b.key(5)},
                {"hotkey_attempts", b.guideKeyAttempts}, {"desktop_capture", c_.streamedCapture},
                {"party_observation", c_.partyObservation}, {"tab_observation", b.guideProbe},
                {"navigation_error", QString::fromUtf8(error.what())}};
            if (b.lastGuideVisible)
                details.insert("last_guide_visible", *b.lastGuideVisible);
            c_.diagnostic("索拉指南导航失败", details);
        }
        throw;
    }
    return *b.tab;
}
TextLine Navigation::selectMonster(const QString &name, int maxPages) {
    NavigationBridge b{c_};
    b.name = name;
    b.run(rust::WorkflowMethod::SelectMonster, {double(maxPages)});
    return b.resultLine;
}
void Navigation::bookCategory(const QString &category, double after) {
    NavigationBridge b{c_};
    b.name = category;
    if (c_.browserCapture && (category == "zhange" || category == "qiangdi")) {
        ensureBookPage("gray_book_boss", category == "zhange" ? "战歌重奏" : "讨伐强敌",
            [&] { return confirmedCategory(c_.frame, guideCategorySample(c_, category)); },
            [&] { b.run(rust::WorkflowMethod::Category, {after}); });
        return;
    }
    b.run(rust::WorkflowMethod::Category, {after});
}
std::optional<Match> Navigation::waypoint() {
    NavigationBridge b{c_};
    b.run(rust::WorkflowMethod::Waypoint);
    return b.best;
}
void Navigation::teleportNearest() {
    NavigationBridge b{c_};
    b.run(rust::WorkflowMethod::Teleport);
}
void Navigation::healAtBeacon() {
    NavigationBridge b{c_};
    b.run(rust::WorkflowMethod::Heal);
}
void Navigation::waitTravel() {
    NavigationBridge b{c_};
    b.run(rust::WorkflowMethod::Travel);
}
void Navigation::startChallenge() {
    NavigationBridge b{c_};
    b.run(rust::WorkflowMethod::StartChallenge);
}
bool Navigation::selectBookEntry(int serial, int total, const QVector<int> &structure, bool fromEnd,
                                 const QString &expectedCategory) {
    NavigationBridge b{c_};
    b.structure = structure;
    if (c_.browserCapture && (expectedCategory == "zhange" || expectedCategory == "qiangdi")) {
        bookCategory(expectedCategory, .35);
        b.expectedBookCategory = guideCategorySample(c_, expectedCategory);
        if (!confirmedCategory(c_.frame, b.expectedBookCategory))
            throw NavigationFailure("索拉指南未确认目标分类，未选择副本");
    }
    return b.run(rust::WorkflowMethod::SelectEntry, {double(serial), double(total), double(fromEnd)})[0] != 0;
}
} // namespace okww::runtime
