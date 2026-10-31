// SPDX-License-Identifier: GPL-2.0-or-later
// §3.16 (Pass 207) ClockSync: NTP offset/delay arithmetic, min-delay filter,
// staleness, and rejection of impossible timestamp sets.
#include "wblink/clock_sync.h"
#include "wbtest.h"

using namespace wblink;

int main() {
    // Craft clock = ground clock + 1'000'000 µs; 2 ms each way, 100 µs hold.
    {
        ClockSync cs;
        const uint64_t t1 = 5'000'000;
        const uint64_t t2 = t1 + 2'000 + 1'000'000;
        const uint64_t t3 = t2 + 100;
        const uint64_t t4 = t1 + 2'000 + 100 + 2'000;
        CHECK(cs.on_sample(t1, t2, t3, t4));
        const auto e = cs.estimate(t4);
        CHECK(e.has_value());
        if (e) {
            CHECK(e->offset_us == 1'000'000);
            CHECK(e->delay_us == 4'000);
            CHECK(e->samples == 1);
        }
    }
    // A craft clock BEHIND the ground's (negative offset) must not wrap.
    {
        ClockSync cs;
        const uint64_t t1 = 900'000'000;
        const uint64_t t2 = 1'000;  // craft booted long after the ground
        const uint64_t t3 = 1'050;
        const uint64_t t4 = t1 + 1'050;
        CHECK(cs.on_sample(t1, t2, t3, t4));
        const auto e = cs.estimate(t4);
        CHECK(e.has_value());
        if (e) CHECK(e->offset_us == static_cast<int64_t>(1'000) - 900'000'500);
    }
    // Queueing on one leg biases that sample; the filter keeps the
    // min-delay one (true offset 0, symmetric 1 ms legs).
    {
        ClockSync cs;
        uint64_t t = 10'000'000;
        CHECK(cs.on_sample(t, t + 1'000 + 20'000, t + 21'000, t + 22'000));
        t += 500'000;
        CHECK(cs.on_sample(t, t + 1'000, t + 1'000, t + 2'000));
        t += 500'000;
        CHECK(cs.on_sample(t, t + 1'000, t + 1'000, t + 30'000));
        const auto e = cs.estimate(t + 30'000);
        CHECK(e.has_value());
        if (e) {
            CHECK(e->offset_us == 0);
            CHECK(e->delay_us == 2'000);
            CHECK(e->samples == 3);
        }
    }
    // Stale after kStaleUs since the newest sample; reset clears.
    {
        ClockSync cs;
        CHECK(cs.on_sample(1'000, 2'000, 2'000, 3'000));
        CHECK(cs.estimate(3'000 + ClockSync::kStaleUs).has_value());
        CHECK(!cs.estimate(3'001 + ClockSync::kStaleUs).has_value());
        cs.reset();
        CHECK(!cs.estimate(3'000).has_value());
    }
    // Impossible sets fold nothing: reply before request, hold > round trip.
    {
        ClockSync cs;
        CHECK(!cs.on_sample(5'000, 1, 2, 4'000));
        CHECK(!cs.on_sample(5'000, 1, 100'000, 6'000));
        CHECK(!cs.on_sample(5'000, 10, 9, 6'000));
        CHECK(!cs.estimate(6'000).has_value());
    }
    // The window holds kWindow samples; older ones roll out.
    {
        ClockSync cs;
        uint64_t t = 1'000'000;
        CHECK(cs.on_sample(t, t + 100, t + 100, t + 200));  // delay 200, best
        for (size_t i = 0; i < ClockSync::kWindow; ++i) {
            t += 100'000;
            CHECK(cs.on_sample(t, t + 5'000, t + 5'000, t + 10'000));
        }
        const auto e = cs.estimate(t + 10'000);
        CHECK(e.has_value());
        if (e) {
            CHECK(e->delay_us == 10'000);
            CHECK(e->samples == ClockSync::kWindow);
        }
    }
    // §3.16 (Pass 208) TIME_BEACON: calibrate d0 while two-way is fresh,
    // then track a drifting offset one-way once two-way goes stale.
    {
        ClockSync cs;
        const int64_t off = -12'000'000'000;  // craft − ground
        const auto craft = [&](uint64_t g, int64_t drift) {
            return static_cast<uint64_t>(static_cast<int64_t>(g) + off + drift);
        };
        uint64_t g = 50'000'000'000;
        CHECK(!cs.d0_us().has_value());
        for (size_t i = 0; i < ClockSync::kWindow; ++i) {
            // Two-way: 1.5 ms each way, 50 µs hold. Beacon: 1.2 ms floor
            // with one 30 ms queued outlier the min filter must ignore.
            CHECK(cs.on_sample(g, craft(g + 1'500, 0), craft(g + 1'550, 0),
                               g + 3'050));
            const uint64_t q = i == 3 ? 30'000 : 1'200;
            cs.on_beacon(craft(g + 100'000, 0), g + 100'000 + q);
            g += 500'000;
        }
        CHECK(cs.d0_us().has_value());
        if (cs.d0_us()) CHECK(*cs.d0_us() == 1'200);
        auto e = cs.estimate(g);
        CHECK(e.has_value());
        if (e) {
            CHECK(!e->beacon);
            CHECK(e->offset_us == off);
        }
        // Ground mutes: no more two-way samples. The craft clock drifts
        // +3 ms; beacons alone must follow it once two-way is stale.
        g += ClockSync::kStaleUs + 1;
        for (size_t i = 0; i < ClockSync::kWindow; ++i) {
            cs.on_beacon(craft(g, 3'000), g + 1'200);
            g += 500'000;
        }
        e = cs.estimate(g);
        CHECK(e.has_value());
        if (e) {
            CHECK(e->beacon);
            CHECK(e->offset_us == off + 3'000);
            CHECK(e->samples == ClockSync::kWindow);
        }
        // Beacons stop too: stale.
        CHECK(!cs.estimate(g + ClockSync::kStaleUs).has_value());
    }
    // A craft reboot (clock step > kBeaconStepUs) clears the beacon window
    // but keeps d0; the new clock is tracked at once.
    {
        ClockSync cs;
        uint64_t g = 10'000'000;
        for (size_t i = 0; i < ClockSync::kWindow; ++i) {
            CHECK(cs.on_sample(g, g + 1'000, g + 1'000, g + 2'000));  // off 0
            cs.on_beacon(g, g + 900);
            g += 500'000;
        }
        CHECK(cs.d0_us().has_value());
        g += ClockSync::kStaleUs + 1;
        const uint64_t reboot_craft = 3'000'000;  // craft clock restarted
        cs.on_beacon(reboot_craft, g + 900);
        const auto e = cs.estimate(g + 900);
        CHECK(e.has_value());
        if (e) {
            CHECK(e->beacon);
            CHECK(e->samples == 1);
            CHECK(e->offset_us == static_cast<int64_t>(reboot_craft) -
                                      static_cast<int64_t>(g));
        }
        cs.reset();
        CHECK(!cs.d0_us().has_value());
    }
    return wbtest_finish("clock_sync_test");
}
