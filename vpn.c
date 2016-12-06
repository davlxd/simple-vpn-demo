#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>

// #define AS_CLIENT YES
// #define SERVER_HOST ""

#define PORT 54345
#define MTU 1400
#define BIND_HOST "0.0.0.0"

static int max(int a, int b) {
  return a > b ? a : b;
}


/*
 * Create VPN interface /dev/tun0 and return a fd
 */
int tun_alloc() {
  struct ifreq ifr;
  int fd, e;

  if ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
    perror("cannot open /dev/net/tun");
    return fd;
  }

  memset(&ifr, 0, sizeof(ifr));

  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  strncpy(ifr.ifr_name, "tun0", IFNAMSIZ);

  if ((e = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0) {
    perror("ioctl[TUNSETIFF]");
    close(fd);
    return e;
  }

  return fd;
}


/*
 * Configure IP address and MTU of VPN interface /dev/tun0
 */
void ifconfig() {
  char cmd[1024];

#ifdef AS_CLIENT
  snprintf(cmd, sizeof(cmd), "ifconfig tun0 10.8.0.2/16 mtu %d up", MTU);
#else
  snprintf(cmd, sizeof(cmd), "ifconfig tun0 10.8.0.1/16 mtu %d up", MTU);
#endif
  printf("%s\n", cmd);
  if (system(cmd)) {
    perror("ifconfig tun0 error");
    exit(1);
  }
}


/*
 * Bind UDP port
 */
int udp_bind(struct sockaddr *addr, socklen_t* addrlen) {
  struct addrinfo hints;
  struct addrinfo *result;
  int sock, flags;

  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

#ifdef AS_CLIENT
  const char *host = SERVER_HOST;
#else
  const char *host = BIND_HOST;
#endif
  if (0 != getaddrinfo(host, NULL, &hints, &result)) {
    perror("getaddrinfo error");
    return -1;
  }

  if (result->ai_family == AF_INET)
    ((struct sockaddr_in *)result->ai_addr)->sin_port = htons(PORT);
  else if (result->ai_family == AF_INET6)
    ((struct sockaddr_in6 *)result->ai_addr)->sin6_port = htons(PORT);
  else {
    fprintf(stderr, "unknown ai_family %d", result->ai_family);
    freeaddrinfo(result);
    return -1;
  }
  memcpy(addr, result->ai_addr, result->ai_addrlen);
  *addrlen = result->ai_addrlen;

  if (-1 == (sock = socket(result->ai_family, SOCK_DGRAM, IPPROTO_UDP))) {
    perror("can not create socket");
    freeaddrinfo(result);
    return -1;
  }

#ifndef AS_CLIENT
  if (0 != bind(sock, result->ai_addr, result->ai_addrlen)) {
    perror("cannot bind");
    close(sock);
    freeaddrinfo(result);
    return -1;
  }
#endif

  freeaddrinfo(result);

  flags = fcntl(sock, F_GETFL, 0);
  if (flags != -1) {
    if (-1 != fcntl(sock, F_SETFL, flags | O_NONBLOCK))
      return sock;
  }
  perror("fcntl error");

  close(sock);
  return -1;
}


int main(int argc, char **argv) {
  int tun_fd;
  if ((tun_fd = tun_alloc()) < 0) {
    return 1;
  }
  ifconfig();

  int udp_fd;
  struct sockaddr_storage client_addr;
  socklen_t client_addrlen = sizeof(client_addr);

  if ((udp_fd = udp_bind((struct sockaddr *)&client_addr, &client_addrlen)) < 0) {
    return 1;
  }

  char buf[MTU];
  bzero(buf, MTU);

  while (1) {
    fd_set readset;
    FD_ZERO(&readset);
    FD_SET(tun_fd, &readset);
    FD_SET(udp_fd, &readset);
    int max_fd = max(tun_fd, udp_fd) + 1;

    if (-1 == select(max_fd, &readset, NULL, NULL, NULL)) {
      perror("select error");
      break;
    }

    int r;
    if (FD_ISSET(tun_fd, &readset)) {
      r = read(tun_fd, buf, MTU);
      if (r < 0) {
        // TODO: ignore some errno
        perror("read from tun_fd error");
        break;
      }

      printf("Writing to UDP %d bytes ...\n", r);
      r = sendto(udp_fd, buf, r, 0, (const struct sockaddr *)&client_addr, client_addrlen);
      if (r < 0) {
        // TODO: ignore some errno
        perror("sendto udp_fd error");
        break;
      }
    }

    if (FD_ISSET(udp_fd, &readset)) {
      r = recvfrom(udp_fd, buf, MTU, 0, (struct sockaddr *)&client_addr, &client_addrlen);
      if (r < 0) {
        // TODO: ignore some errno
        perror("recvfrom udp_fd error");
        break;
      }

      printf("Writing to tun %d bytes ...\n", r);
      r = write(tun_fd, buf, r);
      if (r < 0) {
        // TODO: ignore some errno
        perror("write tun_fd error");
        break;
      }
    }
  }

  close(tun_fd);
  close(udp_fd);


  //TODO: stub encrypt/decrypt

  /* char buffer[999]; */
  /* read(STDIN_FILENO, buffer, 10); */
  /* printf("%s\n", buffer); */

  return 0;
}

