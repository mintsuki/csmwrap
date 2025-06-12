#include <stddef.h>
#include <stdarg.h>

#define NANOPRINTF_IMPLEMENTATION
#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_SMALL_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS 1
#include <nanoprintf.h>

#include <flanterm.h>
#include <flanterm_backends/fb.h>

struct flanterm_context *flanterm_ctx = NULL;

static void _putchar(int character, void *extra_arg) {
    (void)extra_arg;

    if (character == '\n') {
        _putchar('\r', NULL);
    }

    if (flanterm_ctx != NULL) {
        flanterm_write(flanterm_ctx, (const char *)&character, 1);
    }
}

int printf(const char *restrict fmt, ...) {
    va_list l;
    va_start(l, fmt);
    int ret = npf_vpprintf(_putchar, NULL, fmt, l);
    va_end(l);
    return ret;
}
