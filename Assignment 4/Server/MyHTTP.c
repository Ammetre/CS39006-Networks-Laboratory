#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>

#define MAX_BUFFER_SIZE 1048576
#define MAX_METADATA_SIZE 1024

#define NUM_FIELDS 12
#define METHOD 0
#define URL 1
#define PROTOCOL 2
#define HOST 3
#define CONNECTION 4
#define DATE 5
#define ACCEPT 6
#define ACCEPT_LANGUAGE 7
#define IF_MODIFIED_SINCE 8
#define CONTENT_LANGUAGE 9
#define CONTENT_LENGTH 10
#define CONTENT_TYPE 11

#define OK 0
#define BAD_REQUEST 1
#define FORBIDDEN 2
#define NOT_FOUND 3
#define NOT_MODIFIED 4

const char * status_message[] = {"OK", "BAD REQUEST", "FORBIDDEN", "NOT FOUND", "NOT MODIFIED"};
const int status_code[] = {200, 400, 403, 404, 304};

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
    int state = 0;

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

int recv_with_size(int sockfd, char * buffer, int flags, size_t filesize) {
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

time_t get_gmt_offset() {
    time_t t = time(NULL);
    struct tm lt = {0};

    localtime_r(&t, &lt);   

    return lt.tm_gmtoff;
}

time_t http_time_to_rawtime(const char * http_time) {
    struct tm * tm_info = (struct tm *) malloc(sizeof(struct tm));
    char * http_time_copy = strdup(http_time);
    char * token = NULL;
    char * rest = http_time_copy;

    tm_info->tm_gmtoff = 0;

    token = strtok_r(rest, " ", &rest);

    if (token[0] == 'S') {
        if (token[1] == 'u') {
            tm_info->tm_wday = 0;
        } else {
            tm_info->tm_wday = 6;
        }
    } else if (token[0] == 'T') {
        if (token[1] == 'u') {
            tm_info->tm_wday = 2;
        } else {
            tm_info->tm_wday = 4;
        }
    } else if (token[0] == 'M') {
        tm_info->tm_wday = 1;
    } else if (token[0] == 'W') {
        tm_info->tm_wday = 3;
    } else {
        tm_info->tm_wday = 5;
    }

    token = strtok_r(rest, " ", &rest);

    if (token[0] == 'J') {
        if (token[1] == 'u') {
            if (token[2] == 'n') {
                tm_info->tm_mon = 5;
            } else {
                tm_info->tm_mon = 6;
            }
        } else {
            tm_info->tm_mon = 0;
        }
    } else if (token[0] == 'M') {
        if (token[2] == 'r') {
            tm_info->tm_mon = 2;
        } else {
            tm_info->tm_mon = 4;
        }
    } else if (token[0] == 'A') {
        if (token[1] == 'p') {
            tm_info->tm_mon = 3;
        } else {
            tm_info->tm_mon = 7;
        }
    } else {
        switch (token[0]) {
            case 'F':
                tm_info->tm_mon = 1;
                break;
            case 'S':
                tm_info->tm_mon = 8;
                break;
            case 'O':
                tm_info->tm_mon = 9;
                break;
            case 'N':
                tm_info->tm_mon = 10;
                break;
            case 'D':
                tm_info->tm_mon = 11;
                break;
            default:
                break;
        }
    }

    token = strtok_r(rest, " ", &rest);
    tm_info->tm_mday = atoi(token);

    token = strtok_r(rest, ":", &rest);
    tm_info->tm_hour = atoi(token);

    token = strtok_r(rest, ":", &rest);
    tm_info->tm_min = atoi(token);


    token = strtok_r(rest, " ", &rest);
    tm_info->tm_sec = atoi(token);


    token = strtok_r(rest, " ", &rest);
    tm_info->tm_year = atoi(token) - 1900;

    token = strtok_r(rest, "\0", &rest);
    tm_info->tm_zone = strdup(token);

    free(http_time_copy);
    time_t t = mktime(tm_info);
    free(tm_info);

    return t;
}

char * ctime_to_http_time(char * formatted_time) {
    formatted_time[strlen(formatted_time) - 1] = '\0';
    strcat(formatted_time, " GMT\0");

    char * http_time = (char *) malloc((strlen(formatted_time) + 2) * sizeof(char));

    int i, j, state;
    for (i = 0, j = 0, state = 0; formatted_time[i] != '\0'; i++, j++) {
        if (state == 0 && formatted_time[i] == ' ') {
            http_time[j] = ',';
            i--;
            state = 1;
        } else {
            http_time[j] = formatted_time[i];
        }
    } http_time[j] = '\0';

    return http_time;
}

char * get_http_time(int days_gone) {
    time_t rawtime;
    struct tm * tm_info;

    time(&rawtime);
    rawtime += days_gone * 24 * 3600;
    tm_info = gmtime(&rawtime);
    rawtime = mktime(tm_info);

    return ctime_to_http_time(ctime(&rawtime));
}

size_t get_size(const char * filename) {
    FILE* fp = fopen(filename, "r");
  
    fseek(fp, 0L, SEEK_END);
    size_t res = ftell(fp);
    fclose(fp);
  
    return res;
}

char * read_from_file(const char * filename) {
    size_t file_size = get_size(filename);
    char * buffer = (char *) malloc(file_size * sizeof(filename));
    FILE * fp = fopen(filename, "r");
    fread(buffer, 1, file_size, fp);
    return buffer;
}

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

size_t get_line_count(const char * request) {
    size_t line_count = 0;
    if (request == NULL) {
        return 0;
    }

    for (int i = 0; request[i] != '\0'; i++) {
        if (request[i] == '\n') {
            line_count++;
        }
    } return line_count;
}

char ** linearize(const char * request) {
    char * request_copy = strdup(request);
    char * rest = request_copy;
    printf("<Request>%s</Request>\n", request);
    size_t line_count = get_char_count(request, '\r') - 1;
    printf("%ld\n", line_count);
    if (line_count == 0) {
        return NULL;
    }

    char ** lines = (char **) malloc(line_count * sizeof(char *));
    for (size_t i = 0; i < line_count; i++) {
        lines[i] = strtok_r(rest, "\r", &rest); rest++;
        printf("<Line>%s</Line>\n", lines[i]);
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
    field_values[METHOD] = strtok_r(rest, " ", &rest);
    print_with_len(field_values[METHOD]);
    
    field_values[URL] = strdup(strtok_r(rest, " ", &rest));
    char * full_url = (char *) malloc(strlen((field_values[URL]) + 2) * sizeof(char));
    sprintf(full_url, ".%s", field_values[URL]);
    free(field_values[URL]);
    field_values[URL] = full_url;
    print_with_len(field_values[URL]);
    field_values[PROTOCOL] = strtok_r(rest, "\r", &rest); rest++;
    print_with_len(field_values[PROTOCOL]);
    

    for (int i = 1; i < line_count; i++) {
        char * rest = line_copies[i];
        char * field_name = strtok_r(rest, ":", &rest); rest++; 
        char * field_val = strtok_r(rest, "\r", &rest); rest++;
        print_with_len(field_val);

        if (strcmp(field_name, "Host") == 0) {
            field_values[HOST] = field_val;
        } else if (strcmp(field_name, "Connection") == 0) {
            field_values[CONNECTION] = field_val;
        } else if (strcmp(field_name, "Date") == 0) {
            field_values[DATE] = field_val;
        } else if (strcmp(field_name, "Accept") == 0) {
            field_values[ACCEPT] = field_val;
        } else if (strcmp(field_name, "Accept-Language") == 0) {
            field_values[ACCEPT_LANGUAGE] = field_val;
        } else if (strcmp(field_name, "If-Modified-Since") == 0) {
            field_values[IF_MODIFIED_SINCE] = field_val; 
        } else if (strcmp(field_name, "Content-Language") == 0) {
            field_values[CONTENT_LANGUAGE] = field_val; 
        } else if (strcmp(field_name, "Content-Length") == 0) {
            field_values[CONTENT_LENGTH] = field_val;
        } else if (strcmp(field_name, "Content-Type") == 0) {
            field_values[CONTENT_TYPE] = field_val;
        }
    } 

    return field_values;
}

int line_syntax_check(char ** lines, size_t linecount) {
    if (linecount < 2) {
        printf("LCK 1 - %ld\n", linecount);
        return 0;
    }

    if (get_char_count(lines[0], ' ') != 2) {
        printf("%s\n", lines[0]);
        printf("%d\n", get_char_count(lines[0], ' '));
        printf("LCK 2\n");
        return 0;
    }

    for (size_t i = 1; i < linecount; i++) {
        if (get_char_count(lines[i], ':') == 0 || get_char_count(lines[i], ' ') == 0) {
            printf("LCK 3 - %s\n", lines[i]);
            return 0;
        }
    }

    return 1;
}

int field_syntax_check(char ** field_values) {
    if (field_values == NULL) {
        printf("FCK 1\n");
        return 0;
    }

    if (field_values[METHOD] == NULL || field_values[URL] == NULL || field_values[PROTOCOL] == NULL) {
        printf("FCK 2\n");
        return 0;
    }


    if (strcmp(field_values[METHOD], "GET") == 0) {
        for (int i = 0; i <= IF_MODIFIED_SINCE; i++) {
            if (field_values[i] == NULL) {
                printf("FCK 3 - %d\n", i);
                return 0;
            }
        }
    } 
    
    // PUT may not require asize_t fields - If-Modified-Since
    else if (strcmp(field_values[METHOD], "PUT") == 0) {
        for (int i = 0; i <= CONTENT_TYPE; i++) {
            if (!field_values[i]) {
                return 0;
            }
        }
    } else {
        printf("FCK 4\n");
        return 0;
    }

    if (strcmp(field_values[PROTOCOL], "HTTP/1.1") != 0) {
        printf("FCK 5\n");
        return 0;
    }

    // Host check

    if (strcmp(field_values[CONNECTION], "close") != 0) {
        printf("FCK 6\n");
        return 0;
    }

    // Date check

    // Accept check

    // Accept-Language check

    if (strcmp(field_values[METHOD], "PUT") == 0) {
        // if (strcmp(field_values[CONTENT_TYPE], field_values[ACCEPT])) {
        //     printf("CK 8\n");
        //     return 0;
        // }
    }

    return 1;
}

char * expand_tilde(char * filename) {
    char * path = strdup(filename);
    if (filename[0] == '~') {
        char * home_dir = getpwuid(getuid())->pw_dir; 
        char * expanded_path = (char *) malloc((strlen(home_dir) + strlen(filename + 1) + 1) * sizeof(char));
        strcpy(expanded_path, home_dir);
        strcat(expanded_path, filename + 1); 
        free(path);
        return expanded_path;
    } return path;
}

char * get_last_modified(const char * filename) {
    struct stat filestat;
    stat(filename, &filestat);    
    time_t rawtime = filestat.st_mtim.tv_sec - get_gmt_offset();
    return ctime_to_http_time(ctime(&rawtime));
}   

void handle_get_request(int sockfd, char ** field_values, int status) {
    char response_metadata[MAX_METADATA_SIZE] = {0};
    char buffer[MAX_BUFFER_SIZE] = {0};

    if (strcmp(field_values[METHOD], "GET") != 0) {
        printf("Error [Improper function call]: Handler for GET request called!\n");
        return;
    }

    sprintf(response_metadata, "HTTP/1.1 %d %s\r\n", status_code[status], status_message[status]);
    sprintf(response_metadata + strlen(response_metadata), "Expires: %s\r\n", get_http_time(3));
    sprintf(response_metadata + strlen(response_metadata), "Cache-Control: no-store\r\n");

    if (status == OK) {
        size_t filesize = get_size(field_values[URL]);
        sprintf(response_metadata + strlen(response_metadata), "Content-Language: en-US\r\n");
        sprintf(response_metadata + strlen(response_metadata), "Content-Length: %ld\r\n", filesize);
        sprintf(response_metadata + strlen(response_metadata), "Content-Type: %s\r\n", field_values[ACCEPT]);
        sprintf(response_metadata + strlen(response_metadata), "Last-Modified: %s\r\n\r\n", get_last_modified(field_values[URL]));

        printf("\n<Metadata>%s</Metadata>\n", response_metadata);
        printf("%ld bytes of Metadata are generated.\n", strlen(response_metadata));
        size_t bytes_sent = send(sockfd, response_metadata, strlen(response_metadata), 0);
        printf("%ld bytes of Metadata are sent.\n", bytes_sent);

        FILE * fp = fopen(field_values[URL], "rb");
        size_t bytes_read = fread(buffer, 1, filesize, fp);
        printf("%ld bytes of content are read.\n", bytes_read);

        bytes_sent = send(sockfd, buffer, filesize, 0);
        printf("%ld bytes of content are sent.\n", bytes_sent);
    } else {
        send(sockfd, response_metadata, strlen(response_metadata) + 1, 0);
    }    
}

void handle_put_request(int sockfd, char ** field_values, int status) {
    char response_metadata[MAX_METADATA_SIZE] = {0};
    char buffer[MAX_BUFFER_SIZE] = {0};

    if (strcmp(field_values[METHOD], "PUT") != 0) {
        printf("Error [Improper function call]: Handler for PUT request called!\n");
        return;
    }

    size_t filesize = atoi(field_values[CONTENT_LENGTH]);
    size_t bytes_received = recv_with_size(sockfd, buffer, 0, filesize);
    printf("%ld bytes of content were received.", bytes_received);

    if (status == OK) {
        FILE * fp = fopen(field_values[URL], "wb");
        fwrite(buffer, 1, filesize, fp);
        fclose(fp);
    }

    sprintf(response_metadata, "HTTP/1.1 %d %s\r\n", status_code[status], status_message[status]);
    sprintf(response_metadata + strlen(response_metadata), "Expires: %s\r\n", get_http_time(3));
    if (status != NOT_FOUND || status != FORBIDDEN) {
        sprintf(response_metadata + strlen(response_metadata), "Cache-Control: no-store\r\n");
        sprintf(response_metadata + strlen(response_metadata), "Last-Modified: %s\r\n\r\n", get_last_modified(field_values[URL]));
    } else {
        sprintf(response_metadata + strlen(response_metadata), "Cache-Control: no-store\r\n\r\n");
    } send(sockfd, response_metadata, strlen(response_metadata), 0);
}

char ** get_status(const char * request, int * status) {
    size_t line_count = get_char_count(request, '\r') - 1;
    char ** lines = linearize(request);
    if (!line_syntax_check(lines, line_count)) {
        *status = BAD_REQUEST;
        return NULL;
    }

    char ** field_values = get_fields(lines, line_count);
    free(lines);

    if (!field_syntax_check(field_values)) {
        *status = BAD_REQUEST;
        return field_values;
    } 

    if (strcmp(field_values[METHOD], "GET") == 0 && access(field_values[URL], F_OK) != 0) {
        *status = NOT_FOUND;
        return field_values;
    }

    // printf("\nNOT FOUND Check Complete\n");


    // if (strcmp(field_values[METHOD], "GET") == 0 && access(field_values[URL], R_OK) != 0) {
    //     *status = FORBIDDEN;
    //     printf("\nStatus Check Complete\n");
    //     printf("<Staus>%d %s</Status>\n", status_code[*status], status_message[*status]);
    //     return field_values;
    // } else {
    //     if (access(field_values[URL], F_OK) == 0 || access(field_values[URL], W_OK) != 0) {
    //         *status = FORBIDDEN;
    //         printf("\nStatus Check Complete\n");
    //         printf("<Staus>%d %s</Status>\n", status_code[*status], status_message[*status]);
    //         return field_values;
    //     }
    // }

    // printf("\nFORBIDDEN Check Complete\n");

    time_t pivot = http_time_to_rawtime(field_values[IF_MODIFIED_SINCE]);
    time_t modified = http_time_to_rawtime(get_last_modified(field_values[URL]));


    // if (modified < pivot) {
    //     *status = NOT_MODIFIED;
    //     printf("\nStatus Check Complete\n");
    //     printf("<Staus>%d %s</Status>\n", status_code[*status], status_message[*status]);
    //     return field_values;
    // }

    // printf("\nLAST MODIFIED Check Complete\n");

    // printf("\nStatus Check Complete\n");
    // printf("<Staus>%d %s</Status>\n", status_code[*status], status_message[*status]);

    *status = OK;
    return field_values;
}

int main() {
    int sockfd, newsockfd;
    struct sockaddr_in server_address, client_address;
    socklen_t client_address_length;
    char request_metadata[1024];

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Error [Unable to create socket]");
        exit(EXIT_FAILURE);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_address.sin_port = htons(8080);

    if (bind(sockfd, (struct sockaddr *) &server_address, sizeof(server_address)) == -1) {
        perror("Error [Unable to bind]");
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, 5) == -1) {
        perror("Error [Unable to listen]");
        exit(EXIT_FAILURE);
    }

    while (1) {
        client_address_length = sizeof(client_address);
        if ((newsockfd = accept(sockfd, (struct sockaddr *) &client_address, &client_address_length)) == -1) {
            perror("Error [Unable to accept client request]");
            exit(EXIT_FAILURE);
        }

        if (fork() == 0) {
            close(sockfd);

            memset(request_metadata, 0, sizeof(request_metadata));
            if (recv_bytewise(newsockfd, request_metadata, sizeof(request_metadata)) == -1) {
                printf("Error [Unable to receive in chunks]: Not asize_t bytes were received\n");
                exit(EXIT_FAILURE);
            } printf("%s", request_metadata);

            int status = OK;
            char ** field_values = get_status(request_metadata, &status);
            if (strcmp(field_values[METHOD], "GET") == 0) {
                handle_get_request(newsockfd, field_values, status);
            } else {
                handle_put_request(newsockfd, field_values, status);
            }
            
            free(field_values);
            close(newsockfd);
            exit(EXIT_SUCCESS);
        }

        close(newsockfd);
    }

    return 0;
}
