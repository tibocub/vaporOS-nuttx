/* compat_scan_main.c -- triage a C source tree for known vaporOS/NuttX
 * portability gaps, before spending time on a real build attempt.
 *
 *   compat-scan [--severity hard-blocker|needs-review|informational] <path>
 *
 * Every check here maps to a specific, verified finding in
 * vaporOS-nuttx's own docs/c-posix-compatibility.md -- read that for
 * the full rationale and evidence behind each one. This tool doesn't
 * re-derive anything; it just makes those findings greppable against
 * a new codebase.
 *
 * WHAT THIS IS: a cheap, static triage step -- plain text matching
 * over source, no parsing, no build. Meant to roughly tell how much
 * work porting a lib or a program might take before investing time.
 *
 * WHAT THIS ISN'T: a verdict. It cannot tell whether a fork() call
 * is immediately followed by exec()/_exit() (the one usage NuttX's
 * fork() actually allows) versus doing arbitrary child-process work
 * (undefined behavior on NuttX) -- that needs to actually read the
 * call site. False positives are expected and fine; the point is
 * narrowing down what to read, not replacing reading it.
 *
 * Deliberately dependency-free, portable C (no regex.h -- a small,
 * purpose-built matcher instead, since every check here is either a
 * whole-identifier match, an identifier-immediately-followed-by-'('
 * match, or a plain substring match; nothing needs a real regex
 * engine's power). Builds and runs identically as a plain Linux
 * binary (`cc -o compat-scan compat_scan_main.c`) and as a
 * NuttX/vaporOS app (see this directory's own Makefile/Kconfig)
 * -- same source file either way, on purpose: the whole point is
 * running the same triage tool on the device itself, not just on
 * a dev machine.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#  define PATH_MAX 1024
#endif

/****************************************************************************
 * Color output
 ****************************************************************************/

#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"  /* closest ANSI standard color to "orange" */
#define COLOR_BOLD    "\033[1m"
#define COLOR_RESET   "\033[0m"

static int g_use_color;

static void set_color(const char *code)
{
  if (g_use_color)
    {
      fputs(code, stdout);
    }
}

static void reset_color(void)
{
  if (g_use_color)
    {
      fputs(COLOR_RESET, stdout);
    }
}

/****************************************************************************
 * Severity levels
 ****************************************************************************/

enum severity_e
{
  SEV_HARD = 0,   /* confirmed absent/fundamentally different; needs a
                   * rewrite, not a port */
  SEV_REVIEW = 1, /* may work, may not -- read the actual call site */
  SEV_INFO = 2    /* confirmed present and working; just worth knowing
                   * it's there */
};

static const char *severity_name(enum severity_e sev)
{
  switch (sev)
    {
      case SEV_HARD:   return "HARD-BLOCKER";
      case SEV_REVIEW: return "NEEDS-REVIEW";
      default:         return "INFORMATIONAL";
    }
}

static const char *severity_color(enum severity_e sev)
{
  switch (sev)
    {
      case SEV_HARD:   return COLOR_RED;
      case SEV_REVIEW: return COLOR_YELLOW;
      default:         return COLOR_RESET;
    }
}

/****************************************************************************
 * Pattern matching -- small, purpose-built, no regex.h
 ****************************************************************************/

enum match_kind_e
{
  MATCH_CALL,        /* whole identifier, followed by optional whitespace
                       * then '(' -- e.g. "fork(" but not "forklift(" */
  MATCH_IDENT,       /* whole identifier, no call required -- e.g. macros
                       * and flags like __GLIBC__, WUNTRACED */
  MATCH_SUBSTRING,   /* plain substring, no word-boundary checks */
  MATCH_INCLUDE,     /* an actual #include directive naming this header --
                       * not just the header's name appearing in a comment
                       * (e.g. one already explaining why it's absent) */
  MATCH_PREFIX_CALL, /* any whole identifier *starting with* this prefix,
                       * followed by '(' -- for pthread_* as a family */
  MATCH_ENVIRON_ASSIGN /* special-cased: whole word "environ" followed by
                         * '=' but not "==" */
};

struct pattern_s
{
  const char *text;
  enum match_kind_e kind;
};

