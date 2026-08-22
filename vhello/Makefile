include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_VAPOROS_VHELLO_PROGNAME)
PRIORITY  = $(CONFIG_VAPOROS_VHELLO_PRIORITY)
STACKSIZE = $(CONFIG_VAPOROS_VHELLO_STACKSIZE)
MODULE    = $(CONFIG_VAPOROS_VHELLO)

MAINSRC = vhello_main.c

include $(APPDIR)/external/vapor-nostdinc.mk
include $(APPDIR)/Application.mk

# Loadable (MODULE=m) builds must not carry gcov instrumentation: the
# compiler inserts a call to __gcov_init() for any coverage-instrumented
# file, but that symbol is a coverage-runtime internal, never exported
# in the kernel's app symbol table (confirmed directly -- traced the
# real failure via gdb to libelf_symvalue() returning -ENOENT
# specifically for "__gcov_init", not any real, missing app symbol).
# Coverage of a dynamically-loaded, separately-run program isn't
# meaningful under gcov's own single-process-lifetime data-dumping
# model anyway. Stripped here, per-app, rather than in NuttX's own
# Make.defs -- CELFFLAGS is a normal, overridable recursive variable,
# so this only affects this app's own MODULE=m build. Harmless when
# CONFIG_VAPOROS_VHELLO stays "y" (builtin): CELFFLAGS is only used
# for MODULE=m builds in the first place.
ifeq ($(CONFIG_COVERAGE_ALL),y)
CELFFLAGS := $(filter-out -fprofile-arcs -ftest-coverage,$(CELFFLAGS))
endif
