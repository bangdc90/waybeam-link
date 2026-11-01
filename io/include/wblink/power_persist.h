// SPDX-License-Identifier: GPL-2.0-or-later
// §15.2 Pass 215: the `policy.cmd.persist_tx_power` record — the §11.7 0x0A
// tier this craft last accepted, so a reboot comes back at that power instead
// of power_offset_qdb. One small JSON file, {"tier":N}.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "wblink/config.h"

namespace wblink {

// The recorded tier, or nullopt when the file is absent, unreadable,
// malformed, or names a tier outside 0..4 (§11.7 0x0A indexes at most five).
std::optional<uint8_t> read_persisted_tier(const std::string& path);

// Atomic (temp + rename). False on any I/O failure; the old record stands.
bool write_persisted_tier(const std::string& path, uint8_t tier);

// Write half, called on each accepted tier; logs a failed write. No-op unless
// persist_tx_power.
void record_tier(const Config& cfg, uint8_t tier);

}  // namespace wblink
