/*
 * WP-56 reent-tls-bringup, item 1: the real-process stub compatibility layer.
 *
 * DR-0066 closed item 1's empirical phase: a host stub linked in the
 * real-process shape (-nostdlib against the WP-26 crt0.o and -lcygwin, so
 * _dll_crt0 brings the reent up) does not fault on a window collision. It
 * faults at exactly one place -- the crt0 startup path calls
 * cygwin_internal(CW_USER_DATA) Microsoft-style into the faced runtime's
 * System V veneer -- and, past a bridge for that, the stub's own libc use is a
 * Microsoft-into-System-V call that returns without crossing. spike/reent-stub-
 * realproc-window and spike/reent-stub-realproc-version measured both, and a
 * demonstrated fix for each.
 *
 * This header is that fix as a reusable seam. It is a no-op for the ordinary
 * (plain-PE) build the WP-41 exec-* certifications drive: without
 * ELFSYSV_REALPROC every macro below expands to the plain libc it names, so a
 * translation unit that includes it is byte-for-byte the program it was. Under
 * ELFSYSV_REALPROC it routes the two crossings the spikes isolated:
 *
 *   - startup: realproc.c defines a sysv_abi cygwin_internal bridge so the
 *     crt0 crossing reaches main (the -DBRIDGE shape realproc-window pinned);
 *
 *   - the stub's own work: RP_* primitives below do a translation unit's
 *     string and format work with host-safe freestanding code -- never a
 *     Microsoft-into-System-V call into the faced libc -- and route its output
 *     through a sysv_abi puts thunk resolved from elfsysv1.dll, the crossing
 *     direction the ELF world already uses.
 *
 * DR-0066 left the choice between confining the stub to host-safe calls and
 * reaching the faced libc through the System V crossing to the implementing
 * step. This layer takes the host-safe route for the stub's own string and
 * parsing work (freestanding, no crossing at all) and the System V crossing
 * only where a result must come from the faced runtime -- output.
 */
#ifndef ELFSYSV_LOADER_EXEC_REALPROC_H
#define ELFSYSV_LOADER_EXEC_REALPROC_H

#ifndef ELFSYSV_REALPROC

/* Plain (WP-41) build: the seam is the identity. Name the libc directly. */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#define RP_STRCMP(a, b)          strcmp((a), (b))
#define RP_STRNCMP(a, b, n)      strncmp((a), (b), (n))
#define RP_STRLEN(s)             strlen((s))
#define RP_STRTOULL(s, e, base)  strtoull((s), (e), (base))
#define RP_PUTS(s)               puts((s))
#define RP_SNPRINTF(b, n, ...)   snprintf((b), (n), __VA_ARGS__)
#define RP_VSNPRINTF(b, n, f, a) vsnprintf((b), (n), (f), (a))

#else /* ELFSYSV_REALPROC */

#include <stddef.h>
#include <stdarg.h>

/* Freestanding, host-safe: these never call the faced libc, so they carry no
 * ABI crossing. They are the stub's own string and parsing work. */
int                rp_strcmp(const char *a, const char *b);
int                rp_strncmp(const char *a, const char *b, size_t n);
size_t             rp_strlen(const char *s);
unsigned long long rp_strtoull(const char *s, char **end, int base);

/* Freestanding formatting, likewise host-safe: the finished bytes cross
 * through rp_puts, the formatting itself calls no libc. snprintf semantics --
 * NUL-terminate within size, return the length a large-enough buffer would
 * hold. Scope is realproc-fmt.c's conversions, the ones the stub prints. */
int                rp_vsnprintf(char *buf, size_t size, const char *fmt,
                                va_list ap);
int                rp_snprintf(char *buf, size_t size, const char *fmt, ...);

/* Output the one way that must reach the faced runtime: a sysv_abi puts thunk
 * resolved from elfsysv1.dll's export directory, the crossing the ELF world
 * uses. Returns non-negative on success, EOF (-1) if the export is absent. */
int                rp_puts(const char *s);

#define RP_STRCMP(a, b)          rp_strcmp((a), (b))
#define RP_STRNCMP(a, b, n)      rp_strncmp((a), (b), (n))
#define RP_STRLEN(s)             rp_strlen((s))
#define RP_STRTOULL(s, e, base)  rp_strtoull((s), (e), (base))
#define RP_SNPRINTF(b, n, ...)   rp_snprintf((b), (n), __VA_ARGS__)
#define RP_VSNPRINTF(b, n, f, a) rp_vsnprintf((b), (n), (f), (a))
#define RP_PUTS(s)               rp_puts((s))

#endif /* ELFSYSV_REALPROC */

#endif /* ELFSYSV_LOADER_EXEC_REALPROC_H */
