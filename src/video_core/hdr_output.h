// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "common/common_types.h"
#include "common/settings_enums.h"

// HDR10 output (the "HDR output" setting). A Switch 2 game that is told the display is HDR (vi2
// 104, core/hle/service/nx2_hdr_status.h) presents a 10-bit image that holds an HDR10 signal:
// BT.2020 primaries, SMPTE ST 2084 (PQ) transfer. The Legend of Zelda: Ocarina of Time does
// (window image format word 4). With an HDR10 swapchain that image is shown as it is; everything
// else is converted on the way (present/hdr_convert.h).
namespace VideoCore::HdrOutput {

/// The VkFormat of the surface's HDR10 (ST 2084) format, 0 while it offers none: without
/// VK_EXT_swapchain_colorspace, or while HDR is off in the system. Written by the swapchain each
/// time it is created, read by the renderer and by vi2 104.
inline std::atomic<u32> g_host_hdr10_format{0};

/// Whether the swapchain shows HDR10 now (for logs and the status bar).
inline std::atomic<bool> g_host_hdr10_active{false};

/// Whether games are told that the display is HDR. Automatic follows the host display, so a game
/// never sends a signal that could only be shown tonemapped; On reports HDR on any display.
[[nodiscard]] constexpr bool ReportsHdr(Settings::HdrOutput mode, bool host_hdr10) {
    switch (mode) {
    case Settings::HdrOutput::Off:
        return false;
    case Settings::HdrOutput::Automatic:
        return host_hdr10;
    case Settings::HdrOutput::On:
        return true;
    }
    return false;
}

/// Whether a frame is rendered for an HDR10 swapchain. Automatic keeps the SDR swapchain until a
/// game presents an HDR image, so games without HDR are shown exactly as with the setting off.
[[nodiscard]] constexpr bool WantsHdrTarget(Settings::HdrOutput mode, bool host_hdr10,
                                            bool any_hdr_layer) {
    if (!host_hdr10) {
        return false;
    }
    switch (mode) {
    case Settings::HdrOutput::Off:
        return false;
    case Settings::HdrOutput::Automatic:
        return any_hdr_layer;
    case Settings::HdrOutput::On:
        return true;
    }
    return false;
}

/// What a layer needs before it is drawn to its target.
enum class Conversion : u32 {
    None,     ///< the layer's signal is the target's
    SrgbToPq, ///< an SDR layer on an HDR10 target: sRGB at paper white, encoded as PQ
    PqToSrgb, ///< an HDR layer on an SDR target (screenshots, On without an HDR display)
};

[[nodiscard]] constexpr Conversion ConversionFor(bool layer_is_pq, bool target_is_hdr) {
    if (layer_is_pq == target_is_hdr) {
        return Conversion::None;
    }
    return layer_is_pq ? Conversion::PqToSrgb : Conversion::SrgbToPq;
}

// SMPTE ST 2084. The shader (host_shaders/hdr_convert.frag) holds the same constants.
inline constexpr double PqM1 = 2610.0 / 16384.0;
inline constexpr double PqM2 = 2523.0 / 4096.0 * 128.0;
inline constexpr double PqC1 = 3424.0 / 4096.0;
inline constexpr double PqC2 = 2413.0 / 4096.0 * 32.0;
inline constexpr double PqC3 = 2392.0 / 4096.0 * 32.0;
inline constexpr double PqPeakNits = 10000.0;

/// The PQ signal (0..1) of a luminance in nits.
[[nodiscard]] inline double PqEncode(double nits) {
    const double y = std::pow(std::clamp(nits / PqPeakNits, 0.0, 1.0), PqM1);
    return std::pow((PqC1 + PqC2 * y) / (1.0 + PqC3 * y), PqM2);
}

/// The luminance in nits of a PQ signal (0..1).
[[nodiscard]] inline double PqDecode(double signal) {
    const double p = std::pow(std::clamp(signal, 0.0, 1.0), 1.0 / PqM2);
    return PqPeakNits * std::pow((std::max)(p - PqC1, 0.0) / (PqC2 - PqC3 * p), 1.0 / PqM1);
}

/// The tonemap PqToSrgb applies to luminance, in units of paper white: the identity up to
/// `Knee`, then a curve that reaches 1 (SDR white) at `peak` (the HDR peak over paper white) and
/// never passes it. HDR highlights keep their order instead of clipping.
inline constexpr double TonemapKnee = 0.75;
[[nodiscard]] inline double TonemapToSdr(double value, double peak) {
    if (value <= TonemapKnee || peak <= 1.0) {
        return (std::min)(value, 1.0);
    }
    const double span = (std::max)(peak - TonemapKnee, 1e-6);
    const double t = (std::min)((value - TonemapKnee) / span, 1.0);
    // 1 - (1 - t)^2: slope 2 * (1 - Knee) / span at the knee, flat at the peak.
    return TonemapKnee + (1.0 - TonemapKnee) * (1.0 - (1.0 - t) * (1.0 - t));
}

} // namespace VideoCore::HdrOutput
