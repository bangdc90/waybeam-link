#pragma once
// §3.16 (Pass 207) link clock-offset estimate — the requester side of the
// TIME_REQ/TIME_RESP exchange, plus (Pass 208) one-way TIME_BEACON tracking
// for a muted ground. Pure: every timestamp is injected, so the estimator is
// unit-testable and owns no clock. Observability only (§1).

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace wblink {

class ClockSync {
  public:
    // §3.16: the NTP clock filter window and the staleness horizon.
    static constexpr size_t kWindow = 8;
    static constexpr uint64_t kStaleUs = 5'000'000;
    // §3.16 (Pass 208): a beacon step this large is a restarted craft clock.
    static constexpr int64_t kBeaconStepUs = 100'000;

    struct Estimate {
        int64_t offset_us = 0;  // responder − requester
        int64_t delay_us = 0;   // round trip minus responder hold time
        size_t samples = 0;
        uint64_t newest_t4_us = 0;
        bool beacon = false;  // §3.16 Pass 208: one-way source, two-way stale
    };

    // Fold one completed exchange. Returns false (and folds nothing) when the
    // timestamps cannot be a real exchange — a reply older than its request,
    // or a responder hold longer than the round trip.
    bool on_sample(uint64_t t1, uint64_t t2, uint64_t t3, uint64_t t4) {
        if (t4 < t1 || t3 < t2) return false;
        const uint64_t round = t4 - t1;
        const uint64_t hold = t3 - t2;
        if (hold > round) return false;
        Sample s;
        // (t2 − t1) + (t3 − t4) in signed space: the two clocks are unrelated
        // monotonic origins, so either term can be hugely negative.
        s.offset_us = (static_cast<int64_t>(t2 - t1) +
                       static_cast<int64_t>(t3 - t4)) / 2;
        s.delay_us = static_cast<int64_t>(round - hold);
        s.t4_us = t4;
        ring_[next_] = s;
        next_ = (next_ + 1) % kWindow;
        if (count_ < kWindow) ++count_;
        calibrate(t4);
        return true;
    }

    // §3.16 (Pass 208) fold one TIME_BEACON: t3 = craft clock at its radio
    // call, t4 = ours at receipt. Keeps s = t4 − t3 = d − offset.
    void on_beacon(uint64_t t3, uint64_t t4) {
        const int64_t s = static_cast<int64_t>(t4 - t3);
        if (bcount_ > 0) {
            const int64_t prev = bring_[(bnext_ + kWindow - 1) % kWindow].s;
            if ((s > prev ? s - prev : prev - s) > kBeaconStepUs) {
                bcount_ = bnext_ = 0;  // restarted craft clock: old s is void
            }
        }
        bring_[bnext_] = Beacon{s, t4};
        bnext_ = (bnext_ + 1) % kWindow;
        if (bcount_ < kWindow) ++bcount_;
        calibrate(t4);
    }

    // The calibrated one-way floor d0, once a full two-way window and a full
    // beacon window have been fresh together.
    std::optional<int64_t> d0_us() const { return d0_; }

    // The min-delay sample of the window, or nullopt when there is none or
    // the newest is older than kStaleUs at `now_us`.
    // Two-way when fresh; else (Pass 208) d0 − min s over a fresh beacon
    // window once d0 is calibrated; else nullopt.
    std::optional<Estimate> estimate(uint64_t now_us) const {
        if (auto e = two_way(now_us)) return e;
        if (!d0_ || bcount_ == 0) return std::nullopt;
        uint64_t newest = 0;
        const int64_t m = beacon_min(&newest);
        if (now_us > newest && now_us - newest > kStaleUs) return std::nullopt;
        Estimate e;
        e.offset_us = *d0_ - m;
        e.delay_us = *d0_;
        e.samples = bcount_;
        e.newest_t4_us = newest;
        e.beacon = true;
        return e;
    }

    void reset() {
        count_ = next_ = 0;
        bcount_ = bnext_ = 0;
        d0_.reset();
    }

  private:
    std::optional<Estimate> two_way(uint64_t now_us) const {
        if (count_ == 0) return std::nullopt;
        const Sample* best = nullptr;
        uint64_t newest = 0;
        for (size_t i = 0; i < count_; ++i) {
            const Sample& s = ring_[i];
            if (s.t4_us > newest) newest = s.t4_us;
            if (best == nullptr || s.delay_us < best->delay_us) best = &s;
        }
        if (now_us > newest && now_us - newest > kStaleUs) return std::nullopt;
        Estimate e;
        e.offset_us = best->offset_us;
        e.delay_us = best->delay_us;
        e.samples = count_;
        e.newest_t4_us = newest;
        return e;
    }

    int64_t beacon_min(uint64_t* newest) const {
        int64_t m = bring_[0].s;
        for (size_t i = 0; i < bcount_; ++i) {
            if (bring_[i].s < m) m = bring_[i].s;
            if (bring_[i].t4_us > *newest) *newest = bring_[i].t4_us;
        }
        return m;
    }

    // d0 = offset₂ + min s while both full windows are fresh at `now_us`.
    void calibrate(uint64_t now_us) {
        if (count_ < kWindow || bcount_ < kWindow) return;
        const auto e = two_way(now_us);
        if (!e) return;
        uint64_t newest = 0;
        const int64_t m = beacon_min(&newest);
        if (now_us > newest && now_us - newest > kStaleUs) return;
        d0_ = e->offset_us + m;
    }

    struct Sample {
        int64_t offset_us = 0;
        int64_t delay_us = 0;
        uint64_t t4_us = 0;
    };
    struct Beacon {
        int64_t s = 0;  // t4 − t3
        uint64_t t4_us = 0;
    };
    std::array<Sample, kWindow> ring_{};
    size_t count_ = 0;
    size_t next_ = 0;
    std::array<Beacon, kWindow> bring_{};
    size_t bcount_ = 0;
    size_t bnext_ = 0;
    std::optional<int64_t> d0_;
};

}  // namespace wblink
