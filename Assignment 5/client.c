#include "mysocket.h"

#define MAX_BUFFER_SIZE 1024

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

    /***********\
    | The socket |
    \***********/
	MyTCP * socket;

    /*******************\
    | The server address |
    \*******************/
	struct sockaddr_in server_address;

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
	server_address.sin_family	= AF_INET;
	server_address.sin_port	= htons(port);
	inet_aton("127.0.0.1", &server_address.sin_addr);

    /*****************************************\
    | Connect the socket to the server address |
    \*****************************************/
	my_connect(socket, (struct sockaddr *) &server_address, sizeof(server_address));

    memset(buffer, 0, sizeof(buffer));
    strcpy(buffer, "Yennefer of Vengerberg");
    my_send(socket, buffer, strlen(buffer) + 1, 0);

    memset(buffer, 0, sizeof(buffer));
    my_recv(socket, buffer, 0);
    printf("Message from server: %s\n", buffer);

    my_close(socket);
    free(socket);
    // free(argv);
    // if (socket == NULL) {
    //     printf("Freee Succeesss\n");
    // }
    // printf("Hello\n");
    return 0;
}