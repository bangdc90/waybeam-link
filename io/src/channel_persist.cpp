// SPDX-License-Identifier: GPL-2.0-or-later
// §15.2 Pass 210 channel record — see channel_persist.h.
#include "wblink/channel_persist.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "wblink/log.h"

namespace wblink {

using json = nlohmann::json;

std::optional<uint16_t> read_persisted_channel(const std::string& path) {
    std::ifstream f(path);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    const json j = json::parse(ss.str(), nullptr, false);
    if (!j.is_object() || !j.contains("mhz") ||
        !j["mhz"].is_number_unsigned()) {
        return std::nullopt;
    }
    const uint64_t mhz = j["mhz"].get<uint64_t>();
    if (mhz == 0 || mhz > 0xFFFF) return std::nullopt;
    return static_cast<uint16_t>(mhz);
}

bool write_persisted_channel(const std::string& path, uint16_t mhz) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return false;
        f << "{\"mhz\":" << mhz << "}\n";
        // close() before the check: a buffered stream reports ENOSPC/EIO
        // only on flush (same rule as calib_store.cpp write_atomic).
        f.close();
        if (!f.good()) {
            std::remove(tmp.c_str());
            return false;
        }
    }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

std::optional<uint16_t> apply_persisted_channel(Config& cfg) {
    const CsaPolicy& csa = cfg.policy.csa;
    if (!csa.persist_channel) return std::nullopt;
    const auto mhz = read_persisted_channel(csa.persist_file);
    if (!mhz) {
        wb_logf("channel: no persisted record at %s — boot channel from "
                "config (Pass 210)\n",
                csa.persist_file.c_str());
        return std::nullopt;
    }
    for (AdapterCfg& a : cfg.adapters) a.channel_mhz = *mhz;
    // §15.2 auto form: the synthesized stanzas take this channel.
    if (cfg.adapter_auto.enabled) cfg.adapter_auto.channel_mhz = *mhz;
    wb_logf("channel: boot onto persisted %u MHz from %s (Pass 210)\n", *mhz,
            csa.persist_file.c_str());
    return mhz;
}

void record_channel(const Config& cfg, uint16_t mhz) {
    const CsaPolicy& csa = cfg.policy.csa;
    if (!csa.persist_channel) return;
    if (read_persisted_channel(csa.persist_file) == mhz) return;  // no rewrite
    if (write_persisted_channel(csa.persist_file, mhz)) {
        wb_logf("channel: persisted %u MHz to %s (Pass 210)\n", mhz,
                csa.persist_file.c_str());
    } else {
        wb_logf("channel: persisting %u MHz to %s FAILED — the next boot "
                "uses the previous record (Pass 210)\n",
                mhz, csa.persist_file.c_str());
    }
}

}  // namespace wblink
