#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/poll.h>

#define MAX_BUFFER_SIZE 1048576
#define MAX_METADATA_SIZE 1024
int errno;

#define BLACK "\033[30m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"
#define PURPLE "\033[35m"
#define CYAN "\033[36m"
#define WHITE "\033[37m"
#define DEFAULT "\033[0m"

#define BG_BLACK "\033[40m"
#define BG_RED "\033[41m"
#define BG_GREEN "\033[42m"
#define BG_YELLOW "\033[43m"
#define BG_BLUE "\033[44m"
#define BG_PURPLE "\033[45m"
#define BG_CYAN "\033[46m"
#define BG_WHITE "\033[47m"

#define BOLD "\033[1m"
#define ITALIC "\033[3m"
#define UNDERLINE "\033[4m"

#define NUM_FIELDS 9
#define PROTOCOL 0
#define STATUS 1
#define STATUS_MESSAGE 2
#define EXPIRES 3
#define CACHE_CONTROL 4
#define CONTENT_LANGUAGE 5
#define CONTENT_LENGTH 6
#define CONTENT_TYPE 7
#define LAST_MODIFIED 8

#define OK 0
#define BAD_REQUEST 1
#define FORBIDDEN 2
#define NOT_FOUND 3

typedef long long ll;

int get_char_count(const char * s, char c) {
    if (s == NULL) {
        return 0;
    }

    int count = 0;
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == c) {
            count++;
        }
    } return count;
}

char ** linearize(const char * request) {
    char * request_copy = strdup(request);
    char * rest = request_copy;
    size_t line_count = get_char_count(request, '\r') - 1;
    if (line_count == 0) {
        return NULL;
    }

    char ** lines = (char **) malloc(line_count * sizeof(char *));
    for (size_t i = 0; i < line_count; i++) {
        lines[i] = strtok_r(rest, "\r", &rest); rest++;
        // printf("<Line>%s</Line>\n", lines[i]);
    } return lines;
}

void print_with_len(const char * field_val) {
    printf("%s %ld\n", field_val, strlen(field_val));
}

char ** get_fields(char ** lines, size_t line_count) {
    char ** line_copies = (char **) malloc(line_count * sizeof(char *));
    for (size_t i = 0; i < line_count; i++) {
        line_copies[i] = strdup(lines[i]);
    }

    char ** field_values = (char **) malloc(NUM_FIELDS * sizeof(char *));

    char * rest = line_copies[0];
    field_values[PROTOCOL] = strtok_r(rest, " ", &rest);
    // print_with_len(field_values[PROTOCOL]);
    field_values[STATUS] = strtok_r(rest, " ", &rest);
    // print_with_len(field_values[STATUS]);
    field_values[STATUS_MESSAGE] = strtok_r(rest, "\r", &rest); rest++;
    // print_with_len(field_values[STATUS_MESSAGE]);

    for (int i = 1; i < line_count; i++) {
        char * rest = line_copies[i];
        char * field_name = strtok_r(rest, ":", &rest); rest++;
        char * field_val = strtok_r(rest, "\r", &rest); rest++;
        // print_with_len(field_val);

        if (strcmp(field_name, "Expires") == 0) {
            field_values[EXPIRES] = field_val;
        } else if (strcmp(field_name, "Cache-Control") == 0) {
            field_values[CACHE_CONTROL] = field_val;
        } else if (strcmp(field_name, "Content-Language") == 0) {
            field_values[CONTENT_LANGUAGE] = field_val;
        } else if (strcmp(field_name, "Content-Length") == 0) {
            field_values[CONTENT_LENGTH] = field_val;
        } else if (strcmp(field_name, "Content-Type") == 0) {
            field_values[CONTENT_TYPE] = field_val;
        } else if (strcmp(field_name, "Last-Modified") == 0) {
            field_values[LAST_MODIFIED] = field_val; 
        } else {
            // free(field_name);
            // free(field_val);
        }
    } 

    return field_values;
}


void info(char * prompt) {
    fprintf(stderr, BG_BLUE YELLOW ITALIC);
    fprintf(stderr, "%s", prompt);
    fprintf(stderr, DEFAULT);
    fprintf(stderr, "\n");
    fflush(stdout);
}

