
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>

#include <sys/socket.h>
#include <sys/time.h>

#include <arpa/inet.h>

#define MAX_MAS_LENGTH		512
#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)


int send_msg(int conn_fd, char *msg, int length) {

	int res = send(conn_fd, msg, length, 0);
	if (res < 0) {
		perror("send");
		exit(1);
	}
	return res;
}	

// 
int recv_msg(int conn_fd, char *msg, int length) {

	int res = recv(conn_fd, msg, length, 0);
	if (res < 0) {
		perror("recv");
		exit(1);
	}
	return res;

}

// compare expected output with the result from the server.
void equals(char *pattern, char *result, char *case_name) {

	if (strcmp(pattern, result) == 0) {
		//printf("==> PASS --> %s\n", case_name);
	} else {
		printf("==> FAILED --> %s, '%s' != '%s'\n", case_name, pattern, result);
	}
}

// "SET Name Lzww" "SUCCESS"
void test_case(int conn_fd, char *msg, char *pattern, char *case_name) {

	if (!msg||!pattern||!case_name) return ;


	send_msg(conn_fd, msg, strlen(msg));

	char result[MAX_MAS_LENGTH] = {0};
	recv_msg(conn_fd, result, MAX_MAS_LENGTH);

	equals(pattern, result, case_name);

}

// 6 queries
void array_testcase(int conn_fd) {

	test_case(conn_fd, "SET Name King", "SUCCESS", "SETCase");
	test_case(conn_fd, "GET Name", "King", "GETCase");
	test_case(conn_fd, "MOD Name Darren", "SUCCESS", "MODCase");
	test_case(conn_fd, "GET Name", "Darren", "GETCase");
	test_case(conn_fd, "DEL Name", "SUCCESS", "DELCase");
	test_case(conn_fd, "GET Name", "NO EXIST", "GETCase");

}

// 10w * 6
void array_testcase_10w(int connfd) { // 10w

	int count = 100000;
	int i = 0;

	while (i ++ < count) {
		array_testcase(connfd);
	}

}


void rbtree_testcase(int connfd) {

	test_case(connfd, "RSET Name Lzww", "SUCCESS", "SETCase");
	test_case(connfd, "RGET Name", "Lzww", "GETCase");
	test_case(connfd, "RMOD Name Lzww1", "SUCCESS", "MODCase");
	test_case(connfd, "RGET Name", "Lzww1", "GETCase");
	test_case(connfd, "RDEL Name", "SUCCESS", "DELCase");
	test_case(connfd, "RGET Name", "NO EXIST", "GETCase");

}

void rbtree_testcase_10w(int connfd) { // 10w

	int count = 100000;
	int i = 0;

	while (i ++ < count) {
		array_testcase(connfd);
	}

}


// 5w * 2 * 2
void rbtree_testcase_5w_node(int connfd) {

	int count = 50000;
	int i = 0;

	for (i = 0;i < count;i ++) {

		char cmd[128] = {0};

		snprintf(cmd, 128, "RSET Name%d Lzww%d", i, i);
		test_case(connfd, cmd, "SUCCESS", "SETCase");

		char result[128] = {0};
		sprintf(result, "%d", i+1);
		test_case(connfd, "RCOUNT", result, "RCOUNT");
		
		
	}

	for (i = 0;i < count;i ++) {
		
		char cmd[128] = {0};

		snprintf(cmd, 128, "RDEL Name%d Lzww%d", i, i);
		test_case(connfd, cmd, "SUCCESS", "DELCase");

		char result[128] = {0};
		sprintf(result, "%d", count - (i+1));
		test_case(connfd, "RCOUNT", result, "RCOUNT");

	}
	

}



void hash_testcase(int connfd) {

	test_case(connfd, "HSET Name Lzww", "SUCCESS", "HSETCase");
	test_case(connfd, "HGET Name", "Lzww", "HGETCase");
	test_case(connfd, "HMOD Name Lzww1", "SUCCESS", "HMODCase");
	test_case(connfd, "HGET Name", "Lzww1", "HGETCase");
	test_case(connfd, "HDEL Name", "SUCCESS", "HDELCase");
	test_case(connfd, "HGET Name", "NO EXIST", "HGETCase");

}

