#include "mysocket.h"

MyTCP * my_socket(int domain, int type, int protocol) {
    if (type != SOCK_MyTCP) {
        printf("[External Error] Unable to create socket: my_socket only accepts SOCK_MyTCP as socket type.\n");
        exit(EXIT_FAILURE);
    }

    int sockfd;
    if ((sockfd = socket(domain, SOCK_STREAM, protocol)) < 0) {
		perror("[Internal Error] Unable to create socket");
        exit(EXIT_FAILURE);
	} 

    MyTCP * socket = (MyTCP *) malloc(sizeof(MyTCP));
    socket->sockfd = sockfd;

    socket->send_in = 0;
    socket->send_out = 0;
    socket->send_count = 0;
    socket->recv_in = 0;
    socket->recv_out = 0;
    socket->recv_count = 0;

    socket->RshouldClose = 0;
    socket->SshouldClose = 0;

    pthread_mutex_init(&socket->mutexSend, NULL);
    pthread_mutex_init(&socket->mutexRecv, NULL);
    pthread_cond_init(&socket->condSendEmpty, NULL);
    pthread_cond_init(&socket->condRecvEmpty, NULL);
    pthread_cond_init(&socket->condSendFull, NULL); 
    pthread_cond_init(&socket->condRecvFull, NULL); 

    void * arg = (void *) socket;
    if (pthread_create(&socket->S, NULL, &send_routine, arg) < 0) {
        perror("[Internal Error] Unable to create send thread");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&socket->R, NULL, &recv_routine, arg) < 0) {
        perror("[Internal Error] Unable to create recv thread");
        exit(EXIT_FAILURE);
    }

    return socket;
}

void my_bind(MyTCP * socket, const struct sockaddr *addr, socklen_t addrlen) {
    if (socket == NULL) {
        printf("[External Error] Unable to bind socket: Socket does not exist.\n");
		exit(EXIT_FAILURE);
    }

    if (bind(socket->sockfd, addr, addrlen) < 0) {
        perror("[Internal Error] Unable to bind local address");
        exit(EXIT_FAILURE);
    }
}

void my_listen(MyTCP * socket, int backlog) {
    if (socket == NULL) {
        printf("[External Error] Unable to listen: Socket does not exist.\n");
		exit(EXIT_FAILURE);
    }

    if (listen(socket->sockfd, backlog) < 0) {
        perror("[Internal Error] Unable to listen");
        exit(EXIT_FAILURE); 
    } 
}

MyTCP * my_accept(MyTCP * socket, struct sockaddr *addr, socklen_t *addrlen) {
    if (socket == NULL) {
        printf("[External Error] Unable to accept: Socket does not exist.\n");
		exit(EXIT_FAILURE);
    }

    int newsockfd;
    if ((newsockfd = accept(socket->sockfd, addr, addrlen)) < 0) {
        perror("[Internal Error] Unable to accept client request");
        exit(EXIT_FAILURE);
    } 

    MyTCP * newsocket = (MyTCP *) malloc(sizeof(MyTCP));
    newsocket->sockfd = newsockfd;

    newsocket->send_in = 0;
    newsocket->send_out = 0;
    newsocket->send_count = 0;
    newsocket->recv_in = 0;
    newsocket->recv_out = 0;
    newsocket->recv_count = 0;

    socket->RshouldClose = 0;
    socket->SshouldClose = 0;

    pthread_mutex_init(&newsocket->mutexSend, NULL);
    pthread_mutex_init(&newsocket->mutexRecv, NULL);
    pthread_cond_init(&newsocket->condSendEmpty, NULL);
    pthread_cond_init(&newsocket->condRecvEmpty, NULL);
    pthread_cond_init(&newsocket->condSendFull, NULL); 
    pthread_cond_init(&newsocket->condRecvFull, NULL); 

    void * arg = (void *) newsocket;
    if (pthread_create(&newsocket->S, NULL, &send_routine, arg) < 0) {
        perror("[Internal Error] Unable to create send thread");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&newsocket->R, NULL, &recv_routine, arg) < 0) {
        perror("[Internal Error] Unable to create recv thread");
        exit(EXIT_FAILURE);
    }

    return newsocket;
}

void my_connect(MyTCP * socket, const struct sockaddr *addr, socklen_t addrlen) {
    if (socket == NULL) {
        printf("[External Error] Unable to connect: Socket does not exist.\n");
		exit(EXIT_FAILURE);
    }

    if ((connect(socket->sockfd, addr, addrlen)) < 0) {
        perror("[Internal Error] Unable to connect to server");
		exit(EXIT_FAILURE);
	}
}

