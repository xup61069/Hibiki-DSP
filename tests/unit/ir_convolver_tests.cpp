// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/ir_convolver.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fputs("FAILED: " #expr "\n", stderr); \
            return 1; \
        } \
    } while (false)

namespace {
using hibiki::IrConvolverV1;
using hibiki::IrPhaseResolutionV1;

IrPhaseResolutionV1 make_resolution(bool uses_fir = true,
                                     double delay_ms = 0.0) {
    IrPhaseResolutionV1 r{};
    r.schema_version = 1U;
    r.valid = true;
    r.uses_fir = uses_fir;
    r.added_delay_ms = delay_ms;
    return r;
}
}  // namespace

int main() {
    const auto qnan = std::numeric_limits<float>::quiet_NaN();
    const auto pinf = std::numeric_limits<float>::infinity();

    // ---- prepare() parameter rejection -------------------------------------
    {
        IrConvolverV1 c;
        const std::vector<float> kernel(4U, 0.1F);
        const auto valid = make_resolution();

        CHECK(!c.prepare(kernel, 0U, 1U, 2U, 48000U, valid));
        CHECK(!c.prepare(std::vector<float>(hibiki::kMaxRealtimeIrTapsV1 + 1U),
                         hibiki::kMaxRealtimeIrTapsV1 + 1U, 1U, 2U, 48000U,
                         valid));
        CHECK(!c.prepare(std::vector<float>(3U), 4U, 1U, 2U, 48000U, valid));
        CHECK(!c.prepare(kernel, 4U, 0U, 2U, 48000U, valid));
        CHECK(!c.prepare(kernel, 4U, 9U, 9U, 48000U, valid));
        CHECK(!c.prepare(kernel, 4U, 2U, 4U, 48000U, valid));
        CHECK(!c.prepare(kernel, 4U, 1U, 2U, 7999U, valid));
        CHECK(!c.prepare(kernel, 4U, 1U, 2U, 192001U, valid));
        {
            auto bad = make_resolution();
            bad.valid = false;
            CHECK(!c.prepare(kernel, 4U, 1U, 2U, 48000U, bad));
        }
        CHECK(!c.prepare(kernel, 4U, 1U, 2U, 48000U,
                         make_resolution(true, qnan)));
        CHECK(!c.prepare(kernel, 4U, 1U, 2U, 48000U,
                         make_resolution(true, -0.01)));
        CHECK(!c.prepare(std::vector<float>{0.5F, qnan}, 2U, 1U, 2U, 48000U,
                         valid));
        CHECK(!c.prepare(std::vector<float>{0.5F, pinf}, 2U, 1U, 2U, 48000U,
                         valid));
    }

    // ---- identity kernel: output equals input --------------------------------
    {
        IrConvolverV1 c;
        const std::vector<float> kernel{1.0F};
        CHECK(c.prepare(kernel, 1U, 1U, 2U, 48000U, make_resolution()));
        std::vector<float> buf{0.5F, -0.25F, 0.75F, -0.125F};
        const auto original = buf;
        CHECK(c.process_interleaved(buf.data(), 2U, 2U));
        for (std::size_t i = 0; i < buf.size(); ++i) {
            CHECK(buf[i] == original[i]);
        }
    }

    // ---- delay kernel ---------------------------------------------------------
    {
        IrConvolverV1 c;
        constexpr std::size_t kTaps = 4U;
        std::vector<float> kernel(kTaps, 0.0F);
        kernel[kTaps - 1U] = 1.0F;
        CHECK(c.prepare(kernel, kTaps, 1U, 1U, 48000U, make_resolution()));
        std::vector<float> buf{1.0F, 2.0F, 3.0F, 4.0F};
        CHECK(c.process_interleaved(buf.data(), 4U, 1U));
        CHECK(buf[0] == 0.0F);
        CHECK(buf[1] == 0.0F);
        CHECK(buf[2] == 0.0F);
        CHECK(buf[3] == 1.0F);
    }

    // ---- mono broadcast to multi-channel ---------------------------------------
    {
        IrConvolverV1 c;
        const std::vector<float> kernel{0.5F};
        CHECK(c.prepare(kernel, 1U, 1U, 2U, 48000U, make_resolution()));
        std::vector<float> buf{1.0F, 2.0F};
        CHECK(c.process_interleaved(buf.data(), 1U, 2U));
        CHECK(buf[0] == 0.5F);
        CHECK(buf[1] == 1.0F);
    }

    // ---- per-channel kernels: no cross-channel leakage ---------------------------
    {
        IrConvolverV1 c;
        // L: identity [1,0], R: delay-by-1 [0,1]
        const std::vector<float> kernel{1.0F, 0.0F, 0.0F, 1.0F};
        CHECK(c.prepare(kernel, 2U, 2U, 2U, 48000U, make_resolution()));

        std::vector<float> f0{1.0F, 0.0F};
        CHECK(c.process_interleaved(f0.data(), 1U, 2U));
        CHECK(f0[0] == 1.0F);
        CHECK(f0[1] == 0.0F);

        std::vector<float> f1{0.0F, 0.0F};
        CHECK(c.process_interleaved(f1.data(), 1U, 2U));
        CHECK(f1[0] == 0.0F);
        CHECK(f1[1] == 0.0F);

        std::vector<float> f2{0.0F, 1.0F};
        CHECK(c.process_interleaved(f2.data(), 1U, 2U));
        CHECK(f2[0] == 0.0F);
        CHECK(f2[1] == 0.0F);

        std::vector<float> f3{0.0F, 0.0F};
        CHECK(c.process_interleaved(f3.data(), 1U, 2U));
        CHECK(f3[0] == 0.0F);
        CHECK(f3[1] == 1.0F);
    }

    // ---- non-finite inputs become zero ---------------------------------------------
    {
        IrConvolverV1 c;
        const std::vector<float> kernel{1.0F};
        CHECK(c.prepare(kernel, 1U, 1U, 1U, 48000U, make_resolution()));
        std::vector<float> buf{qnan, pinf, 0.5F};
        CHECK(c.process_interleaved(buf.data(), 3U, 1U));
        CHECK(buf[0] == 0.0F);
        CHECK(buf[1] == 0.0F);
        CHECK(buf[2] == 0.5F);
    }

    // ---- reset() clears history ------------------------------------------------------
    {
        IrConvolverV1 ca;
        IrConvolverV1 cb;
        constexpr std::size_t kTaps = 4U;
        std::vector<float> kernel(kTaps, 0.0F);
        kernel[kTaps - 1U] = 1.0F;
        CHECK(ca.prepare(kernel, kTaps, 1U, 1U, 48000U, make_resolution()));
        CHECK(cb.prepare(kernel, kTaps, 1U, 1U, 48000U, make_resolution()));
        std::vector<float> feed{1.0F, 2.0F, 0.0F, 0.0F};
        CHECK(ca.process_interleaved(feed.data(), 4U, 1U));
        ca.reset();
        std::vector<float> inA{7.0F};
        std::vector<float> inB{7.0F};
        CHECK(ca.process_interleaved(inA.data(), 1U, 1U));
        CHECK(cb.process_interleaved(inB.data(), 1U, 1U));
        CHECK(inA[0] == inB[0]);
    }

    // ---- status reflects prepare parameters -------------------------------------------
    {
        IrConvolverV1 c;
        const std::vector<float> kernel(8U, 0.25F);  // 4 taps x 2 ch
        CHECK(c.prepare(kernel, 4U, 2U, 2U, 44100U,
                        make_resolution(false, 12.5)));
        const auto& s = c.status();
        CHECK(s.schema_version == 1U);
        CHECK(s.valid);
        CHECK(s.sample_rate == 44100U);
        CHECK(s.channels == 2U);
        CHECK(s.kernel_channels == 2U);
        CHECK(s.taps == 4U);
        CHECK(s.declared_delay_ms == 12.5);
        CHECK(!s.uses_fir);
    }

    // ---- process_interleaved rejects invalid calls -------------------------------------
    {
        IrConvolverV1 unprepared;
        float dummy[2] = {0.0F, 0.0F};
        CHECK(!unprepared.process_interleaved(nullptr, 1U, 1U));
        CHECK(!unprepared.process_interleaved(dummy, 0U, 1U));
        CHECK(!unprepared.process_interleaved(dummy, 1U, 0U));
        CHECK(!unprepared.process_interleaved(dummy, 1U, 9U));
        CHECK(!unprepared.process_interleaved(dummy, 1U, 1U));

        IrConvolverV1 prepared;
        const std::vector<float> kernel{1.0F};
        CHECK(prepared.prepare(kernel, 1U, 1U, 2U, 48000U, make_resolution()));
        float stereo[2] = {0.5F, 0.5F};
        CHECK(!prepared.process_interleaved(stereo, 1U, 1U));

        constexpr auto max_frames_for_eight_channels =
            std::numeric_limits<std::size_t>::max() / 8U;
        CHECK(max_frames_for_eight_channels * 8U <=
              std::numeric_limits<std::size_t>::max());
        CHECK(max_frames_for_eight_channels + 1U >
              std::numeric_limits<std::size_t>::max() / 8U);
        auto baseline = std::make_unique<IrConvolverV1>();
        auto candidate = std::make_unique<IrConvolverV1>();
        CHECK(baseline->prepare(kernel, 1U, 1U, 8U, 48000U, make_resolution()));
        CHECK(candidate->prepare(kernel, 1U, 1U, 8U, 48000U, make_resolution()));
        std::vector<float> warmup{0.1F, -0.2F, 0.3F, -0.4F,
                                  0.5F, -0.6F, 0.7F, -0.8F};
        auto baseline_warmup = warmup;
        auto candidate_warmup = warmup;
        CHECK(baseline->process_interleaved(baseline_warmup.data(), 1U, 8U));
        CHECK(candidate->process_interleaved(candidate_warmup.data(), 1U, 8U));
        std::vector<float> overflow_guard{0.125F, -0.25F, 0.5F, -0.75F,
                                          0.875F, -1.0F, 0.25F, -0.5F};
        CHECK(!candidate->process_interleaved(
            overflow_guard.data(), max_frames_for_eight_channels + 1U, 8U));
        CHECK(overflow_guard[0] == 0.125F && overflow_guard[7] == -0.5F);

        std::vector<float> baseline_next{0.9F, -0.8F, 0.7F, -0.6F,
                                         0.5F, -0.4F, 0.3F, -0.2F};
        auto candidate_next = baseline_next;
        CHECK(baseline->process_interleaved(baseline_next.data(), 1U, 8U));
        CHECK(candidate->process_interleaved(candidate_next.data(), 1U, 8U));
        for (std::size_t index = 0U; index < baseline_next.size(); ++index) {
            CHECK(candidate_next[index] == baseline_next[index]);
        }
    }

    return 0;
}
