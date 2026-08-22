# Users, groups, and privilege separation in NuttX -- what's real

Research spike, not a design doc: what NuttX actually provides today,
checked directly against source (same standard as `vapor-api.md` and
`vaporterm.md`), so a later "let's add sudo/doas and home directories"
push starts from verified ground rather than re-discovering all of
this. Nothing here has been wired into vaporOS yet -- see OPEN
QUESTIONS at the end for what a real implementation still needs to
decide.

## The headline caveat, first

**`CONFIG_BUILD_FLAT=y` is our current, default build mode: the kernel
and every task/app share one address space, with no memory protection
between them at all.** This matters more than anything else in this
document: a memory-corruption bug in *any* app (network-facing or not)
can already read/write/execute anything the kernel can, regardless of
what UID it's running as. Every user/group/permission mechanism
described below is a layer *on top of* this -- meaningful for
structuring privilege and limiting what a *correctly-behaving* program
is allowed to do, but not a substitute for process isolation. NuttX
does support `CONFIG_BUILD_PROTECTED` (MPU-based kernel/user split) and
`CONFIG_BUILD_KERNEL` (full per-process address spaces via MMU) --
neither investigated yet; that's the more foundational question for
"safe to expose to a network," and shouldn't be assumed solved by
adding users.

## Credential tracking: real, correct, off by default

`CONFIG_SCHED_USER_IDENTITY` (default `n`) adds real per-task-group
fields -- `tg_uid`/`tg_gid` (real), `tg_euid`/`tg_egid` (effective),
`tg_suid`/`tg_sgid` (saved-set) -- in `struct task_group_s`
(`include/nuttx/sched.h`). Child tasks inherit all of them from the
parent. Without this option, `getuid()`/`setuid()`/etc. are stub,
root-only versions.

`setuid()`'s actual implementation (`sched/group/group_setuid.c`) is
correct, real POSIX privilege-transition logic, checked directly: if
already root (`euid == 0`), you can become anyone. If not root, you
can only move your *effective* UID to your own real or saved UID (no
arbitrary escalation). `seteuid()` (`sched/group/group_seteuid.c`)
follows the same standard rules. This part is solid, working
infrastructure, not a stub.

## File permission enforcement: the infrastructure exists, but nothing calls it

`fs_checkmode()` (`fs/inode/fs_inode.c`, gated by `CONFIG_FS_PERMISSION`)
is a complete, correct owner/group/other Unix permission check against
a task's effective UID/GID. Checked directly: **it has exactly one
reference anywhere in the NuttX tree, and that's its own header
declaration.** Nothing calls it. `CONFIG_FS_PERMISSION`'s own Kconfig
help text is honest about this: *"This option alone does not enforce
runtime permission checks."* It's also scoped to pseudo-filesystem
inodes specifically (in-memory driver nodes like `/dev/`), not real
files on a mounted filesystem like ROMFS/hostfs.

**Practical consequence:** today, distinct UIDs don't actually stop one
task from reading/writing/executing a file another task could. The
credential-tracking and privilege-transition machinery is real; the
enforcement that would make it matter for file access isn't wired up
anywhere upstream, as of this pinned NuttX commit.

## A real user database: `/etc/passwd` and `/etc/group`

`CONFIG_LIBC_PASSWD_FILE` (default `n`) makes `getpwnam()`/`getpwuid()`/
`getpwent()` read a real file (default path
`CONFIG_LIBC_PASSWD_FILEPATH`, default `/etc/passwd`). Format, checked
directly against the actual parser
(`libs/libc/pwd/lib_find_pwdfile.c`), is
`user:x:uid:gid:gecos:home` (six fields -- the Kconfig help text
describes five and omits gecos, a real doc/code mismatch worth knowing
about). `pw_dir` comes genuinely from the file -- this is the real
mechanism for "where is this user's home directory."

Two things it does *not* do:
- `pw_shell` and `pw_passwd` are always hardcoded to the same
  root-stub values (`/bin/nsh`, a placeholder), regardless of what's
  actually in the file -- there's no per-user shell field in this
  format.
- It only *looks up* `pw_dir`; it never creates the directory. Nothing
  in libc does. Home directory creation is fully our own work (an
  rcS-style boot step, most likely: enumerate `/etc/passwd` via
  `getpwent()`, `mkdir()` anything missing).

`CONFIG_LIBC_GROUP_FILE` mirrors this for `/etc/group`
(`group:x:gid:users`) via `getgrnam()`/`getgrgid()`. Per
`nsh_is_privileged()`'s own comment (see below): NuttX has no
supplementary-group support -- one primary GID per process, not a list.

