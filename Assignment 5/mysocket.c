#include "mysocket.h"

MyTCP * my_socket(int domain, int type, int protocol) {
    if (type != SOCK_MyTCP) {
        printf("[External Error] Unable to create socket: my_socket only accepts SOCK_MyTCP as socket type.\n");
        exit(EXIT_FAILURE);
    }

    int sockfd;
    if ((sockfd = socket(domain, type, protocol)) < 0) {
		perror("[Internal Error] Unable to create socket");
        exit(EXIT_FAILURE);
	} 
    
    MyTCP * socket = (MyTCP *) malloc(sizeof(MyTCP));
    socket->sockfd = sockfd;
    
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

/*********************************************\
| Function to find the minimum of two integers |
| Parameters: a - The first integer            |
|             b - The second integer           |
| Returns: The minimum of a and b              |
\*********************************************/
int min(int a, int b) {
    return a < b ? a : b;
}

/*****************************************************************\
| Function to send data to a socket                                |
| Parameters: socket      - The socket to which data is to be sent |
|             buffer      - The buffer to be sent                  |
|             buffer_size - The size of the buffer to be sent      |
|             rate        - The rate at which data is to be sent   |
| Returns: The number of bytes sent, -1 for errors                 |
\*****************************************************************/
int send_all(MyTCP * socket, char * buffer, size_t buffer_size, size_t rate) {
    int bytes_left = (int) buffer_size, bytes_sent = 0, total_bytes_sent = 0;

    while (bytes_left > 0) {
        if ((bytes_sent = send(socket->sockfd, buffer + total_bytes_sent, min(rate, bytes_left), 0)) < 0) {
            return -1;
        } 

        total_bytes_sent += bytes_sent;
        bytes_left -= bytes_sent;
    } return bytes_sent;
}

/******************************************************************\
| Function to receive data from a socket                            |
| Parameters: socket - The socket from which data is to be received |
|             buffer - The buffer in which data is to be received   |
|             rate   - The rate at which data is to be received     |
| Returns: The number of bytes received, -1 for errors              |
\******************************************************************/
int receive_all(MyTCP * socket, char * buffer, size_t rate) {
    int bytes_received = 0, total_bytes_received = 0;
    
    while (1) {
        if ((bytes_received = recv(socket->sockfd, buffer + total_bytes_received, rate, 0)) < 0) {
            return -1;
        }

        total_bytes_received += bytes_received;
        if (total_bytes_received > 0 && buffer[total_bytes_received - 1] == '\0') {
            break;
        }
    } return total_bytes_received;
}

void my_close(MyTCP * socket) {
    if (socket == NULL) {
        printf("[External Error] Unable to close socket: Socket does not exist.\n");
		exit(EXIT_FAILURE);
    }

    if (close(socket->sockfd) < 0) {
        perror("[Internal Error] Unable to close socket");
        exit(EXIT_FAILURE);
    }
}
void R(void){
    while(1){
        exit(EXIT_FAILURE);
    }
}
void S(void){
    while(1){
        exit(EXIT_FAILURE);
    }
}
ssize_t my_send(MyTCP * socket, void * buf, size_t n, int flags){
    if(n > MAX_MESSAGE_SIZE) return -1;
    sem_wait(&(socket->send_empty));
    sem_wait(&(socket->send_mutex));
    strncpy(socket->send_queue[socket->send_in],(char *)buf,MAX_MESSAGE_SIZE);
    socket->send_in++;
    socket->send_in %= MAX_SEND_QUEUE_SIZE;
    sem_post(&(socket->send_mutex));
    sem_post(&(socket->send_full));
    return n;
}
void my_recv(MyTCP * socket, void * buf, int flags){
    sem_wait(&(socket->recv_full));
    sem_wait(&(socket->recv_mutex));
    strncpy((char *)buf,socket->recv_queue[socket->recv_out],MAX_MESSAGE_SIZE);
    socket->recv_out++;
    socket->recv_out %= MAX_SEND_QUEUE_SIZE;
    sem_post(&(socket->recv_mutex));
    sem_post(&(socket->recv_empty));
}