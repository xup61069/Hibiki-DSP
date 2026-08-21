// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/control_service.hpp"

namespace hibiki {

bool ControlPlaneHostV1::start(
    const IpcNamedPipeConfigV1& config,
    const ControlCommandSinkV1 sink,
    void* const sink_context,
    DeviceCatalogSnapshotStoreV1* const snapshot_store) noexcept {
    stop();
    if (sink == nullptr) return false;
    context_.sink = sink;
    context_.sink_context = sink_context;
    context_.snapshot_reply = snapshot_store == nullptr
                                  ? nullptr
                                  : device_catalog_snapshot_reply_v1;
    context_.snapshot_context = snapshot_store;
    if (!pipe_.start(config, handle_control_frame_v1, &context_)) {
        context_ = {};
        return false;
    }
    return true;
}

bool ControlPlaneHostV1::start_with_queue(
    const IpcNamedPipeConfigV1& config,
    DeviceCatalogSnapshotStoreV1* const snapshot_store) noexcept {
    return start(config, enqueue_control_command_v1, &queue_, snapshot_store);
}

void ControlPlaneHostV1::stop() noexcept {
    pipe_.stop();
    context_ = {};
}

bool handle_control_frame_v1(const IpcFrameV1& request,
                             IpcFrameV1& response,
                             void* const context) noexcept {
    auto* handler = static_cast<ControlPlaneHandlerContextV1*>(context);
    ControlCommandV1 command{};
    if (handler == nullptr || handler->sink == nullptr ||
        !decode_control_command_v1(request, command) ||
        !handler->sink(command, handler->sink_context)) {
        response = make_error_frame_v1(request);
        return true;
    }
    if (request.header.type == IpcMessageType::DeviceCatalogRequest) {
        response = {};
        if (handler->snapshot_reply == nullptr ||
            !handler->snapshot_reply(response, handler->snapshot_context) ||
            response.header.type != IpcMessageType::DeviceCatalogSnapshot) {
            response = make_error_frame_v1(request);
        } else {
            response.header.request_id = request.header.request_id;
        }
        return true;
    }
    response = make_ack_frame_v1(request);
    return true;
}

}  // namespace hibiki
