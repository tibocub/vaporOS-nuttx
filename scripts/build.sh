#!/usr/bin/env bash
# scripts/build.sh [board] -- called by `make -f dev.mk build`.
set -euo pipefail

BOARD="${1:-nsh}"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE="$(dirname "$DIR")"
cd "$WORKSPACE"

if [ ! -e apps/external ]; then
  echo "apps/external missing -- run setup.sh (or setup.ps1) first." >&2
  exit 1
fi

source "$DIR/scripts/lib.sh"
cd nuttx

# Pick up any new app directories / config changes since last build.
[ -f .config ] && distclean_preserving_lua

case "$BOARD" in
  nxterm)
    ./tools/configure.sh sim:nx11
    kconfig-tweak --disable CONFIG_EXAMPLES_NX
    kconfig-tweak --enable CONFIG_NXTERM
    kconfig-tweak --enable CONFIG_EXAMPLES_NXTERM
    kconfig-tweak --set-str CONFIG_INIT_ENTRYPOINT nxterm_main
    # CLE (NSH's default line editor) assumes a full VT100 terminal;
    # NxTerm's own VT100 support is minimal. NSH_READLINE avoids that.
    kconfig-tweak --enable CONFIG_NSH_READLINE
    ;;
  vterm_fb)
    ./tools/configure.sh sim:nx11
    kconfig-tweak --disable CONFIG_EXAMPLES_NX
    kconfig-tweak --set-str CONFIG_INIT_ENTRYPOINT vterm_fb_main
    kconfig-tweak --enable CONFIG_NSH_READLINE
    kconfig-tweak --enable CONFIG_PSEUDOTERM
    kconfig-tweak --enable CONFIG_DRIVERS_VIDEO
    kconfig-tweak --enable CONFIG_VIDEO_FB
    kconfig-tweak --enable CONFIG_SIM_FRAMEBUFFER
    kconfig-tweak --enable CONFIG_NXFONTS
    kconfig-tweak --enable CONFIG_NXFONT_X11_MISC_FIXED_10X20
    kconfig-tweak --set-val CONFIG_SIM_FBWIDTH 800
    kconfig-tweak --set-val CONFIG_SIM_FBHEIGHT 600
    kconfig-tweak --enable CONFIG_BOARDCTL_POWEROFF
    kconfig-tweak --disable CONFIG_DISABLE_MOUNTPOINT
    kconfig-tweak --enable CONFIG_FS_HOSTFS
    kconfig-tweak --enable CONFIG_SIM_HOSTFS
    kconfig-tweak --enable CONFIG_VAPOROS_VTERM_FB
    ;;
  *)
    ./tools/configure.sh sim:"$BOARD"
    ;;
esac

kconfig-tweak --enable CONFIG_VAPOROS_VHELLO
kconfig-tweak --enable CONFIG_VAPOROS_PORTABLE_WC
kconfig-tweak --enable CONFIG_VAPOROS_PORTABLE_CAT
kconfig-tweak --enable CONFIG_VAPOROS_VLUA
kconfig-tweak --enable CONFIG_VAPOROS_VAPORSHELL
kconfig-tweak --enable CONFIG_VAPOROS_TOYBOX
kconfig-tweak --enable CONFIG_VAPOROS_VTERMTEST
kconfig-tweak --enable CONFIG_INTERPRETERS_LUA
kconfig-tweak --enable CONFIG_NSH_LIBRARY     # INTERPRETERS_LUA needs this; not auto-selected on every board
kconfig-tweak --enable CONFIG_SYSTEM_READLINE # nsh_script.c needs this declared
kconfig-tweak --enable CONFIG_SYSTEM_VI
kconfig-tweak --enable CONFIG_LIBC_STRERROR
kconfig-tweak --enable CONFIG_LIBC_LOCALTIME

# Command history (up/down arrows, Ctrl+P/Ctrl+N -- the latter added
# by patches/nuttx-apps/readline-history-ctrlpn.patch, see setup.sh)
# and Emacs-style line editing, including Ctrl+R reverse incremental
# search. CMD_HISTORY_LEN/LINELEN sized generously (not the small-
# device defaults) since this is a sim-only concern right now, not a
# real, memory-constrained target.
kconfig-tweak --enable CONFIG_READLINE_CMD_HISTORY
kconfig-tweak --set-val CONFIG_READLINE_CMD_HISTORY_LEN 64
kconfig-tweak --set-val CONFIG_READLINE_CMD_HISTORY_LINELEN 128
kconfig-tweak --enable CONFIG_READLINE_EDIT_EMACS
kconfig-tweak --enable CONFIG_READLINE_EDIT_EMACS_REVERSE_SEARCH

# Foundational binary-loading support (docs/binary-loading.md): lets a
# separately-compiled ELF file -- built with MODULE=m instead of the
# usual MODULE=y -- be placed on a writable filesystem and run by
# name, the same way any builtin is. This is additive only: it changes
# nothing about how existing, compiled-in apps build or run, and is
# the real foundation for "download or compile a program on vaporOS
# itself and run it", not something bolted onto NSH/vaporshell.
kconfig-tweak --enable CONFIG_ELF
kconfig-tweak --enable CONFIG_LIBC_ENVPATH
kconfig-tweak --enable CONFIG_EXECFUNCS_HAVE_SYMTAB
kconfig-tweak --enable CONFIG_EXECFUNCS_SYSTEM_SYMTAB
kconfig-tweak --enable CONFIG_BOARDCTL_APP_SYMTAB
kconfig-tweak --disable CONFIG_DISABLE_MOUNTPOINT
kconfig-tweak --enable CONFIG_FS_HOSTFS
kconfig-tweak --enable CONFIG_SIM_HOSTFS

# Compatibility triage tool (docs/c-posix-compatibility.md): scans a C
# source tree for known vaporOS/NuttX portability gaps, runnable both
# on the dev host and directly on vaporOS itself.
kconfig-tweak --enable CONFIG_VAPOROS_COMPAT_SCAN

make olddefconfig
make -j"$(nproc)"

echo "Done. Run with: make -f dev.mk run"
