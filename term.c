#define LOG_PFX TERM
#include "term.h"
#include <fcntl.h>

static int fd;

err_t
term_init(const char *con_path)
{
  const char *banner =
    "\r\n\nThis is the PVP console\r\n"
        "-----------------------\r\n\n";

  fd = open(con_path, O_NONBLOCK | O_RDWR);
  if (fd < 0) {
    POSIX_ERROR(errno, "couldn't open '%s' for console", con_path);
    return ERR_POSIX;
  }

  term_out(banner, strlen(banner));

  return ERR_NONE;
}

void
term_out(const char *buf,
         length_t len)
{
  int written;

  while (len != 0) {
    written = write(fd, buf, len);
    if (written == -1) {
      POSIX_ERROR(written, "term write");
    }

    len -= written;
    buf += written;
  }
}