## Password verification: two separate systems, compatible on one file

- **libc's `crypt()`** (`libs/libc/unistd/lib_crypt_r.c`): real MD5-crypt
  (`$1$salt$hash`), but requires `/dev/crypto` to be configured and
  present -- not a pure software fallback.
- **`apps/fsutils/passwd`** (`passwd_verify()`/`passwd_find()`): a
  separate, more modern system -- PBKDF2-HMAC-SHA256, salted,
  configurable iteration count, `$pbkdf2-sha256$iter$salt$hash` format,
  constant-time comparison (`timingsafe_bcmp()`). Genuinely
  well-engineered, not a toy. Same default file path as libc's
  (`/etc/passwd`), and compatible with it in practice: `getpwnam()`
  never inspects the password field's format, so both systems can read
  the same file without conflict as long as PBKDF2-formatted hashes go
  in that field.

## NSH already has a complete, working reference implementation

`apps/nshlib/nsh_identity.c` and `nsh_login.c` -- gated behind
`CONFIG_NSH_LOGIN`/`CONFIG_NSH_LOGIN_PASSWD`/`CONFIG_NSH_LOGIN_SETUID`,
none currently enabled for vaporOS -- implement, for real:

- `nsh_login()`: username/password prompt, ECHO disabled for the
  password field, configurable retry count and fail-delay
  (brute-force rate limiting), calls `passwd_verify()`.
- `cmd_su` (`su [username]`): already-privileged callers switch without
  a password; anyone can switch to their own identity without one;
  switching to someone else requires their password. Correctly leaves
  real UID at 0 when NSH itself started privileged, so a later `su`
  can `seteuid(0)` back -- i.e. root staying "available" via saved/real
  ID even while running as another effective user.
- `cmd_id` / `cmd_whoami`: `id`(1)-style output (real/effective/saved
  UID/GID, group), via `getresuid()`/`getresgid()`.

None of this is directly reusable by `vaporshell` (same `nsh_vtbl_s`
coupling and non-spawnability as every other NSH command discussed in
`vaporOS-coreutils`'s own `nsh-ports/`), but `nsh_switch_credentials()`
and `cmd_su` specifically are a complete, correct, directly-portable
model for a vaporOS `su` -- and, by extension, a `sudo`/`doas`-style
"run one command elevated" tool built the same way.

## Sketch: what `sudo`/`doas` would actually involve

Not built yet -- for when this becomes a real task, following the
`nsh-ports/` pattern (modeled on NSH's own `cmd_su`, adapted the same
way `poweroff` was: strip the `nsh_vtbl_s` dependency, keep the actual
logic):

1. Prompt for the *caller's own* password (not the target user's --
   this is the `sudo` vs. `su` distinction), disabling terminal echo,
   the same way `nsh_read_password()` already does it.
2. Verify via `passwd_verify()`.
3. `seteuid(0)`/`setegid(0)` (only valid if real UID is still 0 --
   see `nsh_switch_credentials()`'s own comment on why NSH preserves
   real-root even while running as another effective user).
4. Run the requested command, then this process exits -- no lingering
   elevated shell, unlike `su`.

Given the file-permission-enforcement gap above, this would be
meaningful today mainly as: (a) a real convention/habit even before
enforcement exists, (b) future-proofing for whenever `fs_checkmode()`
does get wired up (by us or upstream), and (c) more concretely right
now -- a way to run a specific *service* (e.g. something network-facing)
as a separate, unprivileged user, so that a bug in that one service
doesn't hand over an already-root shell. That last case doesn't
actually need file-permission enforcement to matter: it's really about
whichever *that service* itself checks (e.g. `geteuid() == 0` before
doing something sensitive), which is something we control regardless
of whether the generic VFS-level enforcement ever lands.

## Open questions

- Process/memory isolation (`CONFIG_BUILD_PROTECTED`/`CONFIG_BUILD_KERNEL`)
  not investigated at all yet -- the more foundational question for
  actual network-exposed safety, independent of everything above.
- Would we ever wire up `fs_checkmode()` ourselves (touches upstream,
  pinned NuttX itself, not `apps/`), propose it upstream, or just accept
  the gap and rely on service-level self-checks instead?
- Home directory creation: rcS-style boot step, reading `/etc/passwd`
  via `getpwent()`, `mkdir()`-ing anything missing -- not built.
- `/etc/vaporshell/vsh.conf` (system-wide default config, from the
  history-file work) and per-user `$HOME`-based overrides were designed
  around "no real home directories yet" -- revisit once this exists.
- Single primary GID only (no supplementary groups) is a real NuttX
  limitation, not something we can design around locally.
