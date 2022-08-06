#include "socket.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>

void
socket_disconnect(socket_t *s)
{
  if (s->fd != -1) {
    if (s->on_disconnect != NULL) {
      s->on_disconnect(s);
    }
    close(s->fd);
    s->fd = -1;
  }
}

err_t
socket_handle_connect(socket_t *s)
{
  socklen_t len;
  int sockoptval = 1;
  struct sockaddr_in cli;

  if (s->fd == -1) {
    len = sizeof(cli);
    s->fd = accept(s->sockfd, (struct sockaddr *) &cli, &len);

    if (s->fd != -1) {
      setsockopt(s->fd, SOL_SOCKET, SO_NOSIGPIPE,
                 &sockoptval, sizeof(int));

      if (s->on_connect != NULL) {
        s->on_connect(s);
      }
      return ERR_NONE;
    }
  } else {
    return ERR_NONE;
  }

  return ERR_NOT_READY;
}

err_t
socket_init(socket_t *s)
{
  int sockflags;
  int sockoptval = 1;
  struct sockaddr_in servaddr;

  s->fd = -1;
  s->sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s->sockfd < 0) {
    POSIX_ERROR(errno, "socket");
    return ERR_POSIX;
  }

  sockflags = fcntl(s->sockfd, F_GETFL);
  fcntl(s->sockfd, F_SETFL, sockflags | O_NONBLOCK);

  setsockopt(s->sockfd, SOL_SOCKET, SO_REUSEADDR, &sockoptval, sizeof(int));
  setsockopt(s->sockfd, SOL_SOCKET, SO_NOSIGPIPE, &sockoptval, sizeof(int));

  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = INADDR_ANY;
  servaddr.sin_port = htons(s->port);

  if (bind(s->sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) != 0) {
    POSIX_ERROR(errno, "bind");
    return ERR_POSIX;
  }

  if (listen(s->sockfd, 1) != 0) {
    POSIX_ERROR(errno, "listen");
    return ERR_POSIX;
  }

  return ERR_NONE;
}

void
socket_out(socket_t *s,
           const char *buf,
           length_t len)
{
  int written;

  if (socket_handle_connect(s) != ERR_NONE) {
    return;
  }

  while (len != 0) {
    written = write(s->fd, buf, len);
    if (written == -1) {
      POSIX_ERROR(errno, "term write");
    }

    len -= written;
    buf += written;
  }
}

length_t
socket_in(socket_t *s,
          char *buf,
          length_t expected)
{
  int ret;

  if (socket_handle_connect(s) != ERR_NONE) {
    return 0;
  }

  ret = read(s->fd, buf, expected);
  if (ret == 0) {
    socket_disconnect(s);
  } else if (ret == -1) {
    if (errno != EAGAIN) {
      POSIX_ERROR(errno, "read");
    }
    return 0;
  }
  return ret;
}
