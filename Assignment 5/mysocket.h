#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h> 
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define SOCK_MyTCP 12
#define MAX_SEND_QUEUE_SIZE 10
#define MAX_RECV_QUEUE_SIZE 10
#define MAX_MESSAGE_SIZE 1024
#define META_DATA_SIZE 10

typedef struct MyTCP_ {
    int sockfd;
    pthread_t R, S;
    char send_queue[MAX_SEND_QUEUE_SIZE][MAX_MESSAGE_SIZE];
    char recv_qeueu[MAX_RECV_QUEUE_SIZE][MAX_MESSAGE_SIZE];
} MyTCP;

MyTCP * my_socket(int domain, int type, int protocol);
void my_bind(MyTCP * socket, const struct sockaddr *addr, socklen_t addrlen);
void my_listen(MyTCP * socket, int backlog);
MyTCP * my_accept(MyTCP * socket, struct sockaddr *addr, socklen_t *addrlen);
void my_connect(MyTCP * socket, const struct sockaddr *addr, socklen_t addrlen);
int min(int a, int b);
int send_all(MyTCP * socket, char * buffer, size_t buffer_size, size_t rate);
int receive_all(MyTCP * socket, char * buffer, size_t rate);
void my_close(MyTCP * socket);
