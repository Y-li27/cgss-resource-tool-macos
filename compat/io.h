#ifndef CGSS_COMPAT_IO_H
#define CGSS_COMPAT_IO_H
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#ifndef _O_BINARY
#define _O_BINARY 0
#define _O_TEXT 0
#endif
#ifndef _fileno
#define _fileno fileno
#endif
#ifndef _setmode
#define _setmode(fd, mode) ((void)(fd), (void)(mode), 0)
#endif
#endif
