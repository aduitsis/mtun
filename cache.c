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
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/sem.h>


#include "cache.h"

char* eth2str(u_int8_t *src) {
	static char source[18];
	sprintf(source,"%02x:%02x:%02x:%02x:%02x:%02x",src[0],src[1],src[2],src[3],src[4],src[5]);
	return source;
}
	

void init_cache(struct cache_p cache_p) {
	//int * size = cache_p.cache_size;
	int size = cache_p.control_data->cache_size;
	fprintf(stderr,"init_cache: size is %d\n",size);

	int i;
	for(i=0;i<size;i++) {
		(cache_p.mac_to_ip+i)->n=i;
		(cache_p.mac_to_ip+i)->used=0;
	}
}


void cache_sem_op(struct cache_p cache_p,int op) {
	int semid =  cache_p.control_data->semid;
	
	//prepare a 1-element array of struct sembuf and init it properly
	struct sembuf sembuf[1];
	sembuf[0].sem_num = 0;
	sembuf[0].sem_op = op;
	sembuf[0].sem_flg = SEM_UNDO;

	if(semop(semid,sembuf,1)<0) {
		perror("Cannot execute semaphore operation");
		exit(EXIT_FAILURE);
	}
}

void acquire(struct cache_p cache_p) {
	cache_sem_op(cache_p,-1);
}

void release(struct cache_p cache_p) {
	cache_sem_op(cache_p,1);
}
		 

struct cache_p create_cache(int cache_size) {
	int mmap_size = sizeof(struct control_data)+cache_size*sizeof(struct mac_to_ip);
	fprintf(stderr,"Allocating %d bytes for cache\n",mmap_size);
	void * map;

	/*create a shared memory segment*/
	if ((map = mmap(NULL,mmap_size,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0))<0) {
		perror("cannot create mmap area");
		exit(EXIT_FAILURE);
	}

	fprintf(stderr,"mmap points to %x\n",map);

	/*create a pointer and make it point to the mmaped area*/
	struct cache_mem_map * cache_mem_map_ptr;
	cache_mem_map_ptr = (struct cache_mem_map *)map;	

	/*init members*/
	cache_mem_map_ptr->control_data.cache_size = cache_size;

	/*prepare a pointer to the elements*/
	struct cache_p cache_p;
	
	/*init the pointers*/
	cache_p.control_data = &(cache_mem_map_ptr->control_data);	
	cache_p.mac_to_ip = &(cache_mem_map_ptr->mac_to_ip);

	/*now, create a semaphore*/
	int semid = semget(IPC_PRIVATE, 1, O_RDWR|IPC_CREAT);
	if(semid<0) {
		perror("Cannot create semaphore");
		exit(EXIT_FAILURE);
	}

	//make the union, set val=0
	union semun {
		int val;
		struct semid_ds *buf;
		unsigned short  * array;
	} arg;
	//semaphore is initialized to 1 so that it can be acquired for the first time
	arg.val=1;

	//set the semaphore 0 (2nd argument) to 0 (arg.val).
	if( semctl(semid, 0, SETVAL, arg) < 0) {
		fprintf(stderr, "Cannot set semaphore value.\n");
		exit(EXIT_FAILURE);
	}

	fprintf(stderr, "Semaphore %d initialized.\n", semid);


	cache_p.control_data->semid = semid;

	init_cache(cache_p);

	return cache_p;
}


void walk_cache(struct cache_p cache_p) {
	acquire(cache_p);
	int size = cache_p.control_data->cache_size;
	int i;
	for(i=0;i<size;i++) {
		if ((cache_p.mac_to_ip+i)->used==1) fprintf(stderr,"walk: i:%d address:%x n:%d used:%d time:%d ip:%s mac:%s\n",i,cache_p.mac_to_ip+i,(cache_p.mac_to_ip+i)->n,(cache_p.mac_to_ip+i)->used,(cache_p.mac_to_ip+i)->time.tv_sec,inet_ntoa((cache_p.mac_to_ip+i)->addr.sin_addr),eth2str((cache_p.mac_to_ip+i)->ether_shost));
	}
	release(cache_p);	
}

