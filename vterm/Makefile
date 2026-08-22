include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_VAPOROS_VTERMTEST_PROGNAME)
PRIORITY  = $(CONFIG_VAPOROS_VTERMTEST_PRIORITY)
STACKSIZE = $(CONFIG_VAPOROS_VTERMTEST_STACKSIZE)
MODULE    = $(CONFIG_VAPOROS_VTERMTEST)

# Milestone 1 of docs/vaporterm.md: confirm libvterm itself builds
# through the real NuttX app pipeline (not just plain host gcc) before
# any NX integration work. This CFLAGS/CSRCS list is deliberately
# vendored-source, not a fetched dependency -- see the vaporterm.md
# rationale (no network fetch step to fail mid-build, unlike Lua's).
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/external/vterm/libvterm/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/external/vterm/libvterm/src

CSRCS  = libvterm/src/encoding.c
CSRCS += libvterm/src/keyboard.c
CSRCS += libvterm/src/mouse.c
CSRCS += libvterm/src/parser.c
CSRCS += libvterm/src/pen.c
CSRCS += libvterm/src/screen.c
CSRCS += libvterm/src/state.c
CSRCS += libvterm/src/unicode.c
CSRCS += libvterm/src/vterm.c

MAINSRC = vtermtest_main.c

include $(APPDIR)/external/vapor-nostdinc.mk
include $(APPDIR)/Application.mk
