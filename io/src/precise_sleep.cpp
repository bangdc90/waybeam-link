// SPDX-License-Identifier: GPL-2.0-or-later
// See wblink/precise_sleep.h.
#include "wblink/precise_sleep.h"

namespace wblink {

std::atomic<int> g_precise_sleep_depth{0};

}  // namespace wblink
