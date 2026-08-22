# What compiles on vaporOS unmodified, and what doesn't -- and why

Research spike, same standard as `users-and-privilege.md`: checked
directly against source and against this project's own real porting
history (toybox, libvterm, portable_cat/wc), not assumed. Goal:
understand precisely what separates "just works" from "needs a real
port," so future porting effort (curl, mrsh, anything else from the
existing Unix ecosystem) can be spent where it actually matters
instead of rediscovering the same handful of gaps one project at a
time.

## The single biggest finding: this usually isn't NuttX's fault

Before any real POSIX gap: **`apps/external` builds do not pass
`-nostdinc`**, confirmed directly by dumping the real `cc` invocation
(`make -n`). NuttX's own headers are found first via an earlier
`-isystem`, but the *host's* `/usr/include` stays reachable as a
fallback for anything NuttX doesn't provide.

This sounds harmless -- extra fallback headers, what's the harm --
but it actively broke toybox's port, twice, in confusing ways:

- `__has_include(<utmpx.h>)`: NuttX has no `utmpx.h` at all. Without
  `-nostdinc`, this probe finds the *host's* glibc `utmpx.h` instead,
  which assumes glibc's own `__pid_t` chain and fails to parse against
  NuttX's `pid_t`. The probe itself isn't wrong -- `__has_include`
  correctly reports a header exists -- it's just answering about the
  wrong platform.
- `__GLIBC__`: not compiler-predefined at all -- only defined once
  some header chain transitively includes glibc's `<features.h>`.
  Once host headers are reachable, this silently becomes true,
  causing `portability.c` to select Linux-syscall-based code paths
  that don't exist on NuttX. `-U__linux__` (which *is* a predefined
  compiler macro) does nothing to stop this -- it's a completely
  different kind of leak.

**Practical consequence:** software using `__has_include`/feature-macro
probes to detect what a platform supports -- an extremely common,
otherwise-reasonable portability pattern -- gets systematically lied
to on a NuttX target built this way. This is plausibly responsible
for more real porting pain than actual NuttX POSIX gaps. The fix
applied so far (`-nostdinc` + explicitly adding back the *compiler's*
own freestanding headers, e.g. `stdarg.h`, via `-print-file-name=include`)
was done per-app (`toybox/Makefile`); doing this globally for every
`apps/external` build, once, would prevent every future port from
rediscovering the same two bugs independently.

## Genuine POSIX/libc gaps -- real, not build artifacts

Checked directly, each confirmed by source or by hitting it during a
real port:

- **`fork()` is not real fork(), despite the name.** `CONFIG_ARCH_HAVE_FORK`
  support exists broadly (arm, arm64, riscv, x86_64, mips, sim, more --
  not sim-only, correcting an earlier assumption in this project's own
  history). But `task_fork.c`'s own doc comment is explicit: *"the
  behavior is undefined if the process created by fork() either
  modifies any data other than a variable of type pid_t used to store
  the return value from fork(), or returns from the function in which
  fork() was called, or calls any other function before successfully
  calling `_exit()` or one of the exec family of functions."* That's
  `vfork()` semantics under the `fork()` name -- shared memory with the
  parent, safe only for the immediate "fork, then exec-or-exit"
  pattern. Real-world code doing `if (fork() == 0) { do_real_work(); }`
  -- an extremely common pattern -- is undefined behavior here.
  `posix_spawn`/`posix_spawnp` remain the actually-portable primitive
  (confirmed architecture-independent, not under `arch/`); anything
  written against real fork()+exec() needs restructuring, not a shim.
- **No process groups.** `libs/libc/unistd/lib_setpgid.c`'s own
  comment: *"NuttX does not implement process groups, so a process
  group always contains a single member and its ID equals the process
  ID."* `setpgid`/`tcsetpgrp` exist but are stubs. Breaks real job
  control (`Ctrl+Z`, `fg`/`bg` reassigning a whole pipeline) --
  `vaporshell`'s own design doc already scoped this out for the same
  reason.
- **No symlinks anywhere at the VFS layer.** Already confirmed while
  designing the toybox/coreutils split: `struct mountpt_operations`
  (`include/nuttx/fs/fs.h`) has no symlink/link fields in this NuttX
  version, for any mounted filesystem, not a hostfs-specific gap.
- **Missing headers, confirmed absent, not just relocated:** `paths.h`
  (BSD-derived, `_PATH_DEFPATH` and friends) and `utmpx.h` don't exist
  anywhere in NuttX's tree at all.
- **Struct shape differences:** NuttX's `struct statfs`
  (`include/sys/statfs.h`) has no `f_frsize` field at all, unlike
  Linux -- code needs a NuttX-specific case using `f_bsize` for both
  (the pre-`f_frsize` Unix convention). Also, unlike glibc,
  `<sys/statfs.h>` isn't transitively visible from other headers --
  needs an explicit include.
- **No whole-`environ` replacement.** NuttX has `setenv`/`unsetenv`/
  `clearenv` but no glibc/BSD-style "hand me a new `environ` array and
  I'll use it" primitive -- code assuming that needs rewriting against
  the individual calls instead.
- **Honestly absent, not stubbed:** mount-table introspection,
  inotify-style file watching, xattr, `chroot()`, raw `syscall()`.
  `portability.c` needed explicit "NuttX doesn't have this" branches
  for all of these during the toybox port, not silent fallbacks.

