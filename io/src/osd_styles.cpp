// SPDX-License-Identifier: GPL-2.0-or-later
// §11.7 0x0B OSD_STYLE (Pass 212): OSD style catalog. See osd_styles.h.
#include "wblink/osd_styles.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>

#include "wblink/types.h"

namespace wblink {

namespace {

bool valid_style_name(const std::string& s) {
    if (s.empty() || s.size() > 63) return false;
    for (const char c : s) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

}  // namespace

std::vector<std::string> osd_style_catalog(const std::string& dir) {
    std::vector<std::string> names;
    if (dir.empty()) return names;
    DIR* d = ::opendir(dir.c_str());
    if (d == nullptr) return names;
    static const std::string ext = ".png";
    while (const dirent* e = ::readdir(d)) {
        const std::string file = e->d_name;
        if (file.size() <= ext.size() ||
            file.compare(file.size() - ext.size(), ext.size(), ext) != 0) {
            continue;
        }
        std::string name = file.substr(0, file.size() - ext.size());
        if (!valid_style_name(name)) continue;
        struct stat st {};
        if (::stat((dir + "/" + file).c_str(), &st) != 0 ||
            !S_ISREG(st.st_mode)) {
            continue;
        }
        names.push_back(std::move(name));
    }
    ::closedir(d);
    std::sort(names.begin(), names.end());  // byte-wise, == strcmp order
    return names;
}

std::string osd_style_apply_arg(const std::string& dir, uint8_t arg) {
    if (arg == kOsdStyleOn) return "@on";
    if (arg == kOsdStyleOff) return "@off";
    if (arg >= kOsdStyleMax) return "";  // 0x40..0xFD: §11.7 range error
    const std::vector<std::string> names = osd_style_catalog(dir);
    if (arg >= names.size()) return "";  // past the catalog end
    return names[arg];
}

}  // namespace wblink
