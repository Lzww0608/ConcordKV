/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:51:56
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:51:56
* @Description: epoll entry for kv server
 */


#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <sys/time.h>


#include "kv_store.h"


int accept_cb(int fd);

int recv_cb(int fd);
int send_cb(int fd);



int epfd = 0;
struct conn_item conn_list[1048576] = {0}; 

struct timeval Lzww;


#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)

// 1: add
// 0: mod
int set_event(int fd, int event, int flag) {

	if (flag) { 
		struct epoll_event ev;
		ev.events = event ;
		ev.data.fd = fd;
		epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
	} else {
	
		struct epoll_event ev;
		ev.events = event;
		ev.data.fd = fd;
		epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
	}

	

}

int accept_cb(int fd) {

	struct sockaddr_in client_addr;
	socklen_t len = sizeof(client_addr);
	
	int client_fd = accept(fd, (struct sockaddr*)&client_addr, &len);
	if (client_fd < 0) {
		return -1;
	}
	set_event(client_fd, EPOLLIN, 1);

	conn_list[client_fd].fd = client_fd;
	memset(conn_list[client_fd].rbuffer, 0, BUFFER_LENGTH);
	conn_list[client_fd].rlen = 0;
	memset(conn_list[client_fd].wbuffer, 0, BUFFER_LENGTH);
	conn_list[client_fd].wlen = 0;
	
	conn_list[client_fd].recv_t.recv_callback = recv_cb;
	conn_list[client_fd].send_callback = send_cb;

	if ((client_fd % 1000) == 999) {
		struct timeval tv_cur;
		gettimeofday(&tv_cur, NULL);
		int time_used = TIME_SUB_MS(tv_cur, Lzww);

		memcpy(&Lzww, &tv_cur, sizeof(struct timeval));
		
		printf("client_fd : %d, time_used: %d\n", client_fd, time_used);
	}

	return client_fd;
}

int recv_cb(int fd) { 

	char *buffer = conn_list[fd].rbuffer;
	int idx = conn_list[fd].rlen;
	
	int count = recv(fd, buffer, BUFFER_LENGTH, 0);
	if (count == 0) {
		printf("disconnect\n");

		epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);		
		close(fd);
		
		return -1;
	}

	
	conn_list[fd].rlen = count;

#if 0 //echo: need to send
	memcpy(conn_list[fd].wbuffer, conn_list[fd].rbuffer, conn_list[fd].rlen);
	conn_list[fd].wlen = conn_list[fd].rlen;
	conn_list[fd].rlen -= conn_list[fd].rlen;
#else

	kv_store_request(&conn_list[fd]);
	conn_list[fd].wlen = strlen(conn_list[fd].wbuffer);
#endif

	set_event(fd, EPOLLOUT, 0);

	
	return count;
}


int send_cb(int fd) {

	char *buffer = conn_list[fd].wbuffer;
	int idx = conn_list[fd].wlen;

	int count = send(fd, buffer, idx, 0);

	set_event(fd, EPOLLIN, 0);

	return count;
}




int init_server(unsigned short port) {

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(struct sockaddr_in));

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	if (-1 == bind(sockfd, (struct sockaddr*)&server_addr, sizeof(struct sockaddr))) {
		perror("bind");
		return -1;
	}

	listen(sockfd, 10);

	return sockfd;
}


#if 0
// tcp 
int main() {

	int port_count = 20;
	unsigned short port = 2048;
	int i = 0;

	
	epfd = epoll_create(1); // int size

	for (i = 0;i < port_count;i ++) {
		int sockfd = init_server(port + i);  // 2048, 2049, 2050, 2051 ... 2057
		conn_list[sockfd].fd = sockfd;
		conn_list[sockfd].recv_t.accept_callback = accept_cb;
		set_event(sockfd, EPOLLIN, 1);
	}

	gettimeofday(&Lzww, NULL);

	struct epoll_event events[1024] = {0};
	
	while (1) { // mainloop();

		int nready = epoll_wait(epfd, events, 1024, -1); // 

		int i = 0;
		for (i = 0;i < nready;i ++) {

			int connfd = events[i].data.fd;
			if (events[i].events & EPOLLIN) { //

				int count = conn_list[connfd].recv_t.recv_callback(connfd);
				//printf("recv count: %d <-- buffer: %s\n", count, conn_list[connfd].rbuffer);

			} else if (events[i].events & EPOLLOUT) { 
				// printf("send --> buffer: %s\n",  conn_list[connfd].wbuffer);
				
				int count = conn_list[connfd].send_callback(connfd);
			}

		}

	}


	getchar();
	//close(client_fd);

}

#else

int epoll_entry(void) {

	int port_count = 20;
	unsigned short port = 2048;
	int i = 0;

	
	epfd = epoll_create(1); 

	for (i = 0;i < port_count;i ++) {
		int sockfd = init_server(port + i);  // 2048, 2049, 2050, 2051 ... 2057
		conn_list[sockfd].fd = sockfd;
		conn_list[sockfd].recv_t.accept_callback = accept_cb;
		set_event(sockfd, EPOLLIN, 1);
	}

	gettimeofday(&Lzww, NULL);

	struct epoll_event events[1024] = {0};
	
	while (1) { 

		int nready = epoll_wait(epfd, events, 1024, -1); // 

		int i = 0;
		for (i = 0;i < nready;i ++) {

			int connfd = events[i].data.fd;
			if (events[i].events & EPOLLIN) { //

				int count = conn_list[connfd].recv_t.recv_callback(connfd);
				//printf("recv count: %d <-- buffer: %s\n", count, conn_list[connfd].rbuffer);

			} else if (events[i].events & EPOLLOUT) { 
				// printf("send --> buffer: %s\n",  conn_list[connfd].wbuffer);
				
				int count = conn_list[connfd].send_callback(connfd);
			}

		}

	}


	//getchar();
	//close(client_fd);

}


#endif