int find_unused(struct cache_p cache_p) {
	int size = cache_p.control_data->cache_size;
	int i;
	for(i=0;i<size;i++) {
		if ((cache_p.mac_to_ip+i)->used == 0) return i;
	}
	return -1;
}

int find_oldest(struct cache_p cache_p) {
	int size = cache_p.control_data->cache_size;
	int i;
	int oldest_i=-1;
	time_t oldest=0;
	for(i=0;i<size;i++) {
		if((cache_p.mac_to_ip+i)->used==0) continue;
		if((cache_p.mac_to_ip+i)->time.tv_sec < oldest) {
			oldest = (cache_p.mac_to_ip+i)->time.tv_sec;
			oldest_i = i;		
		}
	}
	return oldest_i;
}

int find_free(struct cache_p cache_p) {
	int size = cache_p.control_data->cache_size;
	int unused = find_unused(cache_p);
	fprintf(stderr,"find_free: found unused n=%d\n",unused);
	if(unused>=0) {
		return unused;
	} 
	else {
		int oldest = find_oldest(cache_p);
		if(oldest<0) {
			perror("ERROR: Cannot find oldest record...internal failure");
			exit(EXIT_FAILURE);
		}
		fprintf(stderr,"find_free: found oldest n=%d\n",oldest);
		return find_oldest(cache_p);
	}
}

int search_for_ether(struct cache_p cache_p,u_int8_t *ether_shost) {
	int size = cache_p.control_data->cache_size;
	int i;
	fprintf(stderr,"search: looking for: %s \n",eth2str(ether_shost));
	for(i=0;i<size;i++) {
		if((cache_p.mac_to_ip+i)->used==0) continue;
		fprintf(stderr,"search: examining %d (%s) \n",i,eth2str((cache_p.mac_to_ip+i)->ether_shost));
		if(memcmp(ether_shost,(cache_p.mac_to_ip+i)->ether_shost,ETH_ALEN*sizeof(u_int8_t))==0) {
			return i;
		}
	}
	return -1;
}
		

void insert(struct cache_p cache_p,struct sockaddr_in addr,u_int8_t *ether_shost) {
	acquire(cache_p);
	int size = cache_p.control_data->cache_size;
	struct timeval time;
	if(gettimeofday(&time,NULL)<0) {
		perror("Cannot get time");
		exit(EXIT_FAILURE);
	}
	fprintf(stderr,"insert: time is %d \n",time.tv_sec);
	fprintf(stderr,"insert: time is %s \n",ctime(&(time.tv_sec)));

	int insert_into = search_for_ether(cache_p,ether_shost);

	if(insert_into>=0) {
		fprintf(stderr,"insert: %s already at %d \n",eth2str(ether_shost),insert_into);
	}
	else {
		int free = find_free(cache_p);
		fprintf(stderr,"insert: haven't seen it before...inserting into slot %d\n",free);
		insert_into = free;
	}

	struct mac_to_ip * record = cache_p.mac_to_ip+insert_into;

	//it is used 
	record->used=1;

	//copy in the time
	memcpy(&(record->time),&time,sizeof(struct timeval));

	//copy in the sockaddr_in
	memcpy(&(record->addr),&addr,sizeof(struct sockaddr_in));

	//copy in the ether_shost
	memcpy(record->ether_shost,ether_shost,ETH_ALEN*sizeof(u_int8_t));

	release(cache_p);	
}

void free_cache(struct cache_p cache_p,int target) {
	
	fprintf(stderr,"free_cache: removing %d\n",target);	
	(cache_p.mac_to_ip+target)->used=0;
}

int find(struct cache_p cache_p,u_int8_t *ether_shost,struct sockaddr_in *ret) {
	acquire(cache_p);
	int found = search_for_ether(cache_p,ether_shost);
	if(found<0) {
		release(cache_p);
		return -1;
	} 
	else {
		//copy the value to be returned to a local auto struct sockaddr_in
		memcpy(ret,&((cache_p.mac_to_ip+found)->addr),sizeof(struct sockaddr_in));
		//now release the lock
		release(cache_p);
		//return the value directly (not a pointer)
		return 0;
	}
}
		
		
