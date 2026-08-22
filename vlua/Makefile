include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_VAPOROS_VLUA_PROGNAME)
PRIORITY  = $(CONFIG_VAPOROS_VLUA_PRIORITY)
STACKSIZE = $(CONFIG_VAPOROS_VLUA_STACKSIZE)
MODULE    = $(CONFIG_VAPOROS_VLUA)

# Reuse NuttX's own Lua source/headers (see interpreters/lua) rather
# than bundling a second copy -- this only works once CONFIG_INTERPRETERS_LUA
# has downloaded it, hence the Kconfig dependency below.
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/interpreters/lua/lua

MAINSRC = vlua_main.c
CSRCS = vapor_core.c

include $(APPDIR)/external/vapor-nostdinc.mk
include $(APPDIR)/Application.mk