struct check_s
{
  const char *check_id;
  enum severity_e severity;
  const char *note;
  const struct pattern_s *patterns; /* terminated by {NULL, 0} */
};

static int is_ident_char(char c)
{
  return isalnum((unsigned char)c) || c == '_';
}

/* Finds `word` as a whole identifier anywhere in `line`. Returns a
 * pointer to the match, or NULL. Word-boundary means the characters
 * immediately before and after (if any) are not themselves identifier
 * characters -- so searching for "fork" in "forklift" correctly
 * fails, but "x = fork();" succeeds.
 */

static const char *find_whole_word(const char *line, const char *word)
{
  size_t wordlen = strlen(word);
  const char *p = line;
  const char *hit;

  while ((hit = strstr(p, word)) != NULL)
    {
      int before_ok = (hit == line) || !is_ident_char(hit[-1]);
      int after_ok = !is_ident_char(hit[wordlen]);

      if (before_ok && after_ok)
        {
          return hit;
        }

      p = hit + 1;
    }

  return NULL;
}

/* Like find_whole_word(), but additionally requires that the first
 * non-whitespace character after the word is '(' -- i.e. it's being
 * called/invoked, not just referenced.
 */

static const char *find_call(const char *line, const char *word)
{
  size_t wordlen = strlen(word);
  const char *hit = find_whole_word(line, word);
  const char *after;

  if (hit == NULL)
    {
      return NULL;
    }

  after = hit + wordlen;
  while (*after == ' ' || *after == '\t')
    {
      after++;
    }

  return (*after == '(') ? hit : NULL;
}

/* Finds any whole identifier in `line` that *starts with* `prefix` and
 * is immediately followed by '(' (after optional whitespace) -- used
 * for matching an entire function family (pthread_create, pthread_
 * join, ...) without listing every member individually.
 */

static const char *find_prefix_call(const char *line, const char *prefix)
{
  size_t prefixlen = strlen(prefix);
  const char *p = line;

  while (*p != '\0')
    {
      if (!is_ident_char(*p) || (p != line && is_ident_char(p[-1])))
        {
          p++;
          continue;
        }

      /* p is the start of an identifier */

      if (strncmp(p, prefix, prefixlen) == 0)
        {
          const char *after = p;
          while (is_ident_char(*after))
            {
              after++;
            }

          {
            const char *scan = after;
            while (*scan == ' ' || *scan == '\t')
              {
                scan++;
              }

            if (*scan == '(')
              {
                return p;
              }
          }
        }

      while (is_ident_char(*p))
        {
          p++;
        }
    }

  return NULL;
}

/* environ = ... (but not environ == ...). A line can contain more
 * than one whole-word "environ" (confirmed directly: a real comment
 * in this codebase reads "sched/environ/ -- ... 'environ = new_array'"
 * -- the first occurrence isn't followed by '=', but the second is),
 * so this has to retry past a non-qualifying match rather than
 * stopping at the first whole-word hit the way a simpler check could
 * get away with.
 */

static const char *find_environ_assign(const char *line)
{
  const char *search_from = line;
  const char *hit;

  while ((hit = find_whole_word(search_from, "environ")) != NULL)
    {
      const char *after = hit + strlen("environ");

      while (*after == ' ' || *after == '\t')
        {
          after++;
        }

      if (*after == '=' && after[1] != '=')
        {
          return hit;
        }

      search_from = hit + strlen("environ");
    }

  return NULL;
}

/* An actual #include directive naming this header -- requires the
 * line to genuinely start with '#' (the caller already trims leading
 * whitespace before calling this), then "include", before the header
 * name is searched for. Excludes comments that merely *mention* a
 * header's name (e.g. one already explaining why it's absent).
 */

static const char *find_include(const char *line, const char *header)
{
  const char *p = line;

  if (*p != '#')
    {
      return NULL;
    }

  p++;
  while (*p == ' ' || *p == '\t')
    {
      p++;
    }

  if (strncmp(p, "include", 7) != 0)
    {
      return NULL;
    }

  return strstr(p + 7, header);
}

