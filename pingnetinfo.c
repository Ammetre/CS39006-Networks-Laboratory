#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <wchar.h>
#include <locale.h>

#define MAX_HOPS 64
#define WORD_SIZE 500
#define MAX_WORDS 3
#define MAX_PACKET_SIZE 4096
#define MAX_BANDWIDTH_PROBES 10
#define MAX_DISCOVERY_PROBES 2

// The commandline parameters
char remotehost_name[256];
int n;
float T;

// The IP addresses of the localhost and the remotehost
char localhost_ip[256];
char remotehost_ip[256];

// The packet and packetsize
char packet[MAX_PACKET_SIZE];
int packet_size;

// The sendto() and its corresponding recvfrom() timestamps
struct timeval * send_time, * recv_time;

// rtt[i][j][k] = RTT to the node reached with ttl=i (ith hop), with payload size j * WORD_SIZE, for the kth probe (0 <= k < n)
// rtt[0][j][k] = 0 for all j and k as RTTs from a node to itself is always 0
float rtt[MAX_HOPS][MAX_WORDS][MAX_BANDWIDTH_PROBES];

struct probe_info {
    int ip_count;
    char ips[MAX_DISCOVERY_PROBES][256];
    int freq[MAX_DISCOVERY_PROBES];
};

struct probe_info * create_probe_info() {
    struct probe_info * pi = (struct probe_info *) malloc(sizeof(struct probe_info));
    pi->ip_count = 0;
    memset(pi->ips, 0, sizeof(pi->ips));
    return pi;
}

void add_to_probe_info(struct probe_info * pi, const char * ip) {
    for (int i = 0; i < pi->ip_count; i++) {
        if (strcmp(pi->ips[i], ip) == 0) {
            pi->freq[i]++;
            return;
        }
    }


    strcpy(pi->ips[pi->ip_count], ip);
    pi->freq[pi->ip_count] = 1;
    pi->ip_count++;
}

char * get_finalised_ip(struct probe_info * pi) {
    int max_freq = 0;
    char finalised_ip[256] = {0};
    for (int i = 0; i < pi->ip_count; i++) {
        if (max_freq < pi->freq[i]) {
            max_freq = pi->freq[i];
            strcpy(finalised_ip, pi->ips[i]);
        } 
    } 
    
    return strdup(finalised_ip);
}

float min(float a, float b) {
    return a < b ? a : b;
}

float max(float a, float b) {
    return a > b ? a : b;
}

void set_localhost_ip() {
    char * hostname = calloc(15,sizeof(char));
    gethostname(hostname, sizeof(hostname));
    char * host_addr_temp = inet_ntoa(*((struct in_addr*)(gethostbyname(hostname)->h_addr_list[0])));
    memset(localhost_ip, 0, sizeof(localhost_ip));
    strcpy(localhost_ip, host_addr_temp);

}

void set_remotehost_ip() {
    memset(remotehost_ip, 0, sizeof(remotehost_ip));
    struct hostent * host;
    if (inet_addr(remotehost_name) == INADDR_NONE) {
        if ((host = gethostbyname(remotehost_name)) == NULL) {
            perror("[Error] gethostbyname()");
            exit(EXIT_FAILURE);
        } strcpy(remotehost_ip, inet_ntoa(*((struct in_addr *) (host->h_addr_list[0]))));
    } else {
        strcpy(remotehost_ip, remotehost_name);
    } 
}

unsigned short get_16bit_checksum(unsigned short * header, int header_len) {
    int bytes_left = header_len;
    int sum = 0;
    unsigned short * header_itr = header;
    unsigned short checksum = 0;

    while (bytes_left > 1) {
        sum += *header_itr++;
        bytes_left -= sizeof(unsigned short);
    }

    if (bytes_left == 1) {
        *(unsigned char*)(&checksum) = *(unsigned char*) header_itr;
        sum += checksum;
    }

    sum = (sum >> 16) + (sum &0xffff);
    sum += (sum >> 16);
    checksum = ~sum;
    return checksum;
}

void create_icmp_packet(const char * src_ip, const char * dest_ip, int ttl, int packet_id, char * __data, int __data_size) {
    struct iphdr * ip_header;
    struct icmp * icmp;

    memset(packet, 0, sizeof(packet));

    // Build the IP part of the ICMP packet
    ip_header = (struct iphdr *) packet;
    ip_header->ihl = 5;
    ip_header->version = 4;
    ip_header->tos = 0;
    ip_header->tot_len = 20 + 8 + __data_size;
    ip_header->id = htons(12345);
    ip_header->frag_off = 0;
    ip_header->ttl = ttl;
    ip_header->protocol = IPPROTO_ICMP;
    ip_header->check = 0;
    ip_header->saddr = inet_addr(localhost_ip);
    ip_header->daddr = inet_addr(remotehost_ip);
    ip_header->check = get_16bit_checksum((unsigned short *) packet, 20);

    //Build the ICMP part of the ICMP packet
    icmp = (struct icmp *) (packet + 20);
    icmp->icmp_type = ICMP_ECHO;
    icmp->icmp_code = 0;
    icmp->icmp_cksum = 0;
    icmp->icmp_seq = packet_id;
    icmp->icmp_id = getpid();
    char * data = (char *) icmp->icmp_data;
    memcpy(data, __data, __data_size);
    icmp->icmp_cksum = get_16bit_checksum((unsigned short *) icmp, 8 + __data_size);

    // Set the packet size
    packet_size = 28 + __data_size;
}

