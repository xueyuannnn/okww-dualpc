#pragma once
#include "workflow_abi.hpp"
#include <openvino/openvino.hpp>
namespace okww::runtime {
inline void inferWithCheckpoint(ov::InferRequest &request, const std::function<void()> &checkpoint,
                                const QString &name) {
    QElapsedTimer elapsed;
    bool aborted = false;
    const std::array args{double(bool(checkpoint))};
    try {
        rust::workflowRun(rust::WorkflowMethod::InferenceWait, args,
                      [&](rust::WorkflowOp op, const double *a, double *out) {
                          switch (op) {
                          case rust::WorkflowOp::Checkpoint:
                              checkpoint();
                              break;
                          case rust::WorkflowOp::InferenceSynchronous:
                              request.infer();
                              break;
                          case rust::WorkflowOp::InferenceStartTimer:
                              elapsed.start();
                              break;
                          case rust::WorkflowOp::InferenceStart:
                              request.start_async();
                              break;
                          case rust::WorkflowOp::InferenceReady:
                              out[0] = request.wait_for(std::chrono::milliseconds(int(a[0])));
                              break;
                          case rust::WorkflowOp::InferenceElapsedMs:
                              out[0] = double(elapsed.elapsed());
                              break;
                          case rust::WorkflowOp::InferenceTimeout:
                              fail(name + "推理超过 10 秒，自动任务已停止");
                              break;
                          case rust::WorkflowOp::InferenceCancel:
                              // Task cancellation already releases device input.
                              // Let an ordinary CPU inference finish so its
                              // dynamic-shape graph is complete before reuse.
                              if (elapsed.elapsed() > 10000) {
                                  aborted = true;
                                  request.cancel();
                              }
                              break;
                          case rust::WorkflowOp::InferenceDrain:
                              while (!aborted && !request.wait_for(std::chrono::milliseconds(20))) {
                                  if (elapsed.elapsed() > 10000) {
                                      aborted = true;
                                      request.cancel();
                                  }
                              }
                              request.wait();
                              break;
                          default:
                              fail("Rust 推理等待宿主无效");
                          }
                      });
    } catch (...) {
        if (aborted) {
            request = request.get_compiled_model().create_infer_request();
        }
        throw;
    }
}
} // namespace okww::runtime
