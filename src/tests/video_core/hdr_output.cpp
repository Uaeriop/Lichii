// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "video_core/hdr_output.h"

using namespace VideoCore::HdrOutput;
using Settings::HdrOutput;

TEST_CASE("HDR output off never reports or renders HDR", "[video_core][hdr_output]") {
    STATIC_REQUIRE_FALSE(ReportsHdr(HdrOutput::Off, false));
    STATIC_REQUIRE_FALSE(ReportsHdr(HdrOutput::Off, true));
    STATIC_REQUIRE_FALSE(WantsHdrTarget(HdrOutput::Off, true, false));
    // A game that was told HDR before the setting was turned off still presents HDR images.
    STATIC_REQUIRE_FALSE(WantsHdrTarget(HdrOutput::Off, true, true));
}

TEST_CASE("Automatic HDR output follows the host display and the game", "[video_core][hdr_output]") {
    STATIC_REQUIRE(ReportsHdr(HdrOutput::Automatic, true));
    STATIC_REQUIRE_FALSE(ReportsHdr(HdrOutput::Automatic, false));
    // Games without HDR keep the SDR swapchain.
    STATIC_REQUIRE_FALSE(WantsHdrTarget(HdrOutput::Automatic, true, false));
    STATIC_REQUIRE(WantsHdrTarget(HdrOutput::Automatic, true, true));
    STATIC_REQUIRE_FALSE(WantsHdrTarget(HdrOutput::Automatic, false, true));
}

TEST_CASE("HDR output on reports HDR on any display", "[video_core][hdr_output]") {
    STATIC_REQUIRE(ReportsHdr(HdrOutput::On, false));
    STATIC_REQUIRE(ReportsHdr(HdrOutput::On, true));
    STATIC_REQUIRE(WantsHdrTarget(HdrOutput::On, true, false));
    STATIC_REQUIRE(WantsHdrTarget(HdrOutput::On, true, true));
    // Without an HDR10 surface format the frames stay SDR and HDR images are tonemapped.
    STATIC_REQUIRE_FALSE(WantsHdrTarget(HdrOutput::On, false, true));
}

TEST_CASE("Layers are converted only when their signal is not the target's",
          "[video_core][hdr_output]") {
    STATIC_REQUIRE(ConversionFor(false, false) == Conversion::None);
    STATIC_REQUIRE(ConversionFor(true, true) == Conversion::None);
    STATIC_REQUIRE(ConversionFor(false, true) == Conversion::SrgbToPq);
    STATIC_REQUIRE(ConversionFor(true, false) == Conversion::PqToSrgb);
    // hdr_convert.frag's modes
    STATIC_REQUIRE(static_cast<u32>(Conversion::SrgbToPq) == 1);
    STATIC_REQUIRE(static_cast<u32>(Conversion::PqToSrgb) == 2);
}

TEST_CASE("PQ encodes luminance as ST 2084 does", "[video_core][hdr_output]") {
    using Catch::Approx;
    CHECK(PqEncode(0.0) == Approx(0.0).margin(1e-6));
    CHECK(PqEncode(10000.0) == Approx(1.0));
    // Reference points of the curve: 100 nits is about 51%, 1000 nits about 75% of the signal.
    CHECK(PqEncode(100.0) == Approx(0.5081).margin(5e-4));
    CHECK(PqEncode(203.0) == Approx(0.5807).margin(5e-4));
    CHECK(PqEncode(1000.0) == Approx(0.7518).margin(5e-4));
    for (const double nits : {0.5, 80.0, 203.0, 1000.0, 4000.0}) {
        CHECK(PqDecode(PqEncode(nits)) == Approx(nits).epsilon(1e-6));
    }
}

TEST_CASE("The SDR tonemap keeps midtones and never passes white", "[video_core][hdr_output]") {
    using Catch::Approx;
    const double peak = 1000.0 / 200.0;
    CHECK(TonemapToSdr(0.0, peak) == 0.0);
    CHECK(TonemapToSdr(0.5, peak) == 0.5);
    CHECK(TonemapToSdr(TonemapKnee, peak) == TonemapKnee);
    CHECK(TonemapToSdr(peak, peak) == Approx(1.0));
    CHECK(TonemapToSdr(peak * 4.0, peak) == Approx(1.0));
    double last = 0.0;
    for (double value = 0.0; value <= peak; value += 0.05) {
        const double mapped = TonemapToSdr(value, peak);
        CHECK(mapped >= last);
        CHECK(mapped <= 1.0);
        last = mapped;
    }
    // An SDR-range signal (peak at paper white) is only clipped.
    CHECK(TonemapToSdr(0.9, 1.0) == 0.9);
    CHECK(TonemapToSdr(1.5, 1.0) == 1.0);
}
