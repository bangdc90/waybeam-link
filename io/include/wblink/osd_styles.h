// SPDX-License-Identifier: GPL-2.0-or-later
// §11.7 0x0B OSD_STYLE (Pass 212): the on-camera OSD style catalog. The fonts
// belong to the OSD service (waybeam-osd); the link only lists the directory,
// with the SAME rule the service builds its own catalog from, so an index the
// ground picked names the same style on both sides.
#ifndef WBLINK_OSD_STYLES_H
#define WBLINK_OSD_STYLES_H

#include <cstdint>
#include <string>
#include <vector>

namespace wblink {

// The catalog under `dir`: `<name>.png` regular files, `<name>` 1..63 chars of
// [A-Za-z0-9_-], strcmp-sorted. Unreadable `dir` yields an empty list.
std::vector<std::string> osd_style_catalog(const std::string& dir);

// The single argv the craft passes to `osd.apply_cmd` for `arg`: "@on" for
// kOsdStyleOn, "@off" for kOsdStyleOff, else the catalog name at index `arg`.
// "" (REJECTED) for an index at or past kOsdStyleMax or the catalog end.
std::string osd_style_apply_arg(const std::string& dir, uint8_t arg);

}  // namespace wblink

#endif  // WBLINK_OSD_STYLES_H
