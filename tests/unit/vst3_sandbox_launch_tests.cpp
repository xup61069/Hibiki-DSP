// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_sandbox.hpp"
#include "hibiki/vst3_worker_lane.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {

using namespace hibiki;

constexpr double kQuietNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kInfinity = std::numeric_limits<double>::infinity();

Vst3SandboxLaunchV1 make_passthrough_launch() {
    Vst3SandboxLaunchV1 launch{};
    launch.worker_executable = L"worker.exe";
    launch.plugin_path = L"plugin.vst3";
    launch.watchdog_timeout_ms = 250U;
    launch.start_time_ms = 1U;
    launch.worker_pipe_name = L"pipe";
    launch.worker_pipe_timeout_ms = 1000U;
    return launch;
}

}  // namespace

int main() {
    // ---- required identity: worker executable and plugin path --------------
    {
        auto empty_worker = make_passthrough_launch();
        empty_worker.worker_executable.clear();
        CHECK(!validate_vst3_sandbox_launch_v1(empty_worker));

        auto empty_plugin = make_passthrough_launch();
        empty_plugin.plugin_path.clear();
        CHECK(!validate_vst3_sandbox_launch_v1(empty_plugin));
    }

    // ---- watchdog bounds: (0, 5000] ms --------------------------------------
    {
        auto zero_watchdog = make_passthrough_launch();
        zero_watchdog.watchdog_timeout_ms = 0U;
        CHECK(!validate_vst3_sandbox_launch_v1(zero_watchdog));

        auto min_watchdog = make_passthrough_launch();
        min_watchdog.watchdog_timeout_ms = 1U;
        CHECK(validate_vst3_sandbox_launch_v1(min_watchdog));

        auto max_watchdog = make_passthrough_launch();
        max_watchdog.watchdog_timeout_ms = 5000U;
        CHECK(validate_vst3_sandbox_launch_v1(max_watchdog));

        auto over_watchdog = make_passthrough_launch();
        over_watchdog.watchdog_timeout_ms = 5001U;
        CHECK(!validate_vst3_sandbox_launch_v1(over_watchdog));
    }

    // ---- optional worker pipe timeout only applies with a pipe name ---------
    {
        auto no_pipe = make_passthrough_launch();
        no_pipe.worker_pipe_name.clear();
        no_pipe.worker_pipe_timeout_ms = 0U;
        CHECK(validate_vst3_sandbox_launch_v1(no_pipe));

        auto named_pipe_zero_timeout = make_passthrough_launch();
        named_pipe_zero_timeout.worker_pipe_timeout_ms = 0U;
        CHECK(!validate_vst3_sandbox_launch_v1(named_pipe_zero_timeout));
    }

    // ---- passthrough mode: empty class UID skips SDK audio validation -------
    {
        Vst3SandboxLaunchV1 passthrough{};
        passthrough.worker_executable = L"worker.exe";
        passthrough.plugin_path = L"plugin.vst3";
        passthrough.watchdog_timeout_ms = 250U;
        passthrough.vst3_sample_rate = 0.0;
        passthrough.vst3_channels = 0U;
        CHECK(validate_vst3_sandbox_launch_v1(passthrough));
    }

    // ---- SDK mode: finite sample rate inside [8000, 384000] Hz --------------
    {
        auto nan_rate = make_passthrough_launch();
        nan_rate.vst3_class_id = L"uid";
        nan_rate.vst3_sample_rate = kQuietNan;
        nan_rate.vst3_channels = 2U;
        CHECK(!validate_vst3_sandbox_launch_v1(nan_rate));

        auto inf_rate = make_passthrough_launch();
        inf_rate.vst3_class_id = L"uid";
        inf_rate.vst3_sample_rate = kInfinity;
        inf_rate.vst3_channels = 2U;
        CHECK(!validate_vst3_sandbox_launch_v1(inf_rate));

        auto slowest = make_passthrough_launch();
        slowest.vst3_class_id = L"uid";
        slowest.vst3_sample_rate = 8000.0;
        slowest.vst3_channels = 2U;
        CHECK(validate_vst3_sandbox_launch_v1(slowest));

        auto fastest = make_passthrough_launch();
        fastest.vst3_class_id = L"uid";
        fastest.vst3_sample_rate = 384000.0;
        fastest.vst3_channels = 2U;
        CHECK(validate_vst3_sandbox_launch_v1(fastest));

        auto too_slow = make_passthrough_launch();
        too_slow.vst3_class_id = L"uid";
        too_slow.vst3_sample_rate = 7999.0;
        too_slow.vst3_channels = 2U;
        CHECK(!validate_vst3_sandbox_launch_v1(too_slow));

        auto too_fast = make_passthrough_launch();
        too_fast.vst3_class_id = L"uid";
        too_fast.vst3_sample_rate = 384001.0;
        too_fast.vst3_channels = 2U;
        CHECK(!validate_vst3_sandbox_launch_v1(too_fast));
    }

    // ---- SDK mode: channel count must be exactly 2/6/8 ----------------------
    for (const std::uint32_t valid_channels : {2U, 6U, 8U}) {
        Vst3SandboxLaunchV1 launch = make_passthrough_launch();
        launch.vst3_class_id = L"uid";
        launch.vst3_sample_rate = 48000.0;
        launch.vst3_channels = valid_channels;
        CHECK(validate_vst3_sandbox_launch_v1(launch));
    }
    for (const std::uint32_t invalid_channels : {0U, 1U, 3U, 4U, 5U, 7U, 9U}) {
        Vst3SandboxLaunchV1 launch = make_passthrough_launch();
        launch.vst3_class_id = L"uid";
        launch.vst3_sample_rate = 48000.0;
        launch.vst3_channels = invalid_channels;
        CHECK(!validate_vst3_sandbox_launch_v1(launch));
    }

    // ---- worker block timeline must not wrap -------------------------------
    {
        constexpr std::uint64_t kMax = (std::numeric_limits<std::uint64_t>::max)();
        CHECK(vst3_worker_block_range_fits_v1(kMax - 1U, 1U));
        CHECK(vst3_worker_block_range_fits_v1(kMax - 4095U, 4095U));
        CHECK(!vst3_worker_block_range_fits_v1(kMax - 4095U, 4096U));
        CHECK(!vst3_worker_block_range_fits_v1(kMax, 1U));
        CHECK(!vst3_worker_block_range_fits_v1(0U, 0U));
    }

    return 0;
}