static const char *match_pattern(const char *line, const struct pattern_s *pat)
{
  switch (pat->kind)
    {
      case MATCH_CALL:
        return find_call(line, pat->text);
      case MATCH_IDENT:
        return find_whole_word(line, pat->text);
      case MATCH_SUBSTRING:
        return strstr(line, pat->text);
      case MATCH_INCLUDE:
        return find_include(line, pat->text);
      case MATCH_PREFIX_CALL:
        return find_prefix_call(line, pat->text);
      case MATCH_ENVIRON_ASSIGN:
        return find_environ_assign(line);
      default:
        return NULL;
    }
}

/****************************************************************************
 * The check table
 ****************************************************************************/

static const struct pattern_s pat_fork[] =
{
  { "fork", MATCH_CALL },
  { NULL, 0 }
};

static const struct pattern_s pat_procgroups[] =
{
  { "setpgid", MATCH_CALL },
  { "getpgid", MATCH_CALL },
  { "tcsetpgrp", MATCH_CALL },
  { "tcgetpgrp", MATCH_CALL },
  { "setpgrp", MATCH_CALL },
  { NULL, 0 }
};

static const struct pattern_s pat_jobsignals[] =
{
  { "SIGTSTP", MATCH_IDENT },
  { "SIGCONT", MATCH_IDENT },
  { "SIGTTIN", MATCH_IDENT },
  { "SIGTTOU", MATCH_IDENT },
  { NULL, 0 }
};

static const struct pattern_s pat_waitflags[] =
{
  { "WUNTRACED", MATCH_IDENT },
  { "WCONTINUED", MATCH_IDENT },
  { NULL, 0 }
};

static const struct pattern_s pat_symlink[] =
{
  { "symlink", MATCH_CALL },
  { "readlink", MATCH_CALL },
  { NULL, 0 }
};

static const struct pattern_s pat_hasinclude[] =
{
  { "__has_include", MATCH_CALL },
  { NULL, 0 }
};

static const struct pattern_s pat_glibclinux[] =
{
  { "__GLIBC__", MATCH_IDENT },
  { "__linux__", MATCH_IDENT },
  { "__gnu_linux__", MATCH_IDENT },
  { NULL, 0 }
};

static const struct pattern_s pat_pathsh[] =
{
  { "paths.h", MATCH_INCLUDE },
  { NULL, 0 }
};

static const struct pattern_s pat_utmpxh[] =
{
  { "utmpx.h", MATCH_INCLUDE },
  { NULL, 0 }
};

static const struct pattern_s pat_xattr[] =
{
  { "sys/xattr.h", MATCH_INCLUDE },
  { "attr/xattr.h", MATCH_INCLUDE },
  { "getxattr", MATCH_CALL },
  { "setxattr", MATCH_CALL },
  { "listxattr", MATCH_CALL },
  { "removexattr", MATCH_CALL },
  { NULL, 0 }
};

static const struct pattern_s pat_inotify[] =
{
  { "sys/inotify.h", MATCH_INCLUDE },
  { "inotify_init", MATCH_CALL },
  { "inotify_add_watch", MATCH_CALL },
  { "inotify_rm_watch", MATCH_CALL },
  { NULL, 0 }
};

static const struct pattern_s pat_mntent[] =
{
  { "mntent.h", MATCH_INCLUDE },
  { "setmntent", MATCH_CALL },
  { "getmntent", MATCH_CALL },
  { "endmntent", MATCH_CALL },
  { NULL, 0 }
};

static const struct pattern_s pat_chroot[] =
{
  { "chroot", MATCH_CALL },
  { NULL, 0 }
};

static const struct pattern_s pat_syscall[] =
{
  { "syscall", MATCH_CALL },
  { NULL, 0 }
};

static const struct pattern_s pat_frsize[] =
{
  { "f_frsize", MATCH_IDENT },
  { NULL, 0 }
};

static const struct pattern_s pat_environ[] =
{
  { "", MATCH_ENVIRON_ASSIGN },
  { NULL, 0 }
};

static const struct pattern_s pat_dlopen[] =
{
  { "dlopen", MATCH_CALL },
  { NULL, 0 }
};

static const struct pattern_s pat_pthread[] =
{
  { "pthread_", MATCH_PREFIX_CALL },
  { NULL, 0 }
};

