// SPDX-License-Identifier: GPL-2.0-or-later
// §15.2 Pass 215 power-tier record — see power_persist.h.
#include "wblink/power_persist.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "wblink/log.h"

namespace wblink {

using json = nlohmann::json;

std::optional<uint8_t> read_persisted_tier(const std::string& path) {
    std::ifstream f(path);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    const json j = json::parse(ss.str(), nullptr, false);
    if (!j.is_object() || !j.contains("tier") ||
        !j["tier"].is_number_unsigned()) {
        return std::nullopt;
    }
    const uint64_t tier = j["tier"].get<uint64_t>();
    if (tier > 4) return std::nullopt;
    return static_cast<uint8_t>(tier);
}

bool write_persisted_tier(const std::string& path, uint8_t tier) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return false;
        f << "{\"tier\":" << static_cast<unsigned>(tier) << "}\n";
        // close() before the check: a buffered stream reports ENOSPC/EIO
        // only on flush (same rule as channel_persist.cpp).
        f.close();
        if (!f.good()) {
            std::remove(tmp.c_str());
            return false;
        }
    }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

void record_tier(const Config& cfg, uint8_t tier) {
    const CmdPolicy& cmd = cfg.policy.cmd;
    if (!cmd.persist_tx_power) return;
    if (read_persisted_tier(cmd.tx_power_file) == tier) return;  // no rewrite
    if (write_persisted_tier(cmd.tx_power_file, tier)) {
        wb_logf("power: persisted tier %u to %s (Pass 215)\n", tier,
                cmd.tx_power_file.c_str());
    } else {
        wb_logf("power: persisting tier %u to %s FAILED — the next boot uses "
                "the previous record (Pass 215)\n",
                tier, cmd.tx_power_file.c_str());
    }
}

}  // namespace wblink
