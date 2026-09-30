#ifndef CGSS_COMPAT_DIRECT_H
#define CGSS_COMPAT_DIRECT_H
#include <sys/stat.h>
#include <sys/types.h>
static inline int _mkdir(const char *path) { return mkdir(path, 0755); }
#endif