void hash_testcase_10w(int connfd) { // 10w

	int count = 100000;
	int i = 0;

	while (i ++ < count) {
		hash_testcase(connfd);
	}

}


void hash_testcase_5w_node(int connfd) {

	int count = 50000;
	int i = 0;

	for (i = 0;i < count;i ++) {

		char cmd[128] = {0};

		snprintf(cmd, 128, "HSET Name%d Lzww%d", i, i);
		test_case(connfd, cmd, "SUCCESS", "SETCase");

		char result[128] = {0};
		sprintf(result, "%d", i+1);
		test_case(connfd, "HCOUNT", result, "HCOUNT");
		
		
	}

	for (i = 0;i < count;i ++) {
		
		char cmd[128] = {0};

		snprintf(cmd, 128, "HDEL Name%d Lzww%d", i, i);
		test_case(connfd, cmd, "SUCCESS", "DELCase");

		char result[128] = {0};
		sprintf(result, "%d", count - (i+1));
		test_case(connfd, "HCOUNT", result, "RCOUNT");

	}
	

}


// tcp
int connect_tcpserver(const char *ip, unsigned short port) {

	int connfd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in tcpserver_addr;
	memset(&tcpserver_addr, 0, sizeof(struct sockaddr_in));

	tcpserver_addr.sin_family = AF_INET;
	tcpserver_addr.sin_addr.s_addr = inet_addr(ip);
	tcpserver_addr.sin_port = htons(port);

	int ret = connect(connfd, (struct sockaddr*)&tcpserver_addr, sizeof(struct sockaddr_in));
	if (ret) {
		perror("connect");
		return -1;
	}

	return connfd;
}

// array: 0x01, rbtree: 0x02, hash: 0x04

// ./testcase -s 127.0.0.1 -p 9096 -m 1
int main(int argc, char *argv[]) {

	int ret = 0;

	char ip[16] = {0};
	int port = 0;
	int mode = 1;

	int opt;
	while ((opt = getopt(argc, argv, "s:p:m:?")) != -1) {

		switch (opt) {

			case 's':
				strcpy(ip, optarg);
				break;

			case 'p':
				port = atoi(optarg);
				break;

			case 'm':
				mode = atoi(optarg);
				break;

			default:
				return -1;
		
		}
		
	}

	
	int connfd = connect_tcpserver(ip, port);

	if (mode & 0x1) { // array

		struct timeval tv_begin;
		gettimeofday(&tv_begin, NULL);
		
		array_testcase_10w(connfd);

		struct timeval tv_end;
		gettimeofday(&tv_end, NULL);

		int time_used = TIME_SUB_MS(tv_end, tv_begin);
		
		printf("array testcase--> time_used: %d, qps: %d\n", time_used, 600000 * 1000 / time_used);
	}

	if (mode & 0x2) { // rbtree

		struct timeval tv_begin;
		gettimeofday(&tv_begin, NULL);
		
		//rbtree_testcase_10w(connfd);
		
		rbtree_testcase_5w_node(connfd);

		struct timeval tv_end;
		gettimeofday(&tv_end, NULL);

		int time_used = TIME_SUB_MS(tv_end, tv_begin);
		
		printf("rbtree testcase-->  time_used: %d, qps: %d\n", time_used, 200000 * 1000 / time_used);
	
	}

	if (mode & 0x4) { // hash

		struct timeval tv_begin;
		gettimeofday(&tv_begin, NULL);
		
		//hash_testcase(connfd);
		hash_testcase_5w_node(connfd);

		struct timeval tv_end;
		gettimeofday(&tv_end, NULL);

		int time_used = TIME_SUB_MS(tv_end, tv_begin);
		
		printf("hash testcase-->  time_used: %d, qps: %d\n", time_used, 200000 * 1000 / time_used);

		
	
	}


}