static const struct pattern_s pat_sockets[] =
{
  { "socket", MATCH_CALL },
  { "getaddrinfo", MATCH_CALL },
  { "getnameinfo", MATCH_CALL },
  { NULL, 0 }
};

static const struct pattern_s pat_mmap[] =
{
  { "mmap", MATCH_CALL },
  { NULL, 0 }
};

static const struct check_s CHECKS[] =
{
  {
    "fork-call", SEV_HARD,
    "NuttX's fork() is real only for the immediate 'set a pid_t, "
    "return, or exec()/_exit() -- nothing else' pattern (its own doc "
    "comment says so explicitly); anything else in the child is "
    "undefined behavior. Read this call site: if the child does real "
    "work before exec/_exit, this needs a rewrite against "
    "posix_spawn, not a portability shim.",
    pat_fork
  },
  {
    "process-groups", SEV_HARD,
    "Process groups are stubbed on NuttX -- a group always has "
    "exactly one member, equal to its own PID (lib_setpgid.c's own "
    "comment). Real job control (Ctrl+Z suspending a pipeline as a "
    "unit, fg/bg) cannot work as written.",
    pat_procgroups
  },
  {
    "job-control-signals", SEV_HARD,
    "Job-control signals -- depends on process groups (see above), "
    "which don't really exist here.",
    pat_jobsignals
  },
  {
    "wait-job-control-flags", SEV_HARD,
    "waitpid() flags specifically for job-control stop/continue "
    "tracking -- same underlying gap as process groups.",
    pat_waitflags
  },
  {
    "symlink-api", SEV_HARD,
    "No mounted filesystem in this NuttX version implements symlinks "
    "at the VFS layer at all (struct mountpt_operations has no "
    "symlink/link fields) -- confirmed directly, not hostfs-specific.",
    pat_symlink
  },
  {
    "has-include-probe", SEV_REVIEW,
    "Feature-detection via __has_include can be fooled by leaked "
    "host headers on a badly-configured build (vaporOS-nuttx's own "
    "-nostdinc fix addresses the general case) -- but even with that "
    "fixed, check what each probed header actually gates: toybox's "
    "own utmpx.h probe assumed 'reachable == usable', which wasn't a "
    "safe assumption even in principle.",
    pat_hasinclude
  },
  {
    "glibc-linux-macros", SEV_REVIEW,
    "Conditional code gated on glibc/Linux specifically -- check "
    "which branch actually runs on NuttX (there's often a generic "
    "POSIX fallback; confirm it's correct, not just present).",
    pat_glibclinux
  },
  {
    "paths-h", SEV_REVIEW,
    "paths.h (_PATH_DEFPATH and friends) doesn't exist anywhere in "
    "NuttX. Needs a small compatibility header providing whatever "
    "subset of _PATH_* macros the code actually references.",
    pat_pathsh
  },
  {
    "utmpx-h", SEV_REVIEW,
    "utmpx.h doesn't exist in NuttX at all.",
    pat_utmpxh
  },
  {
    "xattr", SEV_REVIEW,
    "xattr support doesn't exist on NuttX (toybox's own cp -p/-a "
    "xattr preservation was stubbed for this reason).",
    pat_xattr
  },
  {
    "inotify", SEV_REVIEW,
    "No file-watching (inotify-equivalent) support on NuttX.",
    pat_inotify
  },
  {
    "mount-table-introspection", SEV_REVIEW,
    "No mount-table introspection API on NuttX -- code enumerating "
    "mounted filesystems this way has nothing to call.",
    pat_mntent
  },
  {
    "chroot-call", SEV_REVIEW,
    "chroot() is confirmed absent on NuttX.",
    pat_chroot
  },
  {
    "raw-syscall", SEV_REVIEW,
    "Raw syscall() as an escape hatch is confirmed absent/"
    "unsupported on NuttX -- whatever this was working around needs "
    "a different, NuttX-specific answer.",
    pat_syscall
  },
  {
    "statfs-frsize", SEV_REVIEW,
    "NuttX's struct statfs has no f_frsize field at all, unlike "
    "Linux -- needs a NuttX-specific case using f_bsize for both (the "
    "pre-f_frsize Unix convention), same fix toybox's portability.h "
    "needed.",
    pat_frsize
  },
  {
    "environ-replace", SEV_REVIEW,
    "NuttX has no whole-environ-array replacement primitive (only "
    "individual setenv/unsetenv/clearenv) -- code assuming a "
    "glibc/BSD-style 'hand me a new environ' needs rewriting against "
    "the individual calls.",
    pat_environ
  },
  {
    "dlopen-usage", SEV_REVIEW,
    "dlopen() is real (literally NuttX's own ELF-loading mechanism "
    "under a POSIX name) but limited: no automatic symbol binding "
    "across libraries, no dependency resolution, mode is ignored, and "
    "libelf_setsymtab() must already have been called by the "
    "application. Fine for 'load this one ELF, look up symbols in "
    "it'; not fine for anything assuming full dynamic-linker "
    "behavior.",
    pat_dlopen
  },
  {
    "pthread-usage", SEV_INFO,
    "Real pthread support (96 real implementation files, not stubs) "
    "-- not a gap, just noted for context.",
    pat_pthread
  },
  {
    "sockets-dns", SEV_INFO,
    "Real BSD sockets + DNS resolution -- not a gap. This is the "
    "primitive set libcurl mostly needs, and it's solid here.",
    pat_sockets
  },
  {
    "mmap-usage", SEV_INFO,
    "mmap() exists, but vaporOS-nuttx hasn't verified its real "
    "semantics in a CONFIG_BUILD_FLAT build in depth -- worth a "
    "closer look if the code relies on copy-on-write or file-backed "
    "mapping semantics specifically, not just noted as a known-good "
    "primitive the way sockets/pthreads are.",
    pat_mmap
  }
};