void info_int(int k) {
    char intg[10];
    sprintf(intg, "%d", k);
    info(intg);
}

void error(char * prompt) {
    fprintf(stderr, RED BOLD);
    perror(prompt);
    fprintf(stderr, DEFAULT);
    //fprintf(stderr, "\n");
    fflush(stdout);
}

void prompt() {
    printf(GREEN);
    printf("MyOwnBrowser");
    printf(DEFAULT BOLD);
    printf("> ");
    printf(DEFAULT);
    fflush(stdout);
}

int min(int a, int b) {
    return a < b ? a : b;
}

int send_in_chunks(int sockfd, const char * buffer, size_t buffer_size, int flags, size_t chunk_size) {
    int bytes_sent = 0, bytes_left = (int) buffer_size, total_bytes_sent = 0;

    while (bytes_left > 0) {
        if ((bytes_sent = send(sockfd, buffer + total_bytes_sent, min(bytes_left, chunk_size), flags)) == -1) {
            perror("Error [Unable to send]");
            errno = 0;
            return total_bytes_sent;
        }

        total_bytes_sent += bytes_sent;
        bytes_left -= bytes_sent;
    } return total_bytes_sent;
}

int recv_bytewise(int sockfd, char * buffer, int flags) {
    int bytes_received = 0, total_bytes_received = 0;

    while (1) {
        if ((bytes_received = recv(sockfd, buffer + total_bytes_received, 1, flags)) == -1) {
            perror("Error [Unable to receive]");
            errno = 0;
            return -1;
        }

        total_bytes_received += bytes_received;
        if (total_bytes_received >= 4 && buffer[total_bytes_received - 1] == '\n' && buffer[total_bytes_received - 2] == '\r' && buffer[total_bytes_received - 3] == '\n' && buffer[total_bytes_received - 4] == '\r') {    
            break;
        }
    } return total_bytes_received - 4;
}

int recv_with_size(int sockfd, char * buffer, size_t buffer_size, int flags, size_t filesize) {
    int bytes_received = 0, total_bytes_received = 0;

    while (1) {
        if ((bytes_received = recv(sockfd, buffer + total_bytes_received, 1, flags)) == -1) {
            perror("Error [Unable to receive]");
            errno = 0;
            return -1;
        }

        total_bytes_received += bytes_received;
        if (total_bytes_received == filesize) {
            break;
        }
    } return total_bytes_received;
}

void get_stream_send(char * host_name, char * url_path, const char * type, int port, int sockfd) {
    char get_stream[5120] = {0};

    time_t rawtime, prevtime;
    struct tm * timeinfo, * previnfo;

    time(&rawtime);
    prevtime = rawtime - 2*86400;
    timeinfo = gmtime(&rawtime);

    char dat_str[128];
    char mod_str[128];
    strftime(dat_str, 127, "%a, %b %d %T %Y %Z",timeinfo);
    previnfo = gmtime(&prevtime);
    strftime(mod_str, 127, "%a, %b %d %T %Y %Z",previnfo);

    sprintf(get_stream, "GET %s HTTP/1.1\r\nHost: %s\r\nDate: %s\r\nAccept: %s\r\nAccept-Language: en-Us,en;q=0.5\r\nIf-Modified-Since: %s\r\nConnection: close\r\n\r\n", url_path, host_name, dat_str, type, mod_str);
    // printf("%s\n", get_stream);
    send(sockfd, get_stream, strlen(get_stream), 0);
}

