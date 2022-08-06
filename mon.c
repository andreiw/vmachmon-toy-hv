#define LOG_PFX MON
#include "mon.h"
#include "socket.h"

static int
mon_fprintf(void *unused,
            const char *fmt,
            ...);

#define PICOL_IMPLEMENTATION
#define fflush(x)
#define fprintf(x, fmt, ...) mon_fprintf(x, fmt, ## __VA_ARGS__)
#include "picol.h"

#define PORT      7001
#define IBUF_SIZE PAGE_SIZE
#define PROMPT    "(PVP) "

static socket_t s;
static char *ibuf;
static unsigned ibuf_index;
static picolInterp *interp;
static err_t picol_err;

PICOL_COMMAND(quit) {
  PICOL_ARITY2(argc == 1, "stop VM");

  *(err_t *)pd = ERR_SHUTDOWN;
  return PICOL_OK;
}

PICOL_COMMAND(cont) {
  PICOL_ARITY2(argc == 1, "continue VM");

  *(err_t *)pd = ERR_CONTINUE;
  return PICOL_OK;
}

static int
mon_fprintf(void *unused,
            const char *fmt,
            ...)
{
  int ret;
  static char buf[1024];
  va_list ap;

  va_start(ap, fmt);
  ret = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  if (ret > 0) {
    socket_out(&s, buf, ret);
  }

  return ret;
}

static void
mon_on_connect(socket_t *s)
{
  const char *banner =
    "This is the PVP monitor console\r\n"
    "-------------------------------\r\n\n" PROMPT;

  ibuf_index = 0;
  socket_out(s, banner, strlen(banner));
}

static void
mon_on_disconnect(socket_t *s)
{
  const char *banner =
    "\r\n\nMonitor console closing...\r\n";

  socket_out(s, banner, strlen(banner));
}

err_t
mon_init(void)
{
  err_t err;

  ibuf = malloc(IBUF_SIZE);
  if (ibuf == NULL) {
    POSIX_ERROR(errno, "ibuf");
    return ERR_POSIX;
  }

  interp = picolCreateInterp();
  picolRegisterCmd(interp, "quit", picol_quit, &picol_err);
  picolRegisterCmd(interp, "cont", picol_cont, &picol_err);

  s.port = PORT;
  s.on_connect = mon_on_connect;
  s.on_disconnect = mon_on_disconnect;
  err = socket_init(&s);
  ON_ERROR("socket", err, done);

 done:
  return err;
}

err_t
mon_activate(void)
{
  err_t err;

  LOG("Waiting for monitor console connection on %u", PORT);
  while (socket_handle_connect(&s) != ERR_NONE);
  LOG("Monitor console connected");

  while (1) {
    err = mon_check();

    if (err == ERR_CONTINUE) {
      return ERR_NONE;
    } else if (err != ERR_NONE) {
      break;
    }
  }

  return err;
}

void
mon_bye(void)
{
  socket_disconnect(&s);
}

err_t
mon_check(void)
{
  char c;

  if (socket_in(&s, &c, 1) == 0) {
    return ERR_NONE;
  }

  if (c == '\b') {
    if (ibuf_index != 0) {
      ibuf_index--;
    }
  } else if (c == '\n') {
    int rc;

    ibuf[ibuf_index] = '\0';
    rc = picolEval(interp, ibuf);
    if (interp->result[0] != '\0' || rc != PICOL_OK) {
      mon_fprintf(NULL, "[%d] %s\n", rc, interp->result);
    }
    socket_out(&s, PROMPT, sizeof(PROMPT) - 1);
    ibuf_index = 0;

    if (picol_err != ERR_NONE) {
      return picol_err;
    }
    return ERR_NONE;
  } else if (c == '\r') {
    return ERR_NONE;
  }

  if (ibuf_index == (IBUF_SIZE - 1)) {
    return ERR_NONE;
  }

  ibuf[ibuf_index] = c;
  ibuf_index++;  

  return ERR_NONE;
}
