// SPDX-License-Identifier: GPL-2.0-or-later
// §15.2 Pass 215 power-tier record: round trip, the flag, and the records
// that must be ignored rather than move the radio.
#include "wblink/power_persist.h"

#include <cstdlib>
#include <fstream>
#include <string>

#include "wbtest.h"

using namespace wblink;

namespace {

std::string temp_file() {
    char tmpl[] = "/tmp/wblink-powerXXXXXX";
    const char* d = ::mkdtemp(tmpl);
    return d != nullptr ? std::string(d) + "/power.json" : std::string();
}

void put(const std::string& path, const std::string& body) {
    std::ofstream f(path, std::ios::trunc);
    f << body;
}

void test_round_trip() {
    const std::string path = temp_file();
    CHECK(!read_persisted_tier(path));  // absent
    CHECK(write_persisted_tier(path, 0));
    CHECK(read_persisted_tier(path) == 0);
    CHECK(write_persisted_tier(path, 4));  // overwrite in place
    CHECK(read_persisted_tier(path) == 4);
}

void test_record_needs_the_flag() {
    const std::string path = temp_file();
    Config cfg;
    cfg.policy.cmd.tx_power_file = path;
    record_tier(cfg, 2);
    CHECK(!read_persisted_tier(path));  // persist_tx_power off: nothing
    cfg.policy.cmd.persist_tx_power = true;
    record_tier(cfg, 2);
    CHECK(read_persisted_tier(path) == 2);
}

// A tier past the 0x0A range, or anything that is not a tier, must not be
// handed to set_power_tier at boot.
void test_bad_records_are_ignored() {
    const std::string path = temp_file();
    for (const char* body : {"{\"tier\":5}", "{\"tier\":-1}", "{\"tier\":\"2\"}",
                             "{\"mhz\":5805}", "not json", ""}) {
        put(path, body);
        CHECK(!read_persisted_tier(path));
    }
}

}  // namespace

int main() {
    test_round_trip();
    test_record_needs_the_flag();
    test_bad_records_are_ignored();
    return wbtest_finish("power_persist_test");
}
