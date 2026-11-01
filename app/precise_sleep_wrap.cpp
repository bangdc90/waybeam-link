// SPDX-License-Identifier: GPL-2.0-or-later
// nanosleep wrapper for wblink/precise_sleep.h, linked into waybeam-link only
// with -Wl,--wrap=<symbol> (CMakeLists.txt). musl on 32-bit ARM redirects
// nanosleep to __nanosleep_time64; glibc keeps the plain name. A vendored
// library cannot be patched (third_party/ is read-only), so its sleeps are
// intercepted at link time instead.
#include <time.h>

#include "wblink/precise_sleep.h"

namespace {

bool coarse_tick() {
    static const bool coarse = [] {
        timespec r{};
        return clock_getres(CLOCK_MONOTONIC, &r) == 0 &&
               (r.tv_sec > 0 || r.tv_nsec > 1'000'000);
    }();
    return coarse;
}

int64_t mono_ns() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<int64_t>(t.tv_sec) * 1'000'000'000 + t.tv_nsec;
}

// True when the request was served by spinning.
bool spin(const timespec* req) {
    if (req == nullptr || req->tv_sec != 0 || req->tv_nsec < 0 ||
        req->tv_nsec > wblink::kPreciseSleepMaxNs ||
        wblink::g_precise_sleep_depth.load(std::memory_order_relaxed) == 0 ||
        !coarse_tick()) {
        return false;
    }
    const int64_t end = mono_ns() + req->tv_nsec;
    while (mono_ns() < end) {
    }
    return true;
}

}  // namespace

#if defined(_REDIR_TIME64) && _REDIR_TIME64
extern "C" int __real___nanosleep_time64(const timespec* req, timespec* rem);
extern "C" int __wrap___nanosleep_time64(const timespec* req, timespec* rem) {
    return spin(req) ? 0 : __real___nanosleep_time64(req, rem);
}
#else
extern "C" int __real_nanosleep(const timespec* req, timespec* rem);
extern "C" int __wrap_nanosleep(const timespec* req, timespec* rem) {
    return spin(req) ? 0 : __real_nanosleep(req, rem);
}
#endif