void get_recv(int sockfd, char * filename, char * content_type) {
    char response_metadata[MAX_METADATA_SIZE] = {0};
    char buffer[MAX_BUFFER_SIZE] = {0};

    struct pollfd fdset[1];
    fdset[0].fd = sockfd;
    fdset[0].events = POLLIN;
    int ret = poll(fdset, 1, 3000);

    switch (ret) {
        case -1:
            perror("Error [Unable to poll]");
            break;
        case 0:
            printf("Error [Timeout Exceeded]: Server not responsive\n");
            break;
        default:
            recv_bytewise(sockfd, response_metadata, 0);
            printf("\n%s\n", response_metadata);
            // printf("\nResponse Received!\n");
            char ** lines = linearize(response_metadata);
            // printf("\nLinearization Complete!\n");
            char ** field_values = get_fields(lines, get_char_count(response_metadata, '\r') - 1);
            // printf("\nObtained Fields!\n");
            free(lines);

            if (strcmp(field_values[STATUS_MESSAGE], "OK") == 0) {
                FILE* fp = fopen(filename, "wb");
                size_t filesize = atoi(field_values[CONTENT_LENGTH]);
                // printf("%ld bytes of content are to be received.\n", filesize);
                recv_with_size(sockfd, buffer, sizeof(buffer), 0, filesize);
                // printf("%ld bytes of content were received.\n", bytes_received);
                fwrite(buffer, 1, filesize, fp);
                // printf("%ld bytes of content were written.\n", bytes_written);
                fclose(fp);
            } else {
                fflush(stdout);
                return;
            }
    }


    if (fork()== 0) {
        execlp("xdg-open", "xdg-open", filename, NULL);
        exit(EXIT_SUCCESS);
    } else {
        fflush(stdout);
    }
}

void get_proc(){

    char place[4096];
    scanf("%s",place);

    int site_slash = 0;
    int port_colon = strlen(place)-1;
    int port_number = 80;

    while (port_colon >= 0 && place[port_colon] <= '9' && place[port_colon] >= '0') port_colon--;
    if (!(port_colon >= 0 && place[port_colon]==':')) port_colon = strlen(place);
    if (port_colon != strlen(place)) {
        char port[strlen(place) - port_colon];
        int i;
        for(i = port_colon + 1; i < strlen(place); i++) {
            port[i - port_colon - 1] = place[i];
        }

        port[i - port_colon - 1] = '\0';
        if (strlen(port)) {
            port_number = atoi(port);
        }
    }

    // info_int(port_colon);
    // info_int(port_number);

    char if_head[8];
    for(int i = 0; i < 7; i++){
        if_head[i] = place[i];
    }

    if_head[7] = '\0';
    if(strcmp(if_head, "http://") == 0){
        site_slash = 7;
    }

    for(; site_slash < port_colon && place[site_slash] != '/' ; site_slash++);
    // info_int(site_slash);
    int len_url = 2;
    if(site_slash < port_colon){
        len_url = port_colon - site_slash + 1;
    }

    char URL[len_url];
    if(port_colon == site_slash) URL[0]='/';
    else {
        for (int i = 0 ; i < len_url - 1 ; i++) {
            URL[i] = place[i + site_slash];
        }
    } URL[len_url - 1] = '\0';
    // info(URL);

    int len_host = site_slash + 1;
    if (strcmp(if_head, "http://") == 0) {
        len_host -= 7;
    }

    char host[len_host]; 
    for(int i = 1; i < len_host; i++) {
        host[len_host - 1 - i] = place[site_slash - i];
    }

    host[len_host - 1] = 0;
    // info(host);

    int type_dot = strlen(URL)-1;
    for(; type_dot >= 0 && URL[type_dot] != '.'; type_dot--);
    // info_int(type_dot);
    int len_type;
    if(type_dot == -1){
        len_type = 2;
    }
    else len_type = strlen(URL) - type_dot;
    char type[len_type];
    if(type_dot == -1) type[0]='*';
    else {
        for (int i = strlen(URL) - 1; i > type_dot; i--) {
            type[i - type_dot - 1] = URL[i]; 
        }
    }

    char* filename = strrchr(URL,'/');
    filename += 1;
    // info(filename);
    type[len_type - 1] = '\0';
    // info(type);

    int sockfd;
    struct sockaddr_in serv_addr;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Unable to create socket\n");
		exit(0);
	}

	serv_addr.sin_family	= AF_INET;
	inet_aton(host, &serv_addr.sin_addr);
    // inet_aton("203.110.245.250", &serv_addr.sin_addr);
	// serv_addr.sin_port	= htons(port_number);
	serv_addr.sin_port	= htons(port_number);


	if ((connect(sockfd, (struct sockaddr *) &serv_addr,
						sizeof(serv_addr))) < 0) {
		perror("Unable to connect to server\n");
		exit(0);
	}

    if(strcmp(type,"html") == 0)
        get_stream_send(host, URL, "text/html", port_number, sockfd);
    else if(strcmp(type,"pdf") == 0)
        get_stream_send(host, URL, "application/pdf", port_number, sockfd);
    else if(strcmp(type,"jpg") == 0)
        get_stream_send(host, URL, "image/jpeg", port_number, sockfd);
    else
        get_stream_send(host, URL, "text/*", port_number, sockfd);

    get_recv(sockfd, filename, type);
    close(sockfd);
}

