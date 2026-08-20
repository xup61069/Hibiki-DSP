// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/control_service.hpp"

namespace hibiki {

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
    response = make_ack_frame_v1(request);
    return true;
}

}  // namespace hibiki