## Surprisingly complete -- worth not assuming these are gaps too

- **`/proc` (procfs) is real**, not absent: `fs/procfs/` provides
  `cpuinfo`, `meminfo`, `uptime`, `version`, per-task info, more.
  Software that reads `/proc` for basic system info has a real target
  here, not nothing.
- **`dlopen()`/`dlsym()`/`dlclose()` exist and are real**, not stubs --
  and in `CONFIG_BUILD_FLAT`/`CONFIG_BUILD_PROTECTED` (our case),
  `dlopen()` is *literally* `libelf_insert()`, the same ELF-loading
  mechanism this project already proved out for `MODULE=m` programs.
  Real, documented limits though: no automatic symbol binding across
  libraries, no dependency resolution, `mode` is ignored, and it
  requires `libelf_setsymtab()` already called by the application --
  the same manual symbol-table wiring this project already did by
  hand for its own loadable modules. Genuinely unimplemented (`#warning
  Missing logic`, always `NULL`) under `CONFIG_BUILD_KERNEL` specifically.
- **Networking/threading primitives -- the actual `libcurl`
  question:** real `getaddrinfo()`, real `select()`/`poll()`, 96 real
  (not stub) pthread implementation files, a real `mbedtls` package,
  and even an `openssl_mbedtls_wrapper` (an OpenSSL-API compatibility
  shim over mbedTLS) already present in `apps/crypto/`. `libcurl`
  itself doesn't call `fork()`, doesn't need process groups, doesn't
  need symlinks -- it's built almost entirely on sockets, threads, and
  buffered I/O, exactly the area where NuttX's support is real and
  substantial. On the evidence gathered here, it's a strong
  portability candidate, clearly better-positioned than toybox was --
  worth an actual spike to confirm, not just this desk analysis, but
  the primitives it would need are genuinely present.
- **User/password/group infrastructure**: see
  `users-and-privilege.md` -- real `/etc/passwd`/`/etc/group` file
  support, real `setuid()`/`seteuid()` POSIX transition semantics, a
  genuinely well-engineered PBKDF2-SHA256 password system. Not
  relevant to *compiling* most software, but relevant to anything that
  checks privileges or reads user info.

## Why libvterm and portable_cat/portable_wc "just worked"

Not luck -- structural. Both share a property toybox's `ls`/`cp`/`mv`
don't: they barely touch the OS at all.

- `portable_cat`/`portable_wc` were *designed* as portability smoke
  tests specifically -- `fopen`/`fgetc`/`getopt`, deliberately nothing
  more exotic, as a signal that NuttX's POSIX layer is real for
  ordinary, boring C.
- `libvterm` is a self-contained VT100/xterm state machine operating
  on buffers, calling back into caller-provided output functions. It
  doesn't fork, doesn't fork+exec, doesn't need process groups or
  symlinks, doesn't call `statfs()`, doesn't probe `__has_include` for
  platform detection with a fallback that assumes Linux. It's
  essentially pure ANSI C plus a tiny, generic I/O callback surface.

Toybox's applets, by contrast, are *precisely* the kind of code that
exercises every gap above at once: directory traversal assuming
symlinks exist, `portability.h`'s own platform-detection macros,
`struct statfs` field access, mount-table/xattr calls, and (for the
overall multicall binary, not any one applet) the `main()`-naming
convention collision covered elsewhere in this project's history. None
of that is a sign toybox is unusually broken -- it's a sign that
general-purpose Unix coreutils are, definitionally, deep in OS-surface
territory that a terminal emulator or a `wc` clone never has to touch.

**The practical predictor, going forward:** software's real NuttX
portability correlates far more with *how much OS surface it touches*
(process model, filesystem metadata, job control) than with how
"POSIX-compliant" it claims to be. A large, portable library that
mostly does math/parsing/protocol logic over buffers (libvterm, likely
zlib, likely a JSON parser) is a good bet. Anything doing real process
management, job control, or relying on `fork()`'s traditional semantics
is not, regardless of its own portability reputation elsewhere.

## Concrete recommendations, roughly in order of leverage

1. **Fix `-nostdinc` globally for `apps/external` builds**, not
   per-app. Highest-leverage single change available: every future
   port currently has to rediscover the `utmpx.h`/`__GLIBC__` leakage
   independently, the way toybox's did, twice.
2. **Build a shared, reusable compatibility-shim layer**, not a
   per-project `nuttx-shims/` reinvented each time. `paths.h`,
   a `utmpx.h` stub, and whatever else keeps recurring belong in one
   place a new port can just include, not be rediscovered.
3. **Write down the `fork()`-is-`vfork()` distinction somewhere
   prominent** (this doc, plus a pointer from wherever new porting
   work starts) -- likely the single most-impactful gap for "existing
   C ecosystem" compatibility specifically, since real fork()+exec()
   is an extremely common pattern in exactly the kind of software this
   project wants to run.
4. **A real `libcurl` spike** would meaningfully validate (or correct)
   the analysis above with actual evidence, given how much of its
   needs are already confirmed present.
5. Symlink support at the VFS layer is a bigger, separate undertaking
   (touches upstream NuttX's own filesystem code, not `apps/`) -- worth
   knowing it's the actual blocker next time something needs it,
   rather than re-diagnosing from scratch.
