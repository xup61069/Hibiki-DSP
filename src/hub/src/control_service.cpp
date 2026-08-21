// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/control_service.hpp"

namespace hibiki {

bool ControlPlaneHostV1::start(
    const IpcNamedPipeConfigV1& config,
    const ControlCommandSinkV1 sink,
    void* const sink_context,
    DeviceCatalogSnapshotStoreV1* const snapshot_store,
    ControlStatusSnapshotStoreV1* const status_store,
    SessionCatalogSnapshotStoreV1* const session_catalog_store) noexcept {
    stop();
    if (sink == nullptr) return false;
    context_.sink = sink;
    context_.sink_context = sink_context;
    context_.snapshot_reply = snapshot_store == nullptr
                                  ? nullptr
                                  : device_catalog_snapshot_reply_v1;
    context_.snapshot_context = snapshot_store;
    context_.status_reply = status_store == nullptr
                                ? nullptr
                                : control_status_snapshot_reply_v1;
    context_.status_context = status_store;
    context_.session_catalog_reply = session_catalog_store == nullptr
                                         ? nullptr
                                         : session_catalog_snapshot_reply_v1;
    context_.session_catalog_context = session_catalog_store;
    if (!pipe_.start(config, handle_control_frame_v1, &context_)) {
        context_ = {};
        return false;
    }
    return true;
}

bool ControlPlaneHostV1::start_with_queue(
    const IpcNamedPipeConfigV1& config,
    DeviceCatalogSnapshotStoreV1* const snapshot_store,
    ControlStatusSnapshotStoreV1* const status_store,
    SessionCatalogSnapshotStoreV1* const session_catalog_store) noexcept {
    return start(config, enqueue_control_command_v1, &queue_, snapshot_store, status_store,
                 session_catalog_store);
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
    if (request.header.type == IpcMessageType::ControlStatusRequest) {
        response = {};
        if (handler->status_reply == nullptr ||
            !handler->status_reply(response, handler->status_context) ||
            response.header.type != IpcMessageType::ControlStatusSnapshot) {
            response = make_error_frame_v1(request);
        } else {
            response.header.request_id = request.header.request_id;
        }
        return true;
    }
    if (request.header.type == IpcMessageType::SessionCatalogRequest) {
        response = {};
        if (handler->session_catalog_reply == nullptr ||
            !handler->session_catalog_reply(response, handler->session_catalog_context) ||
            response.header.type != IpcMessageType::SessionCatalogSnapshot) {
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