void my_close(MyTCP * socket) {
    sleep(5);

    if (socket == NULL) {
        printf("[External Error] Unable to close socket: Socket does not exist.\n");
		exit(EXIT_FAILURE);
    }

    if (close(socket->sockfd) < 0) {
        perror("[Internal Error] Unable to close socket");
        exit(EXIT_FAILURE);
    }

    void * RreturnValue, * SreturnValue;
    pthread_join(socket->R, &RreturnValue);
    pthread_join(socket->S, &SreturnValue);
}

ssize_t my_send(MyTCP * socket, void * buf, size_t n, int flags) {
    if (n > MAX_MESSAGE_SIZE) return -1;
    pthread_mutex_lock(&socket->mutexSend);
    while (socket->send_count == MAX_SEND_QUEUE_SIZE) {
        printf("Send queue full. my_send waiting...\n");
        pthread_cond_wait(&socket->condSendFull, &socket->mutexSend);
    } 

    printf("Send queue not full. Moving message '%s' into send queue.\n", (char *) buf);
    strncpy(socket->send_queue[socket->send_in],(char *)buf,MAX_MESSAGE_SIZE);
    socket->send_in++;
    socket->send_in %= MAX_SEND_QUEUE_SIZE;
    socket->send_count++;

    pthread_mutex_unlock(&socket->mutexSend);
    pthread_cond_signal(&socket->condSendEmpty);
    return n;
}

void my_recv(MyTCP * socket, void * buf, int flags) {
    pthread_mutex_lock(&socket->mutexRecv);
    while (socket->recv_count == 0) {
        printf("Recv queue empty. my_recv waiting...\n");
        pthread_cond_wait(&socket->condRecvEmpty, &socket->mutexRecv);
    }

    printf("Recv queue not empty. Moving message '%s' from recv queue to buffer.\n", socket->recv_queue[socket->recv_out]);
    strncpy((char *)buf,socket->recv_queue[socket->recv_out],MAX_MESSAGE_SIZE);
    socket->recv_out++;
    socket->recv_out %= MAX_SEND_QUEUE_SIZE;
    socket->recv_count--;
    
    pthread_mutex_unlock(&socket->mutexRecv);
    pthread_cond_signal(&socket->condRecvFull);
}

void * send_routine(void * arg) {

    MyTCP * socket = (MyTCP *) arg;

    while (1) {
        pthread_mutex_lock(&socket->mutexSend);
        while (socket->send_count == 0) {
            printf("Send queue empty. S thread waiting...\n");
            pthread_cond_wait(&socket->condSendEmpty, &socket->mutexSend);
        }

        printf("Send queue not empty. Sending message '%s'.\n", socket->send_queue[socket->send_out]);
        printf("Sending to socket %d\n", socket->sockfd);
        int bytes_sent = 0;
        if ((bytes_sent = send(socket->sockfd, socket->send_queue[socket->send_out], strlen(socket->send_queue[socket->send_out]) + 1, 0)) <= 0) {
            perror("[Internal Error] Unable to send");
        } else {
            printf("Bytes sent: %d\n", bytes_sent);
            socket->send_out++;
            socket->send_out %= MAX_SEND_QUEUE_SIZE;
            socket->send_count--;
        }

        pthread_mutex_unlock(&socket->mutexSend);
        pthread_cond_signal(&socket->condSendFull);

        sleep(2);
    }

    return NULL;
}

void * recv_routine(void * arg) {

    MyTCP * socket = (MyTCP *) arg;

    while (1) {
        pthread_mutex_lock(&socket->mutexRecv);
        while (socket->recv_count == MAX_RECV_QUEUE_SIZE) {
            printf("Recv queue full. R thread waiting...\n");
            pthread_cond_wait(&socket->condRecvFull, &socket->mutexRecv);
        }

        int bytes_received = 0;
        if ((bytes_received = recv(socket->sockfd, socket->recv_queue[socket->recv_in], MAX_MESSAGE_SIZE, 0)) <= 0) {
            perror("[Internal Error] Unable to receive");
        } else {
            printf("Bytes received: %d\n", bytes_received);
            printf("Recv queue not full. Moving message '%s' into recv queue.\n", socket->recv_queue[socket->recv_in]);
            socket->recv_in++;
            socket->recv_in %= MAX_RECV_QUEUE_SIZE;
            socket->recv_count++;
        }

        pthread_testcancel();
        pthread_mutex_unlock(&socket->mutexRecv);
        pthread_cond_signal(&socket->condRecvEmpty);

        sleep(2);
    }

    return NULL;
}