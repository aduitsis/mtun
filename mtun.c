#  ---------------------------------------------------------------
# | This piece of code is released under the                      |
# | artistic license 2.0                                          |
# | http://www.opensource.org/licenses/artistic-license-2.0.php   |
# | Athanasios Douitsis, aduitsis@netmode.ntua.gr, NTUA, 2011     |
#  ---------------------------------------------------------------


#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <netinet/if_ether.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>


int main(int argc, char *argv[])
{
	struct sockaddr_in addr;
	int sock,ttl=64;
	struct ip_mreq mreq;

	struct ifreq ifr;
	int fd, err, n;
	void *buf;

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
	buf = malloc(10000);
	if( (fd = open("/dev/net/tun", O_RDWR)) < 0 ) {
		fprintf(stderr,"Could not open device\n");
		exit(1);
	}


	/* setup an ifr options struct*/
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

	/* set the options to the device*/
	if( (err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0 ) {
		fprintf(stderr,"Cannot allocate TAP device\n");
		close(fd);
		exit(1);
	}


	fprintf(stderr,"Allocated device %s\n", ifr.ifr_name);

	if(fork()) {
		if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL,
		               (char *) &ttl, 1) < 0 ) {
			perror("Fuck off");
			exit(EXIT_FAILURE);
		}

		close(0);
		fprintf(stderr,"Parent is up\n");
		while(1) {
			if ((n = read(fd, buf, 10000))<0) {
				fprintf(stderr,"Could not read from device");
				close(fd);
				exit(1);
			}
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

			fprintf(stderr,"Read %d from device\n",n);
		}
	} else {
		fprintf(stderr,"Child is up\n");

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

		/*also make sure we won't have loops, eg. out packets will not get echoed back to us*/
		if (setsockopt(sock,IPPROTO_IP,IP_MULTICAST_LOOP,&no,sizeof(no)) < 0) {
			perror("IP_MULTICAST_LOOP = false failed");
			exit(1);
		}
		while(1) {
			/*
			if ((n = read(0, buf, 10000))<0) {
				fprintf(stderr,"Could not read from stdin");
				close(fd);
				exit(1);
			}
			*/
			socklen_t addrlen=sizeof(addr);
			if ((n=recvfrom(sock,buf,10000,0,(struct sockaddr *) &addr,&addrlen)) < 0) {
				perror("recvfrom");
				exit(1);
			}
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
