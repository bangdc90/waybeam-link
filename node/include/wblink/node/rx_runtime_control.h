// SPDX-License-Identifier: GPL-2.0-or-later
// Thread-safe mailbox between an RX node's owner thread and run_rx's event
// loop. The mailbox owns no thread and never exposes loop-owned ScoutEngine or
// DiscoveryCatalog state to another thread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "wblink/node/snapshot_copy.h"

namespace wblink {
namespace node {

class RxRuntimeControl {
  public:
    enum class CommandKind : uint8_t {
        kScoutStart,
        kScoutStop,
        kScoutSelect,
        kClaim,           // §11.4 CSA claim of a scouted craft
        kVehicleCommand,  // §11.7 command campaign toward the bound craft
    };

    struct Command {
        CommandKind kind = CommandKind::kScoutStop;
        std::vector<uint16_t> channels;
        uint32_t dwell_ms = 0;
        uint16_t originator = 0;
        uint64_t generation = 0;
        // kClaim: 0 means "the emptiest allowlisted channel", matching the
        // claim helper's own contract rather than inventing a sentinel here.
        uint16_t target_chan = 0;
        // kVehicleCommand: the §15.5 REST spelling, resolved to a §11.7 id by
        // the loop. Kept as a name and not an id because the name map is the
        // control-plane vocabulary and lives in node/vcmd.h, not here.
        std::string cmd;
        int32_t arg = 0;
    };

    // The longest §15.5 command name is 10 characters. A C caller hands us a
    // pointer, so the copy is bounded before it is made rather than after —
    // an unterminated or hostile string must not turn into an allocation.
    static constexpr size_t kMaxCommandName = 32;

    RxRuntimeControl() = default;
    RxRuntimeControl(const RxRuntimeControl&) = delete;
    RxRuntimeControl& operator=(const RxRuntimeControl&) = delete;

    // One control belongs to one run_rx invocation. start_run opens the
    // enqueue side; stop_run closes it and discards a command the loop did not
    // take. Published snapshots deliberately survive stop_run so a caller can
    // inspect the final state after joining the RX thread.
    void start_run();
    void stop_run();
    bool running() const;

    // C-ABI-shaped results: 0 accepted, 1 allocation/internal failure,
    // 2 invalid argument, 3 no running RX loop. The channel array is copied;
    // NULL is legal only when n == 0. A single pending slot implements latest
    // intent: a new start or stop supersedes a command not yet taken by the
    // loop. Accepted commands receive monotonically increasing generations.
    int enqueue_scout_start(const uint16_t* channels, size_t n,
                            uint32_t dwell_ms, uint64_t* generation);
    int enqueue_scout_stop(uint64_t* generation);
    int enqueue_scout_select(uint16_t originator, uint64_t* generation);
    // §11.4 / §11.7. Same convention as the scout enqueues above, and the same
    // single pending slot — a claim supersedes an un-taken sweep, exactly as
    // the loop's own claim path abandons a running one. `target_chan` 0 asks
    // for the emptiest allowlisted channel. `cmd` is copied; it need not
    // outlive the call, and a name longer than kMaxCommandName is rejected as
    // an invalid argument rather than truncated into a different command.
    int enqueue_claim(uint16_t originator, uint16_t target_chan,
                      uint64_t* generation);
    int enqueue_vehicle_command(const char* cmd, int32_t arg,
                                uint64_t* generation);
    std::optional<Command> take_command();

    void note_applied(uint64_t generation);
    uint64_t applied_generation() const;

    // Pass 216: §15.5 POST /api/v1/uplink. Its OWN latest-intent slot and
    // applied generation: a menu unmutes and then switches channel back to
    // back, so an uplink command must not supersede an un-taken claim (or be
    // superseded by one), and applying it must not mark a pending claim
    // applied. Generations still come from the one counter, so they stay
    // unique across both slots. `auto_mute` without `tx` is 2 (REST 400).
    struct UplinkCommand {
        bool tx = true;
        bool auto_mute = false;
        uint64_t generation = 0;
    };
    int enqueue_uplink(bool tx, bool auto_mute, uint64_t* generation);
    std::optional<UplinkCommand> take_uplink();
    void note_uplink_applied(uint64_t generation);
    uint64_t uplink_applied_generation() const;

    // Pass 217: §15.5 POST /api/v1/csa and /api/v1/move share one more
    // latest-intent slot, the "channel" slot: both name the channel the link
    // should be on, so a newer one supersedes an un-taken older one, and
    // neither touches the command or uplink slot. Own applied generation,
    // generations from the one counter. `mhz` 0 is 2 (REST 400).
    enum class ChannelKind { kCsa, kMove };
    struct ChannelCommand {
        ChannelKind kind = ChannelKind::kCsa;
        uint16_t mhz = 0;
        uint32_t klass = 0;  // kCsa: §11 class, as POST /csa "class"
        uint64_t generation = 0;
    };
    int enqueue_csa(uint16_t mhz, uint32_t klass, uint64_t* generation);
    int enqueue_move(uint16_t mhz, uint64_t* generation);
    std::optional<ChannelCommand> take_channel();
    void note_channel_applied(uint64_t generation);
    uint64_t channel_applied_generation() const;

