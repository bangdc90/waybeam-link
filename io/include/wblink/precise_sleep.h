// SPDX-License-Identifier: GPL-2.0-or-later
// Short sleeps during radio bring-up on a coarse-tick kernel.
//
// devourer waits ~1 µs after many PHY-table writes (Realtek's ODM_delay_us).
// On a kernel without high-resolution timers (GK7205 / Hi3536 OpenIPC
// builds: clock_getres = 10 ms) nanosleep rounds that up to a whole tick, so
// an RTL8812AU bring-up that takes ~1.7 s on a PC took 8.6 s on the camera —
// 7.7 s of it in the BB/RF tables (init.timing, 2026-11-01). While a
// PreciseSleepScope is alive, the nanosleep wrapper linked into the
// waybeam-link binary (app/precise_sleep_wrap.cpp) busy-waits sleeps of
// <= kPreciseSleepMaxNs instead. Outside a scope, on a fine-tick kernel, or
// in a binary without the wrapper, sleeps are untouched.
//
// The ceiling is the table delays (1..50 µs), NOT the millisecond waits of
// the power sequence: spinning those to their exact length (10 ms ceiling)
// made the next register read after power-on fail (rtw_read(0005) iostream
// error, 2 of 2 bring-ups) — the chip evidently relies on the tick-rounded
// 10-20 ms the vendor's "1 ms" has always been on these hosts.
#pragma once

#include <atomic>
#include <cstdint>

namespace wblink {

inline constexpr int64_t kPreciseSleepMaxNs = 200'000;

extern std::atomic<int> g_precise_sleep_depth;

struct PreciseSleepScope {
    PreciseSleepScope() { g_precise_sleep_depth.fetch_add(1, std::memory_order_relaxed); }
    ~PreciseSleepScope() { g_precise_sleep_depth.fetch_sub(1, std::memory_order_relaxed); }
    PreciseSleepScope(const PreciseSleepScope&) = delete;
    PreciseSleepScope& operator=(const PreciseSleepScope&) = delete;
};

}  // namespace wblink
