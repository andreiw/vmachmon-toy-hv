#include "pvp.h"
#include "vmm.h"
#include "libfdt.h"

#define ANSI_CSI    "\33["
#define ANSI_BLACK  0
#define ANSI_RED    1
#define ANSI_GREEN  2
#define ANSI_YELLOW 3
#define ANSI_BLUE   4
#define ANSI_MGENTA 5
#define ANSI_CYAN   6
#define ANSI_WHITE  7
#define ANSI_FG(color)        ((color) + 30)
#define ANSI_BRIGHT_FG(color) ((color) + 90)
#define ANSI_BG(color)        ((color) + 40)
#define ANSI_NORMAL           0
#define ANSI_SET(color)       ANSI_CSI "%um", (color)
#define ANSI_RESET            ANSI_SET(ANSI_NORMAL)

static const char *
err_t_to_string(err_t code)
{
#define ERR_DEF(x, z) case x: { return z; } 

  if (code > ERR_INVALID) {
    code = ERR_INVALID;
  }
  switch(code) {
    ERR_LIST
  }
#undef ERR_DEF
}

/*
 * Print a log message.
 */
void
_log(unsigned level, unsigned log_lvl, unsigned error,
     char *pfx, char *fmt, ...)
{
  unsigned i;
  va_list ap;
  char buf[1024];
  FILE *f;

  unsigned colors[LOG_MAX] = {
    ANSI_BRIGHT_FG(ANSI_MGENTA),
    ANSI_BRIGHT_FG(ANSI_RED),
    ANSI_BRIGHT_FG(ANSI_RED),
    ANSI_BRIGHT_FG(ANSI_RED),
    ANSI_BRIGHT_FG(ANSI_RED),
    ANSI_BRIGHT_FG(ANSI_RED),    
    ANSI_BRIGHT_FG(ANSI_YELLOW),
    ANSI_BRIGHT_FG(ANSI_WHITE),
    ANSI_BRIGHT_FG(ANSI_GREEN),
    ANSI_BRIGHT_FG(ANSI_BLACK),
  };

  if (level > log_lvl &&
      level != LOG_ERROR &&
      level != LOG_FDT_ERROR &&
      level != LOG_MACH_ERROR &&
      level != LOG_POSIX_ERROR &&
      level != LOG_VMM_ERROR &&
      level != LOG_FATAL) {
    return;
  } else if (level > LOG_MAX) {
    level = LOG_MAX - 1;
  }

  if (level == LOG_ERROR ||
      level == LOG_FDT_ERROR ||
      level == LOG_MACH_ERROR ||
      level == LOG_POSIX_ERROR ||
      level == LOG_VMM_ERROR ||
      level == LOG_FATAL) {
    f = stderr;
    fflush(stdout);
  } else {
    f = stdout;
  }

  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  fprintf(f, ANSI_SET(ANSI_BG(ANSI_BLACK)));
  fprintf(f, ANSI_SET(ANSI_BRIGHT_FG(ANSI_BLACK)));
  fprintf(f, "[%s] ", pfx);
  fprintf(f, ANSI_SET(colors[level]));

  if (level == LOG_ERROR ||
      level == LOG_FDT_ERROR ||
      level == LOG_VMM_ERROR ||
      level == LOG_MACH_ERROR ||
      level == LOG_POSIX_ERROR) {
    const char *s;

    fprintf(f, ANSI_SET(ANSI_FG(ANSI_RED)));
    if (level == LOG_VMM_ERROR) {
      s = vmm_return_code_to_string(error);
    } else if (level == LOG_MACH_ERROR) {
      s = mach_error_string(error);
    } else if (level == LOG_POSIX_ERROR) {
      s = strerror(error);
    } else if (level == LOG_FDT_ERROR) {
      s = fdt_strerror(error);
    } else if (level == LOG_ERROR) {
      s = err_t_to_string(error);
    }
    fprintf(f, "%s (0x%x): ", s, error);
  }

  fprintf(f, ANSI_SET(colors[level]));      
  for (i = 0; i < ARRAY_LEN(buf) && buf[i] != '\0'; i++) {
    if (buf[i] == '\n') {
      fputc(buf[i], f);

      fprintf(f, ANSI_SET(ANSI_BRIGHT_FG(ANSI_BLACK)));
      fprintf(f, "[%s] ", pfx);
      fprintf(f, ANSI_SET(colors[level]));

      continue;
    }

    fputc(buf[i], f);
  }

  fprintf(f, ANSI_RESET);
  fputc('\n', f);

  if (level == LOG_FATAL) {
    fprintf(f, ANSI_SET(ANSI_BG(ANSI_RED)));
    fprintf(f, ANSI_SET(ANSI_FG(ANSI_BLACK)));
    fprintf(f, "Fatal error detected.");
    fprintf(f, ANSI_RESET);
    fputc('\n', f);
    exit(-1);
  }
}
