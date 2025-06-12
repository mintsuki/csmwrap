#ifndef PRINTF_H
#define PRINTF_H

#include <flanterm.h>

extern struct flanterm_context *flanterm_ctx;

int printf(const char *restrict fmt, ...);

#endif
