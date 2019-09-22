#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#include <linux/if.h>
#include <linux/if_tun.h>

/*
 * The following lines serve as configurations
 * Uncomment first 2 lines to run as vpn client
 */

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
    perror("Cannot open /dev/net/tun");
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
 * Execute commands
 */
static void run(char *cmd) {
  printf("Execute `%s`\n", cmd);
  if (system(cmd)) {
    perror(cmd);
    exit(1);
  }
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
  run(cmd);
}


/*
 * Setup route table via `iptables` & `ip route`
 */
void setup_route_table() {
  run("sysctl -w net.ipv4.ip_forward=1");

#ifdef AS_CLIENT
  run("iptables -t nat -A POSTROUTING -o tun0 -j MASQUERADE");
  run("iptables -I FORWARD 1 -i tun0 -m state --state RELATED,ESTABLISHED -j ACCEPT");
  run("iptables -I FORWARD 1 -o tun0 -j ACCEPT");
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "ip route add %s via $(ip route show 0/0 | sed -e 's/.* via \([^ ]*\).*/\1/')", SERVER_HOST);
  run(cmd);
  run("ip route add 0/1 dev tun0");
  run("ip route add 128/1 dev tun0");
#else
  run("iptables -t nat -A POSTROUTING -s 10.8.0.0/16 ! -d 10.8.0.0/16 -m comment --comment 'vpndemo' -j MASQUERADE");
  run("iptables -A FORWARD -s 10.8.0.0/16 -m state --state RELATED,ESTABLISHED -j ACCEPT");
  run("iptables -A FORWARD -d 10.8.0.0/16 -j ACCEPT");
#endif
}


/*
 * Cleanup route table
 */
void cleanup_route_table() {
#ifdef AS_CLIENT
  run("iptables -t nat -D POSTROUTING -o tun0 -j MASQUERADE");
  run("iptables -D FORWARD -i tun0 -m state --state RELATED,ESTABLISHED -j ACCEPT");
  run("iptables -D FORWARD -o tun0 -j ACCEPT");
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "ip route del %s", SERVER_HOST);
  run(cmd);
  run("ip route del 0/1");
  run("ip route del 128/1");
#else
  run("iptables -t nat -D POSTROUTING -s 10.8.0.0/16 ! -d 10.8.0.0/16 -m comment --comment 'vpndemo' -j MASQUERADE");
  run("iptables -D FORWARD -s 10.8.0.0/16 -m state --state RELATED,ESTABLISHED -j ACCEPT");
  run("iptables -D FORWARD -d 10.8.0.0/16 -j ACCEPT");
#endif
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
    perror("Cannot create socket");
    freeaddrinfo(result);
    return -1;
  }

#ifndef AS_CLIENT
  if (0 != bind(sock, result->ai_addr, result->ai_addrlen)) {
    perror("Cannot bind");
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



/*
 * Catch Ctrl-C and `kill`s, make sure route table gets cleaned before this process exit
 */
void cleanup(int signo) {
  printf("Goodbye, cruel world....\n");
  if (signo == SIGHUP || signo == SIGINT || signo == SIGTERM) {
    cleanup_route_table();
    exit(0);
  }
}

void cleanup_when_sig_exit() {
  struct sigaction sa;
  sa.sa_handler = &cleanup;
  sa.sa_flags = SA_RESTART;
  sigfillset(&sa.sa_mask);

  if (sigaction(SIGHUP, &sa, NULL) < 0) {
    perror("Cannot handle SIGHUP");
  }
  if (sigaction(SIGINT, &sa, NULL) < 0) {
    perror("Cannot handle SIGINT");
  }
  if (sigaction(SIGTERM, &sa, NULL) < 0) {
    perror("Cannot handle SIGTERM");
  }
}

static inline bool is_errno_can_be_ignore() {
  return errno == EAGAIN;
}

/*
 * For a real-world VPN, traffic inside UDP tunnel is encrypted
 * A comprehensive encryption is not easy and not the point for this demo
 * I'll just leave the stubs here
 */
void encrypt(char *plantext, char *ciphertext, int len) {
  memcpy(ciphertext, plantext, len);
}

void decrypt(char *ciphertext, char *plantext, int len) {
  memcpy(plantext, ciphertext, len);
}


int main(int argc, char **argv) {
  int tun_fd;
  if ((tun_fd = tun_alloc()) < 0) {
    return 1;
  }

  ifconfig();
  setup_route_table();
  cleanup_when_sig_exit();


  int udp_fd;
  struct sockaddr_storage client_addr;
  socklen_t client_addrlen = sizeof(client_addr);

  if ((udp_fd = udp_bind((struct sockaddr *)&client_addr, &client_addrlen)) < 0) {
    return 1;
  }


  /*
   * tun_buf - memory buffer read from/write to tun dev - is always plain
   * udp_buf - memory buffer read from/write to udp fd - is always encrypted
   */
  char tun_buf[MTU], udp_buf[MTU];
  bzero(tun_buf, MTU);
  bzero(udp_buf, MTU);

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
      r = read(tun_fd, tun_buf, MTU);
      if (r < 0) {
        if(is_errno_can_be_ignore())
          continue;
        perror("read from tun_fd error");
        break;
      }

      encrypt(tun_buf, udp_buf, r);
      printf("Writing to UDP %d bytes ...\n", r);

      r = sendto(udp_fd, udp_buf, r, 0, (const struct sockaddr *)&client_addr, client_addrlen);
      if (r < 0) {
        if(is_errno_can_be_ignore())
          continue;
        perror("sendto udp_fd error");
        break;
      }
    }

    if (FD_ISSET(udp_fd, &readset)) {
      r = recvfrom(udp_fd, udp_buf, MTU, 0, (struct sockaddr *)&client_addr, &client_addrlen);
      if (r < 0) {
        if(is_errno_can_be_ignore())
          continue;
        perror("recvfrom udp_fd error");
        break;
      }

      decrypt(udp_buf, tun_buf, r);
      printf("Writing to tun %d bytes ...\n", r);

      r = write(tun_fd, tun_buf, r);
      if (r < 0) {
        if(is_errno_can_be_ignore())
          continue;
        perror("write tun_fd error");
        break;
      }
    }
  }

  close(tun_fd);
  close(udp_fd);

  cleanup_route_table();

  return 0;
}

