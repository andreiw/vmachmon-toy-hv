#define LOG_PFX TERM
#include "term.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>

#define PORT 7000

static int fd;
static int sockfd;

static void
term_disconnect(void)
{
  if (fd != -1) {
    close(fd);
    fd = -1;
  }
}

static err_t
term_handle_connect(void)
{
  socklen_t len;
  int sockoptval = 1;
  struct sockaddr_in cli;

  if (fd == -1) {
    len = sizeof(cli);
    fd = accept(sockfd, (struct sockaddr *) &cli, &len);

    if (fd != -1) {
      setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE,
                 &sockoptval, sizeof(int));
    }
  }

  return fd == -1 ? ERR_NOT_READY : ERR_NONE;
}

err_t
term_init(void)
{
  int sockflags;
  int sockoptval = 1;

  struct sockaddr_in servaddr;

  const char *banner =
    "This is the PVP console\r\n"
    "-----------------------\r\n\n";

  fd = -1;
  sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sockfd < 0) {
    POSIX_ERROR(errno, "socket");
    return ERR_POSIX;
  }

  sockflags = fcntl(sockfd, F_GETFL);
  fcntl(sockfd, F_SETFL, sockflags | O_NONBLOCK);

  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &sockoptval, sizeof(int));
  setsockopt(sockfd, SOL_SOCKET, SO_NOSIGPIPE, &sockoptval, sizeof(int));

  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = INADDR_ANY;
  servaddr.sin_port = htons(PORT);

  if (bind(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) != 0) {
    POSIX_ERROR(errno, "bind");
    return ERR_POSIX;
  }

  if (listen(sockfd, 5) != 0) {
    POSIX_ERROR(errno, "listen");
    return ERR_POSIX;
  }

  LOG("Waiting for console connection on %u", PORT);
  while (term_handle_connect() != ERR_NONE);
  LOG("Console connected");
  term_out(banner, strlen(banner));

  return ERR_NONE;
}

void
term_bye(void)
{
  const char *banner =
    "\r\n\nPVP console closing...\r\n";

  term_out(banner, strlen(banner));
  term_disconnect();
}

void
term_out(const char *buf,
         length_t len)
{
  int written;

  if (term_handle_connect() != ERR_NONE) {
    return;
  }

  while (len != 0) {
    written = write(fd, buf, len);
    if (written == -1) {
      POSIX_ERROR(errno, "term write");
    }

    len -= written;
    buf += written;
  }
}

length_t
term_in(char *buf,
        length_t expected)
{
  int ret;

  if (term_handle_connect() != ERR_NONE) {
    return 0;
  }

  ret = read(fd, buf, expected);
  if (ret == 0) {
    term_disconnect();
  } else if (ret == -1) {
    if (errno != EAGAIN) {
      POSIX_ERROR(errno, "read");
    }
    return 0;
  }
  return ret;
}
