#ifndef RA_PROGRESS_UI_H
#define RA_PROGRESS_UI_H
#include "ra_progress.h"

/* Buffers supplied here hold 32 cells plus a terminator. No font or libc dependency. */
static inline unsigned raProgressClip(char* out, const char* text, unsigned width) {
    unsigned n = 0;
    while (n < width && text[n]) { out[n] = text[n]; ++n; }
    out[n] = 0;
    if (n == width && text[n] && width) out[n - 1] = '~';
    return n;
}

static inline char* raProgressDecimal(char* out, uint32_t value) {
    char digits[10];
    unsigned n = 0;
    do { digits[n++] = '0' + value % 10; value /= 10; } while (value);
    while (n) *out++ = digits[--n];
    *out = 0;
    return out;
}

static inline void raProgressFormat(char* out, const raProgress* p) {
    char* end;
    if (!(p->flags & RA_PROGRESS_MEASURED)) {
        const char* label = (p->flags & RA_PROGRESS_INITIALIZING) ? "Initializing"
            : (p->flags & RA_PROGRESS_DISABLED) ? "Disabled"
            : "No progress data";
        raProgressClip(out, label, 31);
        return;
    }
    if (p->flags & RA_PROGRESS_PERCENT) {
        end = raProgressDecimal(out, raProgressPercent(p->value, p->target));
        *end++ = '%';
    } else {
        end = raProgressDecimal(out, p->value);
        *end++ = '/';
        end = raProgressDecimal(end, p->target);
    }
    *end = 0;
}

static inline unsigned raProgressBarFill(const raProgress* p, unsigned width) {
    if (!(p->flags & RA_PROGRESS_MEASURED) || !p->target) return 0;
    return p->value >= p->target ? width : (unsigned)(((uint64_t)p->value * width) / p->target);
}

static inline void raProgressBar(char* out, const raProgress* p, unsigned width) {
    unsigned i, fill = raProgressBarFill(p, width);
    *out++ = '[';
    for (i = 0; i < width; ++i) *out++ = i < fill ? '=' : '.';
    *out++ = ']';
    *out = 0;
}
#endif
