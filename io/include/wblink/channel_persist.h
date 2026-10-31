// SPDX-License-Identifier: GPL-2.0-or-later
// §15.2 Pass 210: the `policy.csa.persist_channel` record — the channel this
// node last landed on by campaign or operator retune, so a reboot comes back
// there instead of to adapters[].channel. One small JSON file, {"mhz":N}.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "wblink/config.h"

namespace wblink {

// The recorded channel, or nullopt when the file is absent, unreadable,
// malformed, or names a channel outside 1..65535 MHz.
std::optional<uint16_t> read_persisted_channel(const std::string& path);

// Atomic (temp + rename). False on any I/O failure; the old record stands.
bool write_persisted_channel(const std::string& path, uint16_t mhz);

// Boot half: with persist_channel, a valid record replaces every
// adapters[].channel (and the §15.2 auto-form channel). Returns the channel
// applied.
std::optional<uint16_t> apply_persisted_channel(Config& cfg);

// Write half, called at each landing; logs a failed write. No-op unless
// persist_channel.
void record_channel(const Config& cfg, uint16_t mhz);

}  // namespace wblink
