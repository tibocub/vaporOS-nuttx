# vapor-nostdinc.mk -- shared -nostdinc + freestanding-header CFLAGS
# addition for vaporOS apps. See docs/c-posix-compatibility.md for the
# full rationale: apps/external builds don't pass -nostdinc by
# default, so the *host's* own /usr/include stays reachable as a
# fallback for anything NuttX doesn't provide. That's what actually
# broke toybox's port, twice: __has_include(<utmpx.h>) correctly found
# *a* utmpx.h (the host's glibc one -- NuttX has none at all), which
# assumes glibc's own pid_t chain and fails to parse against NuttX's;
# and __GLIBC__ (defined by glibc's own headers, not the compiler)
# silently becomes true once any host header is transitively
# reachable, steering feature-detection code down Linux-specific
# branches. Once host headers are blocked, the *compiler's* own
# freestanding headers (stdarg.h and friends -- NuttX's include/
# doesn't provide these, only stddef.h/float.h are NuttX's own) have
# to be added back explicitly, which is what the second line does.
#
# Centralized here, rather than copied into each app's own Makefile
# (toybox's own port had it inline before this file existed) so a
# future fix only has to happen once. Opt-in per app, not automatic
# for every existing one: applying this retroactively risked
# surfacing new problems an app happened to be masking by relying on
# leaked host headers for something -- verified directly, app by app,
# that adding this doesn't break anything already working, rather
# than assumed safe. Add it to a new app with one line, after that
# app's own CFLAGS additions but before `include
# $(APPDIR)/Application.mk`:
#
#   include $(APPDIR)/external/vapor-nostdinc.mk

CFLAGS += -nostdinc
CFLAGS += ${INCDIR_PREFIX}$(shell $(CC) -print-file-name=include)
