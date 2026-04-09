#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "ipc.h"

int main(int argc, char **argv)
{
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <pid> <command>\n", argv[0]);
    return 0;
  }

  char buf[4096] = {0};
  int len = 0;
  for (int i = 2; i < argc; ++i) {
    size_t arg_len = strlen(argv[i]);
    if (len + arg_len + 1 >= sizeof buf) {
      fprintf(stderr, "Command cannot be longer than %lu\n", sizeof buf);
      return 1;
    }
    memcpy(buf + len, argv[i], arg_len);
    len += arg_len;
    buf[len++] = ' ';
  }
  buf[len-1] = '\n';

  int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
  assert(sockfd);

  struct sockaddr_un desc = {
    .sun_family = AF_UNIX
  };
  imv_ipc_path(desc.sun_path, sizeof desc.sun_path, atoi(argv[1]));

  if (connect(sockfd, (struct sockaddr *)&desc, sizeof desc) < 0) {
    perror("Failed to connect");
    return 1;
  }

  char *pos = buf;
  while (len > 0) {
    ssize_t written = write(sockfd, pos, len);
    if (written == -1) {
      perror("Failed to write");
      return 1;
    }
    pos += written;
    len -= written;
  }
  close(sockfd);
  return 0;
}