#define NUM_CHECKS (sizeof(CHECKS) / sizeof(CHECKS[0]))
#define MAX_SHOWN_PER_CHECK 10

/****************************************************************************
 * Findings storage -- a simple growable array per check
 ****************************************************************************/

struct finding_s
{
  char *path;
  int lineno;
  char *line_text;
};

struct check_results_s
{
  struct finding_s *findings;
  size_t count;
  size_t capacity;
};

static struct check_results_s g_results[NUM_CHECKS];
static size_t g_files_with_findings;

static char *xstrdup(const char *s)
{
  size_t len = strlen(s) + 1;
  char *copy = malloc(len);
  if (copy != NULL)
    {
      memcpy(copy, s, len);
    }

  return copy;
}

static void record_finding(size_t check_idx, const char *path, int lineno,
                            const char *line_text)
{
  struct check_results_s *res = &g_results[check_idx];

  if (res->count == res->capacity)
    {
      size_t newcap = (res->capacity == 0) ? 8 : res->capacity * 2;
      struct finding_s *newarr =
          realloc(res->findings, newcap * sizeof(struct finding_s));

      if (newarr == NULL)
        {
          return; /* drop the finding rather than crash a triage tool */
        }

      res->findings = newarr;
      res->capacity = newcap;
    }

  res->findings[res->count].path = xstrdup(path);
  res->findings[res->count].lineno = lineno;
  res->findings[res->count].line_text = xstrdup(line_text);
  res->count++;
}

/****************************************************************************
 * File scanning
 ****************************************************************************/

#define MAX_LINE_LEN 2048
#define MAX_FILE_SIZE (8 * 1024 * 1024)

static void strip_trailing_newline(char *s)
{
  size_t len = strlen(s);
  while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
    {
      s[--len] = '\0';
    }
}

static void trim_leading_space(char **s)
{
  while (**s == ' ' || **s == '\t')
    {
      (*s)++;
    }
}

static void scan_file(const char *path)
{
  FILE *fp;
  char line[MAX_LINE_LEN];
  int lineno = 0;
  int file_has_finding = 0;
  struct stat st;

  if (stat(path, &st) == 0 && st.st_size > MAX_FILE_SIZE)
    {
      return; /* not real C source at this size; skip rather than
               * risk pathological memory/time on something that
               * isn't the intended input anyway */
    }

  fp = fopen(path, "r");
  if (fp == NULL)
    {
      return;
    }

  while (fgets(line, sizeof(line), fp) != NULL)
    {
      size_t i;

      lineno++;
      strip_trailing_newline(line);

      for (i = 0; i < NUM_CHECKS; i++)
        {
          const struct pattern_s *pat;

          for (pat = CHECKS[i].patterns; pat->kind != 0 || pat->text != NULL;
               pat++)
            {
              char *trimmed = line;

              trim_leading_space(&trimmed);

              if (match_pattern(trimmed, pat) != NULL)
                {
                  record_finding(i, path, lineno, trimmed);
                  file_has_finding = 1;
                  break; /* one match per check per line is enough */
                }
            }
        }
    }

  fclose(fp);

  if (file_has_finding)
    {
      g_files_with_findings++;
    }
}

