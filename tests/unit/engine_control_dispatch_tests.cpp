// SPDX-License-Identifier: Apache-2.0

// Issue #1866: dedicated unit tests for the EngineControlWorkerV1::consume()
// dispatch contract. Every fail-closed branch (handler unset, reply-type
// rejection, idle graph commit) and every Ignored passthrough must be asserted
// with its exact EngineControlResultV1 value so the control worker cannot
// silently regress to accepting commands it must reject.

#include "hibiki/audio_engine.hpp"
#include "hibiki/control_payloads.hpp"
#include "hibiki/engine_control.hpp"

#include <cstdio>

#define CHECK(condition)                                                                     \
    do {                                                                                      \
        if (!(condition)) {                                                                   \
            std::fprintf(stderr, "engine control dispatch check failed: %s (%s:%d)\n",       \
                         #condition, __FILE__, __LINE__);                                      \
            return 1;                                                                          \
        }                                                                                      \
    } while (false)

namespace {

using hibiki::AudioEngineModel;
using hibiki::ControlCommandV1;
using hibiki::EngineControlResultV1;
using hibiki::EngineControlWorkerV1;
using hibiki::IpcMessageType;

bool accept_any_ir_prepare(const hibiki::IrPrepareCommandV1& /*request*/,
                           void* /*context*/) noexcept {
    return true;
}

bool accept_any_device_switch(const hibiki::DeviceSwitchPayloadV1& /*request*/,
                              void* /*context*/) noexcept {
    return true;
}

bool accept_any_session_volume(const hibiki::SessionVolumeCommandV1& /*request*/,
                               void* /*context*/) noexcept {
    return true;
}

bool accept_any_session_route(const hibiki::SessionRouteCommandV1& /*request*/,
                              void* /*context*/) noexcept {
    return true;
}

bool accept_any_session_route_rule(const hibiki::SessionRouteRuleCommandV1& /*request*/,
                                   void* /*context*/) noexcept {
    return true;
}

}  // namespace

int main() {
    using namespace hibiki;

    AudioEngineModel engine;
    engine.set_sample_rate(48000U);

    // Group 1: handler-unset fail-closed. A worker with no handler installed
    // must answer Failed for each delegated command type instead of crashing
    // or pretending the command succeeded.
    {
        EngineControlWorkerV1 worker(engine);
        ControlCommandV1 command{};

        command.type = IpcMessageType::IrPrepareCommand;
        CHECK(worker.consume(command) == EngineControlResultV1::Failed);
        CHECK(worker.consume(command) == EngineControlResultV1::Failed);

        command.type = IpcMessageType::DeviceSwitch;
        CHECK(worker.consume(command) == EngineControlResultV1::Failed);

        command.type = IpcMessageType::SessionVolumeCommand;
        CHECK(worker.consume(command) == EngineControlResultV1::Failed);

        command.type = IpcMessageType::SessionRouteCommand;
        CHECK(worker.consume(command) == EngineControlResultV1::Failed);

        command.type = IpcMessageType::SessionRouteRuleCommand;
        CHECK(worker.consume(command) == EngineControlResultV1::Failed);
    }

    // Group 2: Ignored passthrough. The worker never answers service-layer
    // requests or handshakes; these must be reported as Ignored so callers can
    // distinguish them from real work.
    {
        EngineControlWorkerV1 worker(engine);
        ControlCommandV1 command{};

        command.type = IpcMessageType::Hello;
        CHECK(worker.consume(command) == EngineControlResultV1::Ignored);

        command.type = IpcMessageType::DeviceCatalogRequest;
        CHECK(worker.consume(command) == EngineControlResultV1::Ignored);

        command.type = IpcMessageType::ControlStatusRequest;
        CHECK(worker.consume(command) == EngineControlResultV1::Ignored);

        command.type = IpcMessageType::SessionCatalogRequest;
        CHECK(worker.consume(command) == EngineControlResultV1::Ignored);
    }

    // Group 3: graph transaction boundary. GraphCommit without a pending
    // prepare is a Failed no-op; GraphRollback is an idempotent Applied
    // rollback even when there is nothing to roll back.
    {
        EngineControlWorkerV1 worker(engine);
        ControlCommandV1 commit{};
        commit.type = IpcMessageType::GraphCommit;
        CHECK(worker.consume(commit) == EngineControlResultV1::Failed);

        ControlCommandV1 rollback{};
        rollback.type = IpcMessageType::GraphRollback;
        CHECK(worker.consume(rollback) == EngineControlResultV1::Applied);
        // Rollback stays idempotent after the first call.
        CHECK(worker.consume(rollback) == EngineControlResultV1::Applied);
        // Commit still fails after a no-op rollback.
        CHECK(worker.consume(commit) == EngineControlResultV1::Failed);
    }

    // Group 4: reply and prepare types are Invalid at the worker layer. These
    // frames belong to the service or pipe layer; forwarding them into the
    // engine would bypass the request/response contract.
    {
        EngineControlWorkerV1 worker(engine);
        ControlCommandV1 command{};

        command.type = IpcMessageType::GraphPrepare;
        CHECK(worker.consume(command) == EngineControlResultV1::Invalid);

        command.type = IpcMessageType::Ack;
        CHECK(worker.consume(command) == EngineControlResultV1::Invalid);

        command.type = IpcMessageType::Error;
        CHECK(worker.consume(command) == EngineControlResultV1::Invalid);

        command.type = IpcMessageType::ControlStatusSnapshot;
        CHECK(worker.consume(command) == EngineControlResultV1::Invalid);

        command.type = IpcMessageType::SessionCatalogSnapshot;
        CHECK(worker.consume(command) == EngineControlResultV1::Invalid);
    }

    // Group 5: handler-set success path. Installing each handler flips its
    // command from Failed to Applied, proving the null check above is the only
    // gate between unset and accepted.
    {
        EngineControlWorkerV1 worker(engine);
        int marker = 0;
        worker.set_ir_prepare_handler(accept_any_ir_prepare, &marker);
        worker.set_device_switch_handler(accept_any_device_switch, &marker);
        worker.set_session_volume_handler(accept_any_session_volume, &marker);
        worker.set_session_route_handler(accept_any_session_route, &marker);
        worker.set_session_route_rule_handler(accept_any_session_route_rule, &marker);

        ControlCommandV1 command{};

        command.type = IpcMessageType::IrPrepareCommand;
        CHECK(worker.consume(command) == EngineControlResultV1::Applied);

        command.type = IpcMessageType::DeviceSwitch;
        CHECK(worker.consume(command) == EngineControlResultV1::Applied);

        command.type = IpcMessageType::SessionVolumeCommand;
        CHECK(worker.consume(command) == EngineControlResultV1::Applied);

        command.type = IpcMessageType::SessionRouteCommand;
        CHECK(worker.consume(command) == EngineControlResultV1::Applied);

        command.type = IpcMessageType::SessionRouteRuleCommand;
        CHECK(worker.consume(command) == EngineControlResultV1::Applied);
    }

    return 0;
}

#undef CHECK