void send_icmp_packet(int sockfd, const char * intermediate_ip) {
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = inet_addr(intermediate_ip);

    gettimeofday(send_time, NULL);
    if (sendto(sockfd, packet, packet_size, 0, (struct sockaddr *) &dest_addr, sizeof(dest_addr)) == -1) {
        perror("[Error] sendto()");
        exit(EXIT_FAILURE);
    } 
    
    // wprintf(L"\nSent packet of size %d to %s.\n", packet_size, intermediate_ip);
    #ifdef VERBOSE
        print_packet_details();
    #endif
}

char * recv_icmp_packet(int sockfd) {
    struct sockaddr_in src_addr;
    socklen_t src_addr_len = sizeof(src_addr);

    if ((packet_size = recvfrom(sockfd, packet, packet_size, 0, (struct sockaddr *) &src_addr, &src_addr_len)) == -1) {
        if (errno == EAGAIN) {
            wprintf(L"[Error] recvfrom(): Could not receive packet.\n");
            exit(EXIT_FAILURE);
        } 

        perror("[Error] recvfrom()");
        exit(EXIT_FAILURE);
    }
    gettimeofday(recv_time, NULL);

    // wprintf(L"Received packet of size %d from %s.\n\n", packet_size, inet_ntoa(src_addr.sin_addr));
    #ifdef VERBOSE
        print_packet_details();
    #endif
    return inet_ntoa(src_addr.sin_addr);
}

void print_packet_details() {
    struct iphdr * ip_header;
    struct icmp * icmp;
    
    wprintf(L"\n\n\033[31m#####################################\n");
    wprintf(L"########## Packet Details ###########\n");
    wprintf(L"#####################################\033[0m\n");

    ip_header = (struct iphdr *) packet;
    wprintf(L"\n\033[34m#####################\n");
    wprintf(L"##### IP Header #####\n");
    wprintf(L"#####################\n");
    wprintf(L"Version: %d\n", ip_header->version);
    wprintf(L"IHL: %d\n", ip_header->ihl);
    wprintf(L"Type of Service: %d\n", ip_header->tos);
    wprintf(L"Total Length: %d\n", ip_header->tot_len);
    wprintf(L"Identification: %d\n", ip_header->id);
    wprintf(L"Flags: 0\n");
    wprintf(L"Fragmentation Offset: %d\n", ip_header->frag_off);
    wprintf(L"Time to live: %d\n", ip_header->ttl);
    wprintf(L"Protocol: %d\n", ip_header->protocol);
    wprintf(L"Header Checksum: %d\n", ip_header->check);
    wprintf(L"Source Address: %s\n", inet_ntoa(*((struct in_addr *) &ip_header->saddr)));
    wprintf(L"Destination Address: %s\n\033[0m", inet_ntoa(*((struct in_addr *) &ip_header->daddr)));

    icmp = (struct icmp *) (packet + 20);
    wprintf(L"\n\033[33m#######################\n");
    wprintf(L"##### ICMP Header #####\n");
    wprintf(L"#######################\n");
    wprintf(L"Type: %d\n", icmp->icmp_type);
    wprintf(L"Code: %d\n", icmp->icmp_code);
    wprintf(L"Checksum: %d\n\033[0m", icmp->icmp_cksum);

    wprintf(L"\n\033[92m################\n");
    wprintf(L"##### Data #####\n");
    wprintf(L"################\n");
    wprintf(L"Size: %d\n\033[0m", packet_size - 28);
};

float get_rtt() {
    float time_sec  = recv_time->tv_sec - send_time->tv_sec;
    float time_usec = recv_time->tv_usec - send_time->tv_usec;
    while (time_usec < 0) {
        time_sec -= 1;
        time_usec += 1e6;
    }

    float time_msec = time_sec * 1000 + time_usec / 1000;
    return time_msec;
}

void determine_rtts(int sockfd, int ttl) {
    for (int j = 0; j < MAX_WORDS; j++) {
        for (int k = 0; k < n; k++) {
            int data_size = j * WORD_SIZE;
            char * data = (char *) malloc(data_size * sizeof(char));
            create_icmp_packet(localhost_ip, remotehost_ip, ttl, 0, data, data_size);
            send_icmp_packet(sockfd, remotehost_ip);
            recv_icmp_packet(sockfd);
            rtt[ttl][j][k] = get_rtt(); 
            free(data);
            sleep(T);
        }
    }
}