void put_stream_send (char * host_name, char * url_path, const char * type, int port, int sockfd, char * fileplace) {

    char put_stream[MAX_BUFFER_SIZE] = {0};
    time_t rawtime, prevtime;
    struct tm * timeinfo, * previnfo;
    time(&rawtime);
    prevtime = rawtime - 2*86400;
    timeinfo = gmtime(&rawtime);

    char dat_str[128];
    char mod_str[128];
    strftime(dat_str, 127, "%a, %b %d %T %Y %Z",timeinfo);
    previnfo = gmtime(&prevtime);
    strftime(mod_str, 127, "%a, %b %d %T %Y %Z",previnfo);
    
    FILE* f = fopen(fileplace,"r");
    fseek(f, 0, SEEK_END); // seek to end of file
    size_t size = ftell(f); // get current file pointer
    rewind(f);
    fclose(f);
    char* type_str = strrchr(fileplace,'.');
    type_str = type_str + 1;
    if (strcmp(type_str,"html") == 0) {
        sprintf(put_stream, "PUT %s/%s HTTP/1.1\r\nHost: %s\r\nDate: %s\r\nAccept: %s\r\nAccept-Language: en-us, en;q=0.7\r\nIf-Modified-Since: %s\r\nConnection: close\r\nContent-Language: en-us\r\nContent-Type: text/html\r\nContent-Length: %lu\r\n\r\n",url_path, fileplace, host_name, dat_str,type,mod_str,size);
    } else if (strcmp(type_str,"pdf") == 0) {
        sprintf(put_stream, "PUT %s/%s HTTP/1.1\r\nHost: %s\r\nDate: %s\r\nAccept: %s\r\nAccept-Language: en-us, en;q=0.7\r\nIf-Modified-Since: %s\r\nConnection: close\r\nContent-Language: en-us\r\nContent-Type: application/pdf\r\nContent-Length: %lu\r\n\r\n",url_path, fileplace, host_name, dat_str,type,mod_str,size);
    } else if (strcmp(type_str,"jpeg")==0) {
        sprintf(put_stream, "PUT %s/%s HTTP/1.1\r\nHost: %s\r\nDate: %s\r\nAccept: %s\r\nAccept-Language: en-us, en;q=0.7\r\nIf-Modified-Since: %s\r\nConnection: close\r\nContent-Language: en-us\r\nContent-Type: image/jpeg\r\nContent-Length: %lu\r\n\r\n",url_path, fileplace, host_name, dat_str,type,mod_str,size);
    } else {
        sprintf(put_stream, "PUT %s/%s HTTP/1.1\r\nHost: %s\r\nDate: %s\r\nAccept: %s\r\nAccept-Language: en-us, en;q=0.7\r\nIf-Modified-Since: %s\r\nConnection: close\r\nContent-Language: en-us\r\nContent-Type: text/*\r\nContent-Length: %lu\r\n\r\n",url_path, fileplace, host_name, dat_str,type,mod_str,size);
    }

    // printf("<Metadata>%s</Metadata>\n", put_stream);
    // printf("%ld bytes of Metadata were generated.\n", strlen(put_stream));
    send(sockfd, put_stream, strlen(put_stream), 0);
    // printf("%ld bytes of Metadata were sent.\n", bytes_sent);

    char buffer[MAX_BUFFER_SIZE];
    FILE* fp = fopen(fileplace, "rb");
    fread(buffer, 1, size, fp);
    // printf("%ld bytes of content were read.\n", bytes_read);
    send(sockfd, buffer, size, 0);
    // printf("%ld bytes of content were sent.\n", bytes_sent);

    fclose(fp);
}

void put_recv (int sockfd) {
    char response_metadata[MAX_METADATA_SIZE] = {0};
    recv_bytewise(sockfd, response_metadata, 0);
    printf("\n%s\n", response_metadata);
}

