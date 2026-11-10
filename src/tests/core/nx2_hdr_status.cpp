// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "core/hle/service/nx2_hdr_status.h"

using Service::Nx2HdrPaperWhiteFor;
using Service::Nx2HdrStatus;
using Service::Nx2HdrStatusFor;
using Service::Nx2LogsQuery;

TEST_CASE("NX2 HDR status reports SDR unless asked", "[core][nx2_hdr_status]") {
    // What vi2 104 answered before the switch existed (Kirby Air Riders showed black on other
    // replies).
    STATIC_REQUIRE(Nx2HdrStatusFor(0, 0).Words() == std::array<u32, 3>{1, 0, 400});
    STATIC_REQUIRE(Nx2HdrStatusFor(0, 1500).Words() == std::array<u32, 3>{1, 0, 400});
    STATIC_REQUIRE_FALSE(Nx2HdrStatusFor(0, 0).IsHdr());
}

TEST_CASE("NX2 HDR status reports HDR with a peak", "[core][nx2_hdr_status]") {
    STATIC_REQUIRE(Nx2HdrStatusFor(1, 0).Words() == std::array<u32, 3>{2, 0, 1000});
    STATIC_REQUIRE(Nx2HdrStatusFor(1, 800).Words() == std::array<u32, 3>{2, 0, 800});
    STATIC_REQUIRE(Nx2HdrStatusFor(1, 20).peak_nits == 80);
    STATIC_REQUIRE(Nx2HdrStatusFor(1, 70000).peak_nits == 10000);
    STATIC_REQUIRE(Nx2HdrStatusFor(1, 0).IsHdr());
    // Raw formats for probing the sdk.
    STATIC_REQUIRE(Nx2HdrStatusFor(2, 0).format == 2);
    STATIC_REQUIRE(Nx2HdrStatusFor(3, 0).format == 3);
}

TEST_CASE("NX2 HDR paper white keeps the reference white by default", "[core][nx2_hdr_status]") {
    STATIC_REQUIRE(Nx2HdrPaperWhiteFor(0) == 203);
    STATIC_REQUIRE(Nx2HdrPaperWhiteFor(250) == 250);
    STATIC_REQUIRE(Nx2HdrPaperWhiteFor(5) == 80);
    STATIC_REQUIRE(Nx2HdrPaperWhiteFor(4000) == 1000);
}

TEST_CASE("NX2 repeated queries are logged at powers of ten", "[core][nx2_hdr_status]") {
    STATIC_REQUIRE_FALSE(Nx2LogsQuery(0));
    STATIC_REQUIRE(Nx2LogsQuery(1));
    STATIC_REQUIRE_FALSE(Nx2LogsQuery(2));
    STATIC_REQUIRE(Nx2LogsQuery(10));
    STATIC_REQUIRE_FALSE(Nx2LogsQuery(11));
    STATIC_REQUIRE(Nx2LogsQuery(1000));
    STATIC_REQUIRE_FALSE(Nx2LogsQuery(5000));
}
