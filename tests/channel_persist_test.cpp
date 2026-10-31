// SPDX-License-Identifier: GPL-2.0-or-later
// §15.2 Pass 210 channel record: round trip, the boot override, and the
// records that must be ignored rather than steer the radio.
#include "wblink/channel_persist.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "wbtest.h"

using namespace wblink;

namespace {

std::string temp_file() {
    char tmpl[] = "/tmp/wblink-chanXXXXXX";
    const char* d = ::mkdtemp(tmpl);
    return d != nullptr ? std::string(d) + "/channel.json" : std::string();
}

void put(const std::string& path, const std::string& body) {
    std::ofstream f(path, std::ios::trunc);
    f << body;
}

Config two_adapters(uint16_t mhz) {
    Config cfg;
    cfg.adapters.resize(2);
    for (AdapterCfg& a : cfg.adapters) a.channel_mhz = mhz;
    return cfg;
}

void test_round_trip() {
    const std::string path = temp_file();
    CHECK(!read_persisted_channel(path));  // absent
    CHECK(write_persisted_channel(path, 5745));
    CHECK(read_persisted_channel(path) == 5745);
    CHECK(write_persisted_channel(path, 5825));  // overwrite in place
    CHECK(read_persisted_channel(path) == 5825);
}

void test_boot_override_needs_the_flag() {
    const std::string path = temp_file();
    CHECK(write_persisted_channel(path, 5745));
    Config cfg = two_adapters(5805);
    cfg.policy.csa.persist_file = path;
    CHECK(!apply_persisted_channel(cfg));  // persist_channel off: config wins
    CHECK_EQ_U(cfg.adapters[0].channel_mhz, 5805u);
    cfg.policy.csa.persist_channel = true;
    CHECK(apply_persisted_channel(cfg) == 5745);
    CHECK_EQ_U(cfg.adapters[0].channel_mhz, 5745u);
    CHECK_EQ_U(cfg.adapters[1].channel_mhz, 5745u);  // every ear moves
}

void test_record_channel() {
    const std::string path = temp_file();
    Config cfg = two_adapters(5805);
    cfg.policy.csa.persist_file = path;
    record_channel(cfg, 5765);  // persist_channel off: nothing written
    CHECK(!read_persisted_channel(path));
    cfg.policy.csa.persist_channel = true;
    record_channel(cfg, 5765);
    CHECK(read_persisted_channel(path) == 5765);
}

void test_ignored_records() {
    const std::string path = temp_file();
    Config cfg = two_adapters(5805);
    cfg.policy.csa.persist_channel = true;
    cfg.policy.csa.persist_file = path;
    for (const char* body : {"", "{", "[]", "{\"mhz\":0}", "{\"mhz\":-5}",
                             "{\"mhz\":70000}", "{\"mhz\":\"5745\"}",
                             "{\"chan\":5745}"}) {
        put(path, body);
        CHECK(!apply_persisted_channel(cfg));
        CHECK_EQ_U(cfg.adapters[0].channel_mhz, 5805u);
    }
    // An unwritable location fails loudly and leaves the config alone.
    CHECK(!write_persisted_channel("/nonexistent-dir/channel.json", 5745));
}

}  // namespace

int main() {
    test_round_trip();
    test_boot_override_needs_the_flag();
    test_record_channel();
    test_ignored_records();
    return wbtest_finish("channel_persist");
}
