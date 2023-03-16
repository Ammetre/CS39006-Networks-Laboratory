#include "mysocket.h"

int min(int a, int b) {
    return a < b ? a : b;
}

int send_all(MyTCP * socket, void * buffer, size_t buffer_size, size_t rate) {
    int bytes_left = (int) buffer_size, bytes_sent = 0, total_bytes_sent = 0;

    while (bytes_left > 0) {
        if ((bytes_sent = send(socket->sockfd, buffer + total_bytes_sent, min(rate, bytes_left), 0)) < 0) {
            return -1;
        } 

        total_bytes_sent += bytes_sent;
        bytes_left -= bytes_sent;
    } return bytes_sent;
}

int receive_all(MyTCP * socket, void * buffer, size_t buffer_size, size_t rate) {
    int bytes_received = 0, total_bytes_received = 0;
    
    while (total_bytes_received < buffer_size) {
        if ((bytes_received = recv(socket->sockfd, buffer + total_bytes_received, rate, 0)) < 0) {
            return -1;
        }

        total_bytes_received += bytes_received;
    } return total_bytes_received;
}

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

    socket->SshouldClose = 0;
    socket->RshouldClose = 0;

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

    socket->SshouldClose = 0;
    socket->RshouldClose = 0;

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

    socket->SshouldClose = 1;
    socket->RshouldClose = 1;
    pthread_cond_signal(&socket->condSendEmpty);
    pthread_join(socket->R, NULL);
    pthread_join(socket->S, NULL);
    pthread_mutex_destroy(&socket->mutexSend);
    pthread_mutex_destroy(&socket->mutexRecv);
    pthread_cond_destroy(&socket->condSendEmpty);
    pthread_cond_destroy(&socket->condRecvEmpty);
    pthread_cond_destroy(&socket->condSendFull);
    pthread_cond_destroy(&socket->condRecvFull);
}

ssize_t my_send(MyTCP * socket, void * buf, size_t n, int flags) {
    if (n > MAX_MESSAGE_SIZE) return -1;
    pthread_mutex_lock(&socket->mutexSend);
    while (socket->send_count == MAX_SEND_QUEUE_SIZE) {
        // printf("Send queue full. my_send waiting...\n");
        pthread_cond_wait(&socket->condSendFull, &socket->mutexSend);
    } 

    // printf("Send queue not full. Moving message '%s' into send queue.\n", (char *) buf);
    memset(socket->send_queue[socket->send_in], 0, MAX_MESSAGE_SIZE);
    strcpy(socket->send_queue[socket->send_in],(char *)buf);
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
        // printf("Recv queue empty. my_recv waiting...\n");
        pthread_cond_wait(&socket->condRecvEmpty, &socket->mutexRecv);
    }

    // printf("Recv queue not empty. Moving message '%s' from recv queue to buffer.\n", socket->recv_queue[socket->recv_out]);
    strcpy((char *)buf,socket->recv_queue[socket->recv_out]);
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
            if (socket->SshouldClose) {
                break;
            }
            // printf("Send queue empty. S thread waiting...\n");
            pthread_cond_wait(&socket->condSendEmpty, &socket->mutexSend);
        }

        if (socket->SshouldClose) {
            break;
        }

        // printf("Send queue not empty. Sending message '%s'.\n", socket->send_queue[socket->send_out]);
        // printf("Sending to socket %d\n", socket->sockfd);
        char * msg = socket->send_queue[socket->send_out];
        int * msg_len_ptr = (int *) malloc(sizeof(int));
        *msg_len_ptr = strlen(msg);
        int bytes_sent = send(socket->sockfd, msg_len_ptr, sizeof(int), 0);
        if (bytes_sent <= 0) {
            perror("[Internal Error] Unable to send");
        } else {
            // printf("Bytes sent: %d\n", bytes_sent);
            send_all(socket, msg, *msg_len_ptr, 50);
            socket->send_out++;
            socket->send_out %= MAX_SEND_QUEUE_SIZE;
            socket->send_count--;
        }

        pthread_mutex_unlock(&socket->mutexSend);
        pthread_cond_signal(&socket->condSendFull);

        sleep(2);
    }

    printf("Thread R exited successfully.\n");
    pthread_exit(NULL);
}

void * recv_routine(void * arg) {
    MyTCP * socket = (MyTCP *) arg;

    while (1) {
        pthread_mutex_lock(&socket->mutexRecv);
        while (socket->recv_count == MAX_RECV_QUEUE_SIZE) {
            // printf("Recv queue full. R thread waiting...\n");
            pthread_cond_wait(&socket->condRecvFull, &socket->mutexRecv);
        }

        int * msg_len_ptr = (int *) malloc(sizeof(int));
        int bytes_received = recv(socket->sockfd, msg_len_ptr, sizeof(int), MSG_DONTWAIT);
        if (bytes_received <= 0) {
            // perror("[Internal Error] Unable to receive");
        } else {
            // printf("Bytes received: %d\n", bytes_received);
            // printf("Recv queue not full. Moving message '%s' into recv queue.\n", socket->recv_queue[socket->recv_in]);
            memset(socket->recv_queue[socket->recv_in], 0, MAX_MESSAGE_SIZE);
            receive_all(socket, socket->recv_queue[socket->recv_in], *msg_len_ptr, 50);
            socket->recv_in++;
            socket->recv_in %= MAX_RECV_QUEUE_SIZE;
            socket->recv_count++;
        }

        if (socket->RshouldClose) {
            break;
        }

        pthread_mutex_unlock(&socket->mutexRecv);
        pthread_cond_signal(&socket->condRecvEmpty);

        sleep(2);
    }

    printf("Thread S exited successfully.\n");
    pthread_exit(NULL);
}