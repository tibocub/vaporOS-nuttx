include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_VAPOROS_COMPAT_SCAN_PROGNAME)
PRIORITY  = $(CONFIG_VAPOROS_COMPAT_SCAN_PRIORITY)
STACKSIZE = $(CONFIG_VAPOROS_COMPAT_SCAN_STACKSIZE)
MODULE    = $(CONFIG_VAPOROS_COMPAT_SCAN)

# CONFIG_VAPOROS_COMPAT_SCAN=n alone isn't reliably enough to make
# Application.mk skip building MAINSRC -- same real bug vterm_fb's own
# Makefile already documents (confirmed directly there: a build with
# it disabled still attempted to compile its MAINSRC, without the
# -Dmain= rename, producing the exact "expected identifier or '('
# before 'int'" error at the plain "int main(...)" line, since nothing
# renamed it to compat_scan_main). Guarding CSRCS/MAINSRC behind the
# config check directly, so Application.mk never sees them at all when
# disabled, rather than relying on its own MODULE-based skip logic.
ifeq ($(CONFIG_VAPOROS_COMPAT_SCAN),y)
# compat_scan_main.c is deliberately plain, portable C (no NuttX
# headers, no vaporOS-specific code at all) -- the same source file
# also builds standalone on any POSIX host via a plain
# `cc -o compat-scan compat_scan_main.c`. That's on purpose: the
# whole point of this tool is running the identical triage on the
# device itself, not just on a dev machine.
MAINSRC = compat_scan_main.c
endif

include $(APPDIR)/external/vapor-nostdinc.mk
include $(APPDIR)/Application.mk