char * discover_kth_hop(int sockfd, int k) {
    struct probe_info * pi = create_probe_info();

    for (int i = 0; i < MAX_DISCOVERY_PROBES; i++) {
        create_icmp_packet(localhost_ip, remotehost_ip, k, 0, NULL, 0);
        send_icmp_packet(sockfd, remotehost_ip);
        add_to_probe_info(pi, recv_icmp_packet(sockfd));
    } 

    char * finalised_ip = get_finalised_ip(pi);
    free(pi);
    return finalised_ip;
}

float get_row_average(int i, int j) {
    float avg = 0;
    for (int k = 0; k < n; k++) {
        avg += rtt[i][j][k];
    }  avg /= n;
    return avg;
}
 
float get_latency(int i) {
    return fabs(get_row_average(i, 0) - get_row_average(i - 1, 0));
}

float get_bandwidth(int i) {
    // We use packets with payloads WORD_SIZE and 2 * WORD_SIZE to determine bandwidth
    // (L2 - L1) = WORD_SIZE
    float t1 = get_row_average(i - 1, 1);
    float t2 = get_row_average(i - 1, 2);
    float t3 = get_row_average(i, 1);
    float t4 = get_row_average(i, 2);
    printf("%f %f %f %f\n",t1,t2,t3,t4);
    float bandwidth = (WORD_SIZE) / fabs((t4 - t3) - (t2 - t1));
    return bandwidth;
}

int main(int argc, char * argv[]) {

    wchar_t mu = 0x3bc;
    // Display correct usage format in case of incorrect usage
    if (argc < 4) {
        wprintf(L"[Error] Usage: %s <hostname>/<host IP address> <Number of probes per link> <Time difference between two probes>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Extract relevant parameters from the commandline arguments
    memset(remotehost_name, 0, sizeof(remotehost_name));
    strcpy(remotehost_name, argv[1]);
    n = atoi(argv[2]);
    T = atof(argv[3]);

    // Create the raw socket
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) == -1) {
        perror("[Error] socket()");
        exit(EXIT_FAILURE);
    }

    // Allow custom IP header specification for packets being sent over the raw socket
    int enable = 1;
    if (setsockopt(sockfd, IPPROTO_IP, IP_HDRINCL, &enable, sizeof(enable)) == -1) {
        perror("[Error] setsockopt()");
        exit(EXIT_FAILURE);
    }

    // Make recvfrom() call through the raw socket get timed out after 1s
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *) &tv, sizeof(tv)) < 0) {
        perror("[Error] sockopt()");
        exit(EXIT_FAILURE);
    }

    // Set localhost and remotehost ip address in dotted decimal notation and print them
    set_localhost_ip();
    set_remotehost_ip();
    wprintf(L"\nLocalhost  IPv4 Address: %s\n", localhost_ip);
    wprintf(L"Remotehost IPv4 Address: %s\n\n", remotehost_ip);    

    // RTT from a node to itself is always 0
    memset(rtt[0], 0, sizeof(rtt[0])); 

    // Initialise the send_time timestamp and recv_time timestamp
    send_time = (struct timeval *) malloc(sizeof(struct timeval));
    recv_time = (struct timeval *) malloc(sizeof(struct timeval));

    // Keep probing and discovering links and then determining the latency and bandwidth of links until destination is reached
    char intermediate_ips[MAX_HOPS][256];
    for (int i = 0; i < MAX_HOPS; i++) {

        // Set ttl and discover the (i + 1)th link
        int ttl = i + 1;
        strcpy(intermediate_ips[i], discover_kth_hop(sockfd, ttl));
        wprintf(L"\nDiscovered Link [%d]: \033[1;92m%s --- %s\033[0m\n", i + 1, i == 0 ? localhost_ip : intermediate_ips[i - 1], intermediate_ips[i]);

        // Send different amounts of data and record the RTTs
        determine_rtts(sockfd, ttl);

        // Determine the latency and the bandwidth
        float latency   = get_latency(i + 1);
        float bandwidth = get_bandwidth(i + 1);
        wprintf(L"Latency: \033[91;3m%f ms\n\033[0m", latency);
        wprintf(L"Bandwidth: \033[94;3m%f kBps\n\033[0m", bandwidth);

        // Exit if destination is reached
        if (strcmp(intermediate_ips[i], remotehost_ip) == 0) {
            break;
        } 
    }

    // Close the raw socket
    if (close(sockfd) == -1) {
        perror("[Error] close()");
        exit(EXIT_FAILURE);
    }

    return 0;
}