    // A reader requests a fresh snapshot as part of every copy attempt. The
    // RX loop consumes these edge flags and serializes loop-owned state on its
    // own thread. A request arriving after a take remains set for the next
    // iteration; publish does not clear it.
    bool take_scout_snapshot_request();
    bool take_discovery_snapshot_request();
    bool take_selection_snapshot_request();
    bool take_command_snapshot_request();
    bool take_clock_snapshot_request();   // Pass 216
    bool take_uplink_snapshot_request();  // Pass 216
    bool take_thermal_snapshot_request();  // Pass 219

    // Publication swaps an immutable string. Scout generation names the
    // command state represented by that JSON and is returned atomically with
    // it by copy_scout.
    void publish_scout(std::string json, uint64_t generation);
    void publish_discovery(std::string json);
    // §15.5 (Pass 172): the per-die capability answers. Published once at
    // backend bring-up — the fields are static per die, so there is no
    // snapshot-request edge for the loop to serve; the bring-up publish IS
    // the freshest state for the life of the run.
    void publish_adapters(std::string json);
    void publish_selection(std::string json, uint64_t generation);
    // The §11.7 campaign state, plus the synchronous verdict of the queued
    // command that carried this generation. A queued caller cannot be handed
    // the helper's return value, so the outcome has to arrive here or nowhere.
    void publish_command(std::string json, uint64_t generation);
    // Pass 176: the §15.3 stats line and §15.4 health object, published by
    // the run loop at the stats cadence from the ONE fill path
    // (emit_stats / build_health_json). No snapshot-request edge: the loop
    // republishes on its own stats.hz beat, and stats.hz=0 means these stay
    // unpublished by design — the always-on surface is the status/lifecycle
    // snapshot, not a silently re-enabled stats walk.
    void publish_stats(std::string json);
    void publish_health(std::string json);
    // Pass 178: "addr:port" as resolved from the listening socket, published
    // once, after a successful bind. Not JSON — a bare endpoint string.
    void publish_control_endpoint(std::string endpoint);
    // Pass 216: the GET /api/v1/clock and GET /api/v1/uplink bodies, from the
    // same generators the control server calls. The uplink snapshot carries
    // the uplink applied generation, never the scout/claim one.
    void publish_clock(std::string json);
    void publish_uplink(std::string json, uint64_t generation);
    // Pass 219: the GET /api/v1/thermal body, from the control server's
    // generator.
    void publish_thermal(std::string json);
    // Pass 217: the verdict of the channel command that carried this
    // generation (the REST reply it would have got). Published once per
    // applied channel command, so there is no snapshot-request edge.
    void publish_channel(std::string json, uint64_t generation);

    // Buffer-copy contract for the C shim: `required` includes the trailing
    // NUL. NULL + zero is a size query. Returns 0 on success/query, 2 for bad
    // arguments, 3 before the first snapshot, and 4 when capacity is short.
    // No partial JSON is copied. Reads remain valid after stop_run.
    int copy_scout(char* buffer, size_t capacity, size_t* required,
                   uint64_t* generation);
    int copy_discovery(char* buffer, size_t capacity, size_t* required);
    int copy_adapters(char* buffer, size_t capacity, size_t* required);
    int copy_selection(char* buffer, size_t capacity, size_t* required,
                       uint64_t* generation);
    int copy_command(char* buffer, size_t capacity, size_t* required,
                     uint64_t* generation);
    int copy_stats(char* buffer, size_t capacity, size_t* required);
    int copy_health(char* buffer, size_t capacity, size_t* required);
    int copy_control_endpoint(char* buffer, size_t capacity, size_t* required);
    int copy_clock(char* buffer, size_t capacity, size_t* required);
    int copy_uplink(char* buffer, size_t capacity, size_t* required,
                    uint64_t* generation);
    int copy_thermal(char* buffer, size_t capacity, size_t* required);
    int copy_channel(char* buffer, size_t capacity, size_t* required,
                     uint64_t* generation);

  private:
    // GeneratedSnapshot moved to snapshot_copy.h with the copy contract
    // (Pass 176).
    mutable std::mutex mutex_;
    bool running_ = false;
    uint64_t next_generation_ = 0;
    uint64_t applied_generation_ = 0;
    std::optional<Command> pending_;
    uint64_t uplink_applied_generation_ = 0;
    std::optional<UplinkCommand> pending_uplink_;
    uint64_t channel_applied_generation_ = 0;
    std::optional<ChannelCommand> pending_channel_;
    bool scout_snapshot_requested_ = false;
    bool discovery_snapshot_requested_ = false;
    bool selection_snapshot_requested_ = false;
    bool command_snapshot_requested_ = false;
    bool clock_snapshot_requested_ = false;
    bool uplink_snapshot_requested_ = false;
    bool thermal_snapshot_requested_ = false;
    std::shared_ptr<const GeneratedSnapshot> scout_snapshot_;
    std::shared_ptr<const std::string> discovery_snapshot_;
    std::shared_ptr<const std::string> adapters_snapshot_;
    std::shared_ptr<const GeneratedSnapshot> selection_snapshot_;
    std::shared_ptr<const GeneratedSnapshot> command_snapshot_;
    std::shared_ptr<const std::string> stats_snapshot_;
    std::shared_ptr<const std::string> health_snapshot_;
    std::shared_ptr<const std::string> control_endpoint_;
    std::shared_ptr<const std::string> clock_snapshot_;
    std::shared_ptr<const GeneratedSnapshot> uplink_snapshot_;
    std::shared_ptr<const std::string> thermal_snapshot_;
    std::shared_ptr<const GeneratedSnapshot> channel_snapshot_;
};

}  // namespace node
}  // namespace wblink
