#ifndef WBLINK_GLIBC_236_COMPAT_H
#define WBLINK_GLIBC_236_COMPAT_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <features.h>

#if defined(__GLIBC__) && __GLIBC_PREREQ(2, 38)
#undef __GLIBC_USE_ISOC2X
#define __GLIBC_USE_ISOC2X 0
#undef __GLIBC_USE_C2X_STRTOL
#define __GLIBC_USE_C2X_STRTOL 0
#endif

#endif