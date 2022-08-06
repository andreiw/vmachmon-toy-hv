#pragma once

#include "pvp.h"

typedef struct socket_s {
  int port;
  int fd;
  int sockfd;
  void (*on_disconnect)(struct socket_s *s);
  void (*on_connect)(struct socket_s *s);
} socket_t;

err_t socket_init(socket_t *s);
void socket_out(socket_t *s, const char *buf, length_t len);
length_t socket_in(socket_t *s, char *buf, length_t expected);
void socket_disconnect(socket_t *s);
err_t socket_handle_connect(socket_t *s);
bool socket_connected(socket_t *s);
