#include "mysocket.h"

#define MAX_BUFFER_SIZE 1024
#define MAX_REQUESTS 2

int main(int argc, char * argv[]) {

    /****************\
    | The port number |
    \****************/
    int port;
    if (argc == 1) {
        printf("Error [Unable to get port number]: Run as %s <server port number>.\n", argv[0]);
        exit(EXIT_FAILURE);
    } else {
        port = atoi(argv[1]);
    }

    /************\
    | The sockets |
    \************/
    MyTCP * socket, * newsocket;

    /******************************************\
    | The server address and the client address |
    \******************************************/
	struct sockaddr_in client_address, server_address;

    /**************************\
    | The client address length |
    \**************************/
    socklen_t client_address_length;

    /**********************************\
    | The buffer used for communication |
    \**********************************/
	char buffer[MAX_BUFFER_SIZE];

    /******************\
    | Create the socket |
    \******************/
    socket = my_socket(AF_INET, SOCK_MyTCP, 0);

    /**************************\
    | Define the server address |
    \**************************/
	server_address.sin_family		= AF_INET;
	server_address.sin_addr.s_addr	= INADDR_ANY;
	server_address.sin_port		    = htons(port);

    /**************************************\
    | Bind the socket to the server address |
    \**************************************/
	my_bind(socket, (struct sockaddr *) &server_address, sizeof(server_address));

    /*****************\
    | Start the server |
    \*****************/
	my_listen(socket, 5);

	printf("\nServer running on port %d.\n\n", port);

    /*******************\
    | Number of requests |
    \*******************/
    int requests = 0;

    while (requests < MAX_REQUESTS) {

        /************************************************************************\
        | Accept an incoming client connection request and get the client details |
        \************************************************************************/
        newsocket = my_accept(socket, (struct sockaddr *) &client_address, &client_address_length);

        requests++;
        printf("%d\n", newsocket->sockfd);
        my_recv(newsocket, buffer, 0);
        printf("Message from client: %s\n", buffer);
        memset(buffer, 0, sizeof(buffer));
        strcpy(buffer, "Geralt of Rivia");
        my_send(newsocket, buffer, strlen(buffer) + 1, 0);

        my_close(newsocket);
        printf("Closed!\n");
    }

    my_close(socket);

    return 0;
}