// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>

#include "common/common_types.h"

namespace Service {

/// What nn::vi2::QueryDisplayHdrStatus (vi2 104) answers: u64 output format, u16 peak luminance
/// in nits. The sdk takes format 1 as SDR and 2 as HDR and fails the query on any other value.
struct Nx2HdrStatus {
    static constexpr u32 FormatSdr = 1;
    static constexpr u32 FormatHdr = 2;
    static constexpr u32 SdrPeakNits = 400;     ///< a typical SDR panel's peak
    static constexpr u32 DefaultHdrPeakNits = 1000;
    static constexpr u32 DefaultPaperWhiteNits = 203; ///< BT.2408 reference white

    u32 format{FormatSdr};
    u32 peak_nits{SdrPeakNits};

    /// The reply's words.
    [[nodiscard]] constexpr std::array<u32, 3> Words() const {
        return {format, 0, peak_nits};
    }
    [[nodiscard]] constexpr bool IsHdr() const {
        return format != FormatSdr;
    }
};

/// EXPERIMENT (EDEN_NX2_REPORT_HDR): the status to report. `report` 0 (the default) is SDR,
/// exactly what every build before answered; 1 is HDR; any higher value is sent as the raw
/// format, to probe what the sdk does with it. `peak_nits` 0 takes the default HDR peak; the
/// reply holds it as a u16 and a display below 80 nits is no HDR display.
[[nodiscard]] constexpr Nx2HdrStatus Nx2HdrStatusFor(u32 report, u32 peak_nits) {
    if (report == 0) {
        return {};
    }
    return {
        .format = report == 1 ? Nx2HdrStatus::FormatHdr : report,
        .peak_nits = std::clamp<u32>(peak_nits == 0 ? Nx2HdrStatus::DefaultHdrPeakNits : peak_nits,
                                     80, 10000),
    };
}

/// The paper white vi2 105 answers, a u16 in nits: `nits` 0 keeps the default.
[[nodiscard]] constexpr u32 Nx2HdrPaperWhiteFor(u32 nits) {
    return nits == 0 ? Nx2HdrStatus::DefaultPaperWhiteNits : std::clamp<u32>(nits, 80, 1000);
}

/// Whether the `count`th call of a query that games repeat every frame gets a log line: the
/// first, then each power of ten.
[[nodiscard]] constexpr bool Nx2LogsQuery(u64 count) {
    if (count == 0) {
        return false;
    }
    while (count % 10 == 0) {
        count /= 10;
    }
    return count == 1;
}

} // namespace Service
