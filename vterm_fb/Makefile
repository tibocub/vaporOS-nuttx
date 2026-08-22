include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_VAPOROS_VTERM_FB_PROGNAME)
PRIORITY  = $(CONFIG_VAPOROS_VTERM_FB_PRIORITY)
STACKSIZE = $(CONFIG_VAPOROS_VTERM_FB_STACKSIZE)
MODULE    = $(CONFIG_VAPOROS_VTERM_FB)

# CONFIG_VAPOROS_VTERM_FB=n alone isn't reliably enough to make
# Application.mk skip building MAINSRC -- confirmed directly: a plain
# sim:nsh build (this app correctly disabled) still attempted to
# compile vterm_fb_main.c and failed, since it depends on NXFONTS/
# video headers that aren't available outside the vterm_fb target.
# Guarding CSRCS/MAINSRC behind the config check directly, so
# Application.mk never sees them at all when disabled, rather than
# relying on its own MODULE-based skip logic.
ifeq ($(CONFIG_VAPOROS_VTERM_FB),y)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/external/vterm/libvterm/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/external/vterm/libvterm/src
# Diagnostic logging (dumps every byte read from the pty master to
# /data/vaporterm_debug.log). Left on since it's cheap and has
# earned its keep more than once already -- turn off if the sim's
# hostfs mount ever becomes a bottleneck, not before.
CFLAGS += -DVAPORTERM_DEBUG_LOG

CSRCS  = ../vterm/libvterm/src/encoding.c
CSRCS += ../vterm/libvterm/src/keyboard.c
CSRCS += ../vterm/libvterm/src/mouse.c
CSRCS += ../vterm/libvterm/src/parser.c
CSRCS += ../vterm/libvterm/src/pen.c
CSRCS += ../vterm/libvterm/src/screen.c
CSRCS += ../vterm/libvterm/src/state.c
CSRCS += ../vterm/libvterm/src/unicode.c
CSRCS += ../vterm/libvterm/src/vterm.c

MAINSRC = vterm_fb_main.c
endif

include $(APPDIR)/external/vapor-nostdinc.mk
include $(APPDIR)/Application.mk
