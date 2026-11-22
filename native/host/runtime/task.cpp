#include "task.hpp"
#include "task_abi.hpp"
#include <opencv2/imgproc.hpp>
namespace okww::runtime {
void TaskContext::beginWorkflow(QJsonArray steps, QString timingPath) {
    rustRun(int(rust::TaskMethod::BeginWorkflow), {}, {{"steps", steps}, {"timing_path", timingPath}});
}
void TaskContext::workflowStep(const QString &id, const QString &state, const QString &detail) {
    rustRun(int(rust::TaskMethod::WorkflowStep), {}, {{"id", id}, {"state", state}, {"detail", detail}});
}
void TaskContext::publishWorkflow() { rustRun(int(rust::TaskMethod::PublishWorkflow)); }
void TaskContext::reportProgress(QString stage, double current, double total, QString unit, QString detail) {
    rustRun(int(rust::TaskMethod::ReportProgress), {current, total},
            {{"stage", stage}, {"unit", unit}, {"detail", detail}});
}
TaskContext::TaskContext(Execution &e, Input &i, Templates &t, Profile &p, FrameSource source,
                         TextReader reader)
    : source_(std::move(source)), textReader_(std::move(reader)), execution(e), input(i), templates(t),
      profile(p) {}
void TaskContext::refresh() { rustRun(int(rust::TaskMethod::Refresh)); }
void TaskContext::sleep(double seconds) { rustRun(int(rust::TaskMethod::Sleep), {seconds}); }

QVector<TextLine> TaskContext::text(double x, double y, double right, double bottom, double threshold) {
    return text(gameBox(frame, x, y, right, bottom), threshold);
}

QString TaskContext::hotkey(const QString &setting, const QString &fallback) const {
    return profile.value("Game Hotkey", setting, fallback).toString();
}
bool TaskContext::inTeam() { return party().visible; }
bool TaskContext::canBackToWorld() {
    if (!streamedCapture)
        return true;
    if (inTeam())
        return false;
    return findAny({"gray_book_quest", "gray_book_all_monsters", "gray_book_boss",
        "gray_book_weekly", "esc_setting", "team_close", "btn_dialog_close", "map_my_location"},
        {.threshold = .82, .grayscale = true, .blurSigma = .6}).has_value();
}
PartyHud TaskContext::party() {
    if (streamedImage() && remotePartyVersion_ == frameVersion && remotePartySize_ == frame.size())
        return remoteParty_;
    const auto result = rustRun(int(streamedImage() ? rust::TaskMethod::RemoteParty : rust::TaskMethod::Party));
    if (streamedImage()) {
        remotePartyVersion_ = frameVersion;
        remotePartySize_ = frame.size();
        remoteParty_ = {result[0] != 0, int(result[1]), int(result[2])};
        if (remoteParty_.visible && remoteParty_.current >= 0) {
            knownRemotePartySize_ = remoteParty_.size;
            lastRemotePartySeen_ = execution.now();
        } else if (!remoteParty_.visible && execution.now() - lastRemotePartySeen_ > 2.) {
            knownRemotePartySize_ = 0;
        }
        partyObservation["current"] = remoteParty_.current;
        partyObservation["size"] = remoteParty_.size;
        partyObservation["visible"] = remoteParty_.visible;
    }
    return {result[0] != 0, int(result[1]), int(result[2])};
}
bool TaskContext::monthlyCard() { return rustRun(int(rust::TaskMethod::MonthlyCard))[0] != 0; }
void TaskContext::monthlyWindow() { rustRun(int(rust::TaskMethod::MonthlyWindow)); }
bool TaskContext::skipConfirm() { return rustRun(int(rust::TaskMethod::SkipConfirm))[0] != 0; }
bool TaskContext::travelButton() { return rustRun(int(rust::TaskMethod::TravelButton))[0] != 0; }
bool TaskContext::fastTravel() { return rustRun(int(rust::TaskMethod::FastTravel))[0] != 0; }
bool TaskContext::login() { return rustRun(int(rust::TaskMethod::Login))[0] != 0; }
bool TaskContext::autoPick() { return rustRun(int(rust::TaskMethod::AutoPick))[0] != 0; }
bool TaskContext::autoDialog() { return rustRun(int(rust::TaskMethod::AutoDialog))[0] != 0; }
QVector<TaskDefinition> builtinTasks() {
    return {{"AutoLoginTask", true, 5, [](TaskContext &c) { return c.login(); }},
            {"FastTravelTask", true, .1, [](TaskContext &c) { return c.fastTravel(); }},
            {"AutoPickTask", true, .1, [](TaskContext &c) { return c.autoPick(); }},
            {"AutoDialogTask", true, .5, [](TaskContext &c) { return c.autoDialog(); }}};
}
} // namespace okww::runtime
