// SPDX-License-Identifier: GPL-2.0-or-later
// §11.7 0x0B OSD_STYLE (Pass 212): the OSD style catalog the craft resolves an
// index against. It must match waybeam-osd's own rule (<name>.png, name 1..63
// of [A-Za-z0-9_-], regular files, strcmp order), so an index the ground picked
// names the same style on the camera; 0xFE/0xFF are the on/off sentinels.
#include "wblink/osd_styles.h"

#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <string>
#include <vector>

#include "wblink/types.h"
#include "wbtest.h"

using namespace wblink;

namespace {

void write_file(const std::string& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << "png";
}

}  // namespace

int main() {
    char tmpl[] = "/tmp/wblink_osd_XXXXXX";
    const char* dir = ::mkdtemp(tmpl);
    CHECK(dir != nullptr);
    const std::string d = dir;

    // Listed: strcmp order puts upper case and digits before lower case.
    const std::vector<std::string> good = {"msposd_inav", "Walksnail",
                                           "msposd_ardu", "dji-o3", "9x"};
    for (const auto& n : good) write_file(d + "/" + n + ".png");
    // Skipped: wrong extension, bad charset, empty stem, 64-char stem, a
    // directory, an upload temp file.
    write_file(d + "/readme.txt");
    write_file(d + "/bad name.png");
    write_file(d + "/dot.v2.png");
    write_file(d + "/.png");
    write_file(d + "/" + std::string(64, 'a') + ".png");
    write_file(d + "/msposd_btfl.png.tmp");
    ::mkdir((d + "/subdir.png").c_str(), 0755);
    // A 63-char stem is the longest valid name.
    const std::string longest(63, 'z');
    write_file(d + "/" + longest + ".png");

    const std::vector<std::string> cat = osd_style_catalog(d);
    const std::vector<std::string> want = {"9x",          "Walksnail",
                                           "dji-o3",      "msposd_ardu",
                                           "msposd_inav", longest};
    CHECK(cat == want);

    // Index → name, past the end → "" (REJECTED).
    CHECK(osd_style_apply_arg(d, 0) == "9x");
    CHECK(osd_style_apply_arg(d, 4) == "msposd_inav");
    CHECK(osd_style_apply_arg(d, 5) == longest);
    CHECK(osd_style_apply_arg(d, 6).empty());
    // 0x40..0xFD are never catalog indices, even before the catalog is read.
    CHECK(osd_style_apply_arg(d, kOsdStyleMax).empty());
    CHECK(osd_style_apply_arg(d, 0xFD).empty());
    // Sentinels do not depend on the catalog, and '@' is outside the name
    // charset, so no style can be mistaken for them.
    CHECK(osd_style_apply_arg(d, kOsdStyleOn) == "@on");
    CHECK(osd_style_apply_arg(d, kOsdStyleOff) == "@off");
    CHECK(osd_style_apply_arg("", kOsdStyleOff) == "@off");

    // Missing or unset dir → empty catalog.
    CHECK(osd_style_catalog("").empty());
    CHECK(osd_style_catalog(d + "/nope").empty());
    CHECK(osd_style_apply_arg(d + "/nope", 0).empty());

    for (const auto& n : good) ::unlink((d + "/" + n + ".png").c_str());
    for (const char* f : {"readme.txt", "bad name.png", "dot.v2.png", ".png",
                          "msposd_btfl.png.tmp"}) {
        ::unlink((d + "/" + f).c_str());
    }
    ::unlink((d + "/" + std::string(64, 'a') + ".png").c_str());
    ::unlink((d + "/" + longest + ".png").c_str());
    ::rmdir((d + "/subdir.png").c_str());
    ::rmdir(d.c_str());

    return wbtest_finish("osd_styles_test");
}
