#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>


/*
 * Create VPN interface /dev/tun0
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
  if (system("ifconfig tun0 10.8.0.1/16 mtu 1400 up") < 0) {
    perror("ifconfig tun0 error");
    exit(1);
  }
}


int main(int argc, char **argv) {
  int tun_fd;

  if ((tun_fd = tun_alloc()) < 0) {
    perror("failed to create tun device");
    return -1;
  }
  ifconfig();

  //TODO: WIP

  char buffer[999];
  read(STDIN_FILENO, buffer, 10);
  printf("%s\n", buffer);

  return 0;
}