void put_proc() {
    char place[4096], fileplace[1024];
    scanf("%s",place);
    scanf("%s",fileplace);
    int site_slash = 0;
    int port_colon = strlen(place)-1;
    int port_number = 80;
    while(port_colon >= 0 && place[port_colon] <='9' && place[port_colon] >= '0') port_colon--;
    if(!(port_colon >= 0 && place[port_colon]==':')) port_colon=strlen(place);
    if(port_colon != strlen(place)){
        char port[strlen(place) - port_colon];
        int i;
        for(i=port_colon + 1; i < strlen(place); i++){
            port[i - port_colon - 1] = place[i];
        }
        port[i - port_colon - 1] = '\0';
        if(strlen(port)){
            port_number = atoi(port);
        }
    }
    // info_int(port_colon);
    // info_int(port_number);
    char if_head[8];
    for(int i = 0; i < 7; i++){
        if_head[i] = place[i];
    }
    if_head[7] = '\0';
    if(strcmp(if_head, "http://") == 0){
        site_slash = 7;
    }
    for(; site_slash < port_colon && place[site_slash] != '/' ; site_slash++);
    // info_int(site_slash);
    int len_url = 2;
    if(site_slash < port_colon){
        len_url = port_colon - site_slash + 1;
    }
    char URL[len_url];
    if(port_colon == site_slash) URL[0]='/';
    else{
        for(int i = 0 ; i < len_url - 1 ; i++){
            URL[i] = place[i + site_slash];
        }
    }
    URL[len_url - 1] = '\0';
    // info(URL);
    int len_host = site_slash + 1;
    if(strcmp(if_head, "http://") == 0){
        len_host -= 7;
    }
    char host[len_host]; 
    for(int i = 1; i < len_host; i++){
        host[len_host - 1 - i] = place[site_slash - i];
    }
    host[len_host - 1] = 0;
    // info(host);
    int type_dot = strlen(URL)-1;
    for(; type_dot >= 0 && URL[type_dot] != '.'; type_dot--);
    // info_int(type_dot);
    int len_type;
    if(type_dot == -1){
        len_type = 2;
    }
    else len_type = strlen(URL) - type_dot;
    char type[len_type];
    if(type_dot == -1) type[0]='*';
    else{
    for(int i = strlen(URL) - 1; i > type_dot; i--){
       type[i - type_dot - 1] = URL[i]; 
    }
    }
    char* filename = strrchr(URL,'/');
    filename += 1;
    // info(filename);
    type[len_type - 1] = '\0';
    // info(type);

    int sockfd;
    struct sockaddr_in serv_addr;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Unable to create socket\n");
		exit(0);
	}

	serv_addr.sin_family	= AF_INET;
	inet_aton(host, &serv_addr.sin_addr);
	serv_addr.sin_port	= htons(port_number);

	/* With the information specified in serv_addr, the connect()
	   system casize_t establishes a connection with the server process.
	*/
	if ((connect(sockfd, (struct sockaddr *) &serv_addr,
						sizeof(serv_addr))) < 0) {
		perror("Unable to connect to server\n");
		exit(0);
	}

    if(strcmp(type,"html") == 0)
        put_stream_send(host, URL, "text/html", port_number, sockfd, fileplace);
    else if(strcmp(type,"pdf") == 0)
        put_stream_send(host, URL, "application/pdf", port_number, sockfd, fileplace);
    else if(strcmp(type,"jpg") == 0)
        put_stream_send(host, URL, "image/jpeg", port_number, sockfd, fileplace);
    else
        put_stream_send(host, URL, "text/*", port_number, sockfd, fileplace);
    put_recv(sockfd);
    close(sockfd);
    return;
}

int main(int argc, char* argv[]){

    while(1) { 
        prompt();
        char cmd[5];
        scanf("%s",cmd);
        if(strcmp(cmd,"QUIT") == 0) break;
        else if(strcmp(cmd,"GET") == 0) get_proc();
        else if(strcmp(cmd,"PUT") == 0) put_proc();
        else{
            char S[4096];
            scanf("%[^\n]%*c",S);
            //printf("%s\n",S);
            error("Invalid Option");
        }
    }
}
