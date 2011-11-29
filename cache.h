#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <netinet/if_ether.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <linux/ip.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>
#include <net/ethernet.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>


struct control_data {
	int cache_size;
	int timeout;
	int semid;
};

struct mac_to_ip {
	int n;
	int used;
	u_int8_t  ether_shost[ETH_ALEN];
	struct sockaddr_in addr;
	struct timeval time;
};

struct cache_mem_map {
	struct control_data control_data;
	struct mac_to_ip mac_to_ip;
};

struct cache_p {
	struct control_data * control_data;
	struct mac_to_ip * mac_to_ip;
};

struct cache_p create_cache(int cache_size);