static int has_source_suffix(const char *name)
{
  static const char *suffixes[] =
  {
    ".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", NULL
  };
  size_t namelen = strlen(name);
  int i;

  for (i = 0; suffixes[i] != NULL; i++)
    {
      size_t suflen = strlen(suffixes[i]);
      if (namelen > suflen &&
          strcmp(name + namelen - suflen, suffixes[i]) == 0)
        {
          return 1;
        }
    }

  return 0;
}

static int compare_strings(const void *a, const void *b)
{
  const char *sa = *(const char * const *)a;
  const char *sb = *(const char * const *)b;
  return strcmp(sa, sb);
}

static void walk_tree(const char *path)
{
  struct stat st;
  DIR *dir;
  struct dirent *entry;
  char **names = NULL;
  size_t count = 0;
  size_t capacity = 0;
  size_t i;

  if (stat(path, &st) != 0)
    {
      fprintf(stderr, "compat-scan: cannot stat %s\n", path);
      return;
    }

  if (S_ISREG(st.st_mode))
    {
      scan_file(path);
      return;
    }

  if (!S_ISDIR(st.st_mode))
    {
      return; /* device nodes, fifos, etc -- not source, skip quietly */
    }

  dir = opendir(path);
  if (dir == NULL)
    {
      fprintf(stderr, "compat-scan: cannot open directory %s\n", path);
      return;
    }

  /* Collect entry names first and sort them, rather than acting on
   * readdir()'s own (unspecified, filesystem-dependent) order --
   * makes two runs over the same tree produce the same report, which
   * matters for diffing output or reporting a result to someone else.
   */

  while ((entry = readdir(dir)) != NULL)
    {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
          continue;
        }

      if (count == capacity)
        {
          size_t newcap = (capacity == 0) ? 16 : capacity * 2;
          char **newarr = realloc(names, newcap * sizeof(char *));

          if (newarr == NULL)
            {
              break; /* best effort -- scan what we already collected */
            }

          names = newarr;
          capacity = newcap;
        }

      names[count] = xstrdup(entry->d_name);
      if (names[count] != NULL)
        {
          count++;
        }
    }

  closedir(dir);

  if (count > 0)
    {
      qsort(names, count, sizeof(char *), compare_strings);
    }

  for (i = 0; i < count; i++)
    {
      char childpath[PATH_MAX];

      snprintf(childpath, sizeof(childpath), "%s/%s", path, names[i]);

      if (stat(childpath, &st) != 0)
        {
          free(names[i]);
          continue;
        }

      if (S_ISDIR(st.st_mode))
        {
          walk_tree(childpath);
        }
      else if (S_ISREG(st.st_mode) && has_source_suffix(names[i]))
        {
          scan_file(childpath);
        }

      free(names[i]);
    }

  free(names);
}

/****************************************************************************
 * Reporting
 ****************************************************************************/

static void print_wrapped_note(const char *note)
{
  /* Simple, dependency-free wrap at ~72 columns so the note reads
   * reasonably in a normal terminal without pulling in anything
   * beyond what's already used here.
   */

  const char *p = note;
  size_t linelen = strlen(note);
  const size_t width = 72;

  printf("  ");
  if (linelen <= width)
    {
      printf("%s\n", note);
      return;
    }

  while (*p != '\0')
    {
      size_t remaining = strlen(p);
      size_t take = remaining;
      const char *breakpoint;

      if (remaining > width)
        {
          take = width;
          breakpoint = p + width;
          while (breakpoint > p && *breakpoint != ' ')
            {
              breakpoint--;
            }

          if (breakpoint > p)
            {
              take = (size_t)(breakpoint - p);
            }
        }

      printf("%.*s\n", (int)take, p);
      p += take;
      while (*p == ' ')
        {
          p++;
        }

      if (*p != '\0')
        {
          printf("  ");
        }
    }
}

