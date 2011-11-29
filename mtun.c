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

#include "cache.h"


#define BUFSIZE 10000

int main(int argc, char *argv[])
{
	struct sockaddr_in addr;
	int sock,ttl=64;
	struct ip_mreq mreq;

	struct ifreq ifr;
	int fd, err, n;
	void *buf;

	void *map;

	int cache_size = 2000;
	int expiration = 60;

	struct cache_p cache;
	cache = create_cache(cache_size);

	fprintf(stderr,"control_data points to %x\n",cache.control_data);
	fprintf(stderr,"mac_to_ip points to %x\n",cache.mac_to_ip);

	walk_cache(cache);

	fprintf(stderr,"%s %s \n",argv[1],argv[2]);

	/*create a udp socket*/
	if ((sock=socket(AF_INET,SOCK_DGRAM,0)) < 0) {
		perror("socket");
		exit(1);
	}
	/*make a dest address*/
	memset(&addr,0,sizeof(addr));
	addr.sin_family=AF_INET;
	addr.sin_addr.s_addr=inet_addr(argv[1]);
	addr.sin_port=htons(atoi(argv[2]));

	/*open the tun device*/
	buf = malloc(BUFSIZE);
	if( (fd = open("/dev/net/tun", O_RDWR)) < 0 ) perror("Could not open device\n"); 

	/* setup an ifr options struct*/
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

	/* set the options to the device*/
	if( (err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0 ) {
		close(fd);
		perror("Cannot allocate TAP device");
	}

	fprintf(stderr,"Allocated device %s\n", ifr.ifr_name);

	if(fork()) {
		if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL,
		               (char *) &ttl, 1) < 0 ) {
			perror("setsockopt failed on reader");
			exit(EXIT_FAILURE);
		}

		close(0);
		fprintf(stderr,"Reader is up\n");
		while(1) {
			if ((n = read(fd, buf, BUFSIZE))<0) {
				fprintf(stderr,"Could not read from device");
				close(fd);
				exit(1);
			}

			u_int8_t *dest = ((struct ether_header *)buf)->ether_dhost;
			char destination[18];
			sprintf(destination,"%02x:%02x:%02x:%02x:%02x:%02x",dest[0],dest[1],dest[2],dest[3],dest[4],dest[5]);
			printf("reader: destination: %s\n",destination);
			
			struct sockaddr_in dest_addr;
			if(find(cache,dest,&dest_addr)<0) {
				fprintf(stderr,"Sending via multicast\n");	
				/*
				if(write(1,buf,n)<0) {
					 fprintf(stderr,"write to stdout failed\n");
					 close(fd);
					 exit(1);
				}
				*/

				if(sendto(sock,buf,n,0,(struct sockaddr *) &addr,sizeof(addr)) < 0) {
					perror("sendto");
					exit(1);
				}
			}
			else {
				fprintf(stderr,"I should send this via unicast to %s\n",inet_ntoa(dest_addr.sin_addr));
			}

			fprintf(stderr,"Read %d from device\n",n);
		}
	} else {
		fprintf(stderr,"Writer is up\n");

		u_int yes=1;
		u_int no=0;

		/*other sockets get get packets with the same port number*/
		if (setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes)) < 0) {
			perror("Reusing failed");
			exit(1);
		}


		/*this socket will get packets from anywhere*/
		addr.sin_addr.s_addr=htonl(INADDR_ANY);
		if (bind(sock,(struct sockaddr *) &addr,sizeof(addr)) < 0) {
			perror("bind");
			exit(1);
		}
		/*and now, join the multicast group*/
		mreq.imr_multiaddr.s_addr=inet_addr(argv[1]);
		mreq.imr_interface.s_addr=htonl(INADDR_ANY);
		if (setsockopt(sock,IPPROTO_IP,IP_ADD_MEMBERSHIP,&mreq,sizeof(mreq)) < 0) {
			perror("setsockopt");
			exit(1);
		}

		/*also make sure we won't have loops, 
		eg. our packets will not get echoed back to us*/
		//if (setsockopt(sock,IPPROTO_IP,IP_MULTICAST_LOOP,&no,sizeof(no)) < 0) {
		//	perror("IP_MULTICAST_LOOP = false failed");
		//	exit(1);
		//}
		while(1) {

			socklen_t addrlen=sizeof(addr);
			if ((n=recvfrom(sock,buf,BUFSIZE,0,(struct sockaddr *) &addr,&addrlen)) < 0) {
				perror("recvfrom");
				exit(1);
			}

			fprintf(stderr,"writer: packet came from: %s \n",inet_ntoa(addr.sin_addr));

			u_int8_t *src = ((struct ether_header *)buf)->ether_shost;
			char source[18];
			sprintf(source,"%02x:%02x:%02x:%02x:%02x:%02x",src[0],src[1],src[2],src[3],src[4],src[5]);
			printf("writer: source %s\n",source);

			/*we must insert it into the cache*/
			insert(cache,addr,src);
			walk_cache(cache);

			if(write(fd,buf,n)<0) {
				fprintf(stderr,"write to device failed\n");
				close(fd);
				exit(1);
			}
			fprintf(stderr,"Write %d to device\n",n);
		}
	}


	return 0;
}
