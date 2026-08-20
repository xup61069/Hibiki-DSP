#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/control_payloads.hpp"

namespace hibiki {

using ControlCommandSinkV1 = bool (*)(const ControlCommandV1& command,
                                      void* context) noexcept;

struct ControlPlaneHandlerContextV1 {
    ControlCommandSinkV1 sink{nullptr};
    void* sink_context{nullptr};
};

// Adapter suitable for IpcNamedPipeServerV1. It validates the typed command
// first, then delegates to a control-plane queue/sink supplied by the host.
// The sink must enqueue or otherwise hand off work; it must not run DSP on
// this pipe thread and must not throw.
[[nodiscard]] bool handle_control_frame_v1(const IpcFrameV1& request,
                                           IpcFrameV1& response,
                                           void* context) noexcept;

}  // namespace hibiki