static int severity_at_least(enum severity_e sev, enum severity_e min_level)
{
  return sev <= min_level;
}

static void print_report(enum severity_e min_level)
{
  size_t i;
  int any_shown = 0;
  enum severity_e sev;

  for (i = 0; i < NUM_CHECKS; i++)
    {
      if (g_results[i].count > 0)
        {
          any_shown = 1;
          break;
        }
    }

  if (!any_shown)
    {
      set_color(COLOR_GREEN);
      printf("No matches for any known gap pattern -- see this tool's own\n"
             "top-of-file comment for what that does and doesn't mean.\n");
      reset_color();
      return;
    }

  for (sev = SEV_HARD; sev <= SEV_INFO; sev++)
    {
      int section_has_content = 0;

      if (!severity_at_least(sev, min_level))
        {
          continue;
        }

      for (i = 0; i < NUM_CHECKS; i++)
        {
          if (CHECKS[i].severity == sev && g_results[i].count > 0)
            {
              section_has_content = 1;
              break;
            }
        }

      if (!section_has_content)
        {
          continue;
        }

      printf("\n");
      set_color(COLOR_BOLD);
      set_color(severity_color(sev));
      printf("============================================================\n");
      printf("%s\n", severity_name(sev));
      printf("============================================================\n");
      reset_color();

      for (i = 0; i < NUM_CHECKS; i++)
        {
          size_t j;

          if (CHECKS[i].severity != sev || g_results[i].count == 0)
            {
              continue;
            }

          printf("\n[%s] %zu match(es)\n", CHECKS[i].check_id,
                 g_results[i].count);
          print_wrapped_note(CHECKS[i].note);

          for (j = 0; j < g_results[i].count && j < MAX_SHOWN_PER_CHECK; j++)
            {
              printf("    %s:%d: %s\n", g_results[i].findings[j].path,
                     g_results[i].findings[j].lineno,
                     g_results[i].findings[j].line_text);
            }

          if (g_results[i].count > MAX_SHOWN_PER_CHECK)
            {
              printf("    ... and %zu more\n",
                     g_results[i].count - MAX_SHOWN_PER_CHECK);
            }
        }
    }

  printf("\n%zu file(s) with at least one match.\n", g_files_with_findings);
  printf("Read each hard-blocker call site directly before assuming it's "
         "fatal --\n");
  printf("this tool flags patterns, not verdicts.\n");
}

/****************************************************************************
 * main
 ****************************************************************************/

static void usage(const char *prog)
{
  fprintf(stderr,
          "usage: %s [--severity hard-blocker|needs-review|informational] "
          "<path>\n"
          "\n"
          "Triage a C source tree for known vaporOS/NuttX portability "
          "gaps.\n",
          prog);
}

int main(int argc, char *argv[])
{
  const char *path = NULL;
  enum severity_e min_level = SEV_INFO;
  int i;

  for (i = 1; i < argc; i++)
    {
      if (strcmp(argv[i], "--severity") == 0 && i + 1 < argc)
        {
          i++;
          if (strcmp(argv[i], "hard-blocker") == 0)
            {
              min_level = SEV_HARD;
            }
          else if (strcmp(argv[i], "needs-review") == 0)
            {
              min_level = SEV_REVIEW;
            }
          else if (strcmp(argv[i], "informational") == 0)
            {
              min_level = SEV_INFO;
            }
          else
            {
              fprintf(stderr, "compat-scan: unknown severity '%s'\n",
                      argv[i]);
              usage(argv[0]);
              return 1;
            }
        }
      else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
          usage(argv[0]);
          return 0;
        }
      else if (path == NULL)
        {
          path = argv[i];
        }
      else
        {
          usage(argv[0]);
          return 1;
        }
    }

  if (path == NULL)
    {
      usage(argv[0]);
      return 1;
    }

  g_use_color = isatty(fileno(stdout));

  walk_tree(path);
  print_report(min_level);

  return 0;
}
