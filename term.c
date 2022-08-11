#define LOG_PFX TERM
#include "term.h"
#include "socket.h"

#define PORT 7000

static socket_t s;

static void
term_on_connect(socket_t *s)
{
  const char *banner =
    "\nThis is the PVP console\r\n"
    "-----------------------\r\n\n";
  socket_out(s, banner, strlen(banner));
}

static void
term_on_disconnect(socket_t *s)
{
  const char *banner =
    "\r\n\nPVP console closing...\r\n";

  socket_out(s, banner, strlen(banner));
}

err_t
term_init(void)
{
  err_t err;

  s.port = PORT;
  s.on_connect = term_on_connect;
  s.on_disconnect = term_on_disconnect;
  err = socket_init(&s);
  ON_ERROR("socket", err, done);

  LOG("Waiting for console connection on %u", PORT);
  while (socket_handle_connect(&s) != ERR_NONE);
  LOG("Console connected");

 done:
  return err;
}

void
term_bye(void)
{
  socket_disconnect(&s);
}

length_t
term_in(char *buf,
        length_t expected)
{
  return socket_in(&s, buf, expected);
}

void
term_out(const char *buf,
         length_t len)
{
  socket_out(&s, buf, len);
}
