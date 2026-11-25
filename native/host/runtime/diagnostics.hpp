#pragma once
#include "diagnostic_abi.hpp"
#include "platform_workflow.hpp"
#include "app_info.hpp"
#include "common.hpp"
#include <optional>

namespace okww::runtime {
class ExecutionFailure : public std::runtime_error {
  public:
    QString details;
    ExecutionFailure(const QString &message, QString trace)
        : std::runtime_error(message.toUtf8().constData()), details(std::move(trace)) {}
};

inline bool diagnosticFileUnder(const QString &file, const QString &directory) {
    const auto info = QFileInfo(file);
    const auto root = QFileInfo(directory).canonicalFilePath();
    return info.isFile() && !root.isEmpty() &&
           info.canonicalFilePath().startsWith(root + '/', Qt::CaseInsensitive);
}

inline void collectDiagnosticFiles(const QString &data, const QString &folder, QJsonObject &event) {
    const QDir output(folder);
    auto copyScreenshot = [&](QJsonObject &report, const QString &destination) {
        const auto source = report["screenshot"].toString();
        if (source.isEmpty())
            return;
        report.remove("screenshot");
        const auto name = QStringLiteral("screenshot.png");
        if (QFileInfo(source).suffix().compare("png", Qt::CaseInsensitive) == 0 &&
            diagnosticFileUnder(source, data + "/screenshots/errors") &&
            QFile::copy(source, destination + '/' + name))
            report["screenshot"] = name;
        else
            report["screenshot_copy_error"] = "无法将现场截图复制到诊断目录";
    };
    copyScreenshot(event, folder);

    QJsonArray reports;
    int omitted = 0;
    const auto requested = event["step_diagnostics"].toArray();
    for (const auto &entry : requested) {
        const auto source = entry.toString();
        if (reports.size() >= 3 || QFileInfo(source).fileName() != "report.json" ||
            !diagnosticFileUnder(source, data + "/logs/diagnostics") ||
            QFileInfo(source).size() > 2 * 1024 * 1024) {
            ++omitted;
            continue;
        }
        try {
            auto step = readJson(source);
            if (step["phase"] != "task_step" || step["diagnostic_id"].toString().isEmpty()) {
                ++omitted;
                continue;
            }
            const auto relative = QString("steps/%1").arg(reports.size() + 1, 2, 10, QChar('0'));
            const auto directory = output.filePath(relative);
            if (!QDir().mkpath(directory)) {
                ++omitted;
                continue;
            }
            step.remove("diagnostic_directory");
            step.remove("diagnostic_report");
            const QDir sourceDir(QFileInfo(source).absolutePath());
            const auto image = sourceDir.filePath("screenshot.png");
            if (step["screenshot"] == "screenshot.png" && diagnosticFileUnder(image, sourceDir.absolutePath()) &&
                QFile::copy(image, directory + "/screenshot.png")) {
                step["screenshot"] = "screenshot.png";
            } else {
                copyScreenshot(step, directory);
            }
            writeJson(directory + "/report.json", step);
            reports.append(relative + "/report.json");
        } catch (const std::exception &) {
            ++omitted;
        }
    }
    if (!requested.isEmpty())
        event["step_diagnostics"] = reports;
    if (omitted)
        event["step_diagnostics_omitted"] = omitted;
}

// Local records deliberately omit configuration/credentials. Keep recent logs
// bounded so reporting an error cannot duplicate a large runtime log in memory.

inline QJsonObject saveDiagnostic(const QString &data, QJsonObject event) {
    QString id, folder;
    MEMORYSTATUSEX memory{sizeof(memory)};
    std::optional<QFile> input;
    std::optional<QSaveFile> output;
    QByteArray tail;
    static const char *const names[]{"native.log", "workshop.log"};
    rust::platformWorkflowRun(
        rust::WorkflowMethod::SaveDiagnostic, {},
        [&](rust::WorkflowOp op, const double *a, double *out, const std::exception_ptr &error) {
            switch (op) {
            case rust::WorkflowOp::DiagnosticMetadata:
                id = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + '-' +
                     QUuid::createUuid().toString(QUuid::Id128).left(8);
                folder = data + "/logs/diagnostics/" + id;
                event["diagnostic_id"] = id;
                event["diagnostic_directory"] = folder;
                event["diagnostic_report"] = folder + "/report.json";
                event["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
                event["version"] = product::releaseVersion;
                event["os"] = QSysInfo::prettyProductName();
                break;
            case rust::WorkflowOp::DiagnosticMemoryStatus:
                out[0] = GlobalMemoryStatusEx(&memory);
                break;
            case rust::WorkflowOp::DiagnosticMemoryFields:
                event["memory"] = QJsonObject{{"available_physical_bytes", double(memory.ullAvailPhys)},
                                              {"available_commit_bytes", double(memory.ullAvailPageFile)}};
                break;
            case rust::WorkflowOp::DiagnosticMakeDirectory:
                out[0] = QDir().mkpath(folder);
                break;
            case rust::WorkflowOp::DiagnosticCreateInput:
                input.emplace(data + "/logs/" + names[int(a[0])]);
                break;
            case rust::WorkflowOp::DiagnosticOpenInput:
                out[0] = input->open(QIODevice::ReadOnly);
                break;
            case rust::WorkflowOp::DiagnosticDropInput:
                input.reset();
                break;
            case rust::WorkflowOp::DiagnosticSeekTail:
                input->seek(rust::okww_rs_diagnostic_tail_start(input->size()));
                break;
            case rust::WorkflowOp::DiagnosticCreateOutput:
                output.emplace(folder + '/' + names[int(a[0])]);
                break;
            case rust::WorkflowOp::DiagnosticReadTail:
                tail = input->read(qint64(a[0]));
                break;
            case rust::WorkflowOp::DiagnosticOpenOutput:
                out[0] = output->open(QIODevice::WriteOnly);
                break;
            case rust::WorkflowOp::DiagnosticWriteTail:
                out[0] = output->write(tail) == tail.size();
                break;
            case rust::WorkflowOp::DiagnosticCommitOutput:
                out[0] = output->commit();
                break;
            case rust::WorkflowOp::DiagnosticDropLog:
                tail = {};
                output.reset();
                input.reset();
                break;
            case rust::WorkflowOp::DiagnosticWriteReport:
            {
                collectDiagnosticFiles(data, folder, event);
                auto portable = event;
                portable.remove("diagnostic_directory");
                portable.remove("diagnostic_report");
                writeJson(event["diagnostic_report"].toString(), portable);
                break;
            }
            case rust::WorkflowOp::DiagnosticWriteErrorField:
                try {
                    std::rethrow_exception(error);
                } catch (const std::exception &e) {
                    event["diagnostic_write_error"] = errorText(e);
                }
                break;
            case rust::WorkflowOp::DiagnosticFailure: {
                static const char *const errors[]{"无法创建诊断目录", "无法保存诊断日志"};
                fail(QString::fromUtf8(errors[int(a[0])]));
            }
            default:
                fail("Rust 诊断记录宿主无效");
            }
        });
    return event;
}
} // namespace okww::runtime
