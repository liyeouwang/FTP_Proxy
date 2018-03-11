/**
 * Simple FTP Proxy For Introduction to Computer Network.
 * Author: z58085111 @ HSNL
 * **/
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <arpa/inet.h>

#include <sys/time.h>	// High precition clocl

#define MAXSIZE 2048
#define FTP_PORT 8740
#define FTP_PASV_CODE 227
#define FTP_ADDR "140.114.71.159"
#define max(X,Y) ((X) > (Y) ? (X) : (Y))

int proxy_IP[4];
double rate;

int connect_FTP(int ser_port, int clifd, int is_forked);
int proxy_func(int ser_port, int clifd, int is_forked);
int create_server(int port);
void rate_control(struct timeval start_time, struct timeval current_time, unsigned long total_send_bytes);
void print_is_forked(int is_forked);

int main (int argc, char **argv) {
    int ctrlfd, connfd, port, send_bytes = 0;
	int first_send = 1;
    pid_t childpid;
    socklen_t clilen;
    struct sockaddr_in cliaddr;
    if (argc < 4) {
        printf("\n[v] Usage: ./executableFile <ProxyIP> <ProxyPort> <rate> \n");
        return -1;
    }

    sscanf(argv[1], " %d.%d.%d.%d", &proxy_IP[0], &proxy_IP[1], &proxy_IP[2], &proxy_IP[3]);
    port = atoi(argv[2]);
	rate = atoi(argv[3]);

	printf("Create server with rate %d KB/s\n", (int)rate);
	fflush(stdout);

    ctrlfd = create_server(port);
    clilen = sizeof(struct sockaddr_in);
    for (;;) {
		printf("\n...Waiting for connection...\n");
		fflush(stdout);
        connfd = accept(ctrlfd, (struct sockaddr *)&cliaddr, &clilen);
        if (connfd < 0) {
            printf("\n[x] Accept failed\n");
            return 0;
        }

        printf("\n[v] Client: %s:%d connect!\n", inet_ntoa(cliaddr.sin_addr), htons(cliaddr.sin_port));
        if ((childpid = fork()) == 0) {
            close(ctrlfd);
            proxy_func(FTP_PORT, connfd, 0);
            printf("\n[v] Client: %s:%d terminated!\n", inet_ntoa(cliaddr.sin_addr), htons(cliaddr.sin_port));
            exit(0);
        }

        close(connfd);
    }
    return 0;
}

int connect_FTP(int ser_port, int clifd, int is_forked) {
    int sockfd;
    char addr[] = FTP_ADDR;
    int byte_num;
    char buffer[MAXSIZE];
    struct sockaddr_in servaddr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		print_is_forked(is_forked);
        printf("[x] Create socket error");
        return -1;
    }

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(ser_port);

    if (inet_pton(AF_INET, addr, &servaddr.sin_addr) <= 0) {
		print_is_forked(is_forked);
        printf("[v] Inet_pton error for %s", addr);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
		print_is_forked(is_forked);
        printf("[x] Connect error");
        return -1;
    }

	print_is_forked(is_forked);
    printf("[v] Connect to FTP server\n");
    if (ser_port == FTP_PORT) {
        if ((byte_num = read(sockfd, buffer, MAXSIZE)) <= 0) {
			print_is_forked(is_forked);
            printf("[x] Connection establish failed.\n");
        }

        if (write(clifd, buffer, byte_num) < 0) {
			print_is_forked(is_forked);
            printf("[x] Write to client failed.\n");
            return -1;
        }
    }

    return sockfd;
}

int proxy_func(int ser_port, int clifd, int is_forked){
    char buffer[MAXSIZE];
    int serfd = -1, datafd = -1, connfd;
    int data_port;
    int byte_num = 0;
    int status, pasv[7];
    int childpid;
    socklen_t clilen;
    struct sockaddr_in cliaddr;
	struct timeval start_time, current_time;	// CAN'T be initiate as global variable!
	int first_transmit = 1;

	unsigned long total_upload_bytes = 0;	// CAN'T be initiate as global variable!
	unsigned long total_download_bytes = 0;

    // select vars
    int maxfdp1;
    int i, nready = 0;
    fd_set rset, allset;

    // connect to FTP server
    if ((serfd = connect_FTP(ser_port, clifd, is_forked)) < 0) {
		print_is_forked(is_forked);
        printf("[x] Connect to FTP server failed.\n");
        return -1;
    }

    // datafd = serfd;

    // initialize select vars
    FD_ZERO(&allset);
    FD_SET(clifd, &allset);
    FD_SET(serfd, &allset);

    // selecting
    for (;;) {
        // reset select vars
        rset = allset;
        maxfdp1 = max(clifd, serfd) + 1;

        // select descriptor
        nready = select(maxfdp1, &rset, NULL, NULL, NULL);
        if (nready > 0) {
			if(first_transmit){
				// 1st time transfering bytes
				gettimeofday(&start_time, NULL);
				first_transmit = 0;
			}
            // check FTP client socket fd
            if (FD_ISSET(clifd, &rset)) {
				// Client -> Server
                memset(buffer, 0, MAXSIZE);
                if ((byte_num = read(clifd, buffer, MAXSIZE)) <= 0) {
					print_is_forked(is_forked);
                    printf("[!] Client terminated the connection.\n");
                    break;
                }


                if (write(serfd, buffer, byte_num) < 0) {
                    printf("\n[x] Write to server failed.\n");
                    break;
                }

				gettimeofday(&current_time, NULL);
				total_upload_bytes += byte_num;
				rate_control(start_time, current_time, total_upload_bytes);
            }

            // check FTP server socket fd
            if (FD_ISSET(serfd, &rset)) {
				// Client <- Server
                memset(buffer, 0, MAXSIZE);
                if ((byte_num = read(serfd, buffer, MAXSIZE)) <= 0) {
					print_is_forked(is_forked);
                    printf("[!] Server terminated the connection.\n");
                    break;
                }
                if(ser_port == FTP_PORT)
                  buffer[byte_num] = '\0';

                status = atoi(buffer);

                if (status == FTP_PASV_CODE && ser_port == FTP_PORT) {
					// Enter passive mode, use FORK!
					print_is_forked(is_forked);
                    printf("[!] Entering Passive Mode\n");

                    sscanf(buffer, "%d Entering Passive Mode (%d,%d,%d,%d,%d,%d)",&pasv[0],&pasv[1],&pasv[2],&pasv[3],&pasv[4],&pasv[5],&pasv[6]);
                    memset(buffer, 0, MAXSIZE);
                    sprintf(buffer, "%d Entering Passive Mode (%d,%d,%d,%d,%d,%d)\n", status, proxy_IP[0], proxy_IP[1], proxy_IP[2], proxy_IP[3], pasv[5], pasv[6]);

                    if ((childpid = fork()) == 0) {
                        data_port = pasv[5] * 256 + pasv[6];
                        datafd = create_server(data_port);
						printf("\nForked:");
                        printf("[-] Waiting for data connection!\n");
                        clilen = sizeof(struct sockaddr_in);
                        connfd = accept(datafd, (struct sockaddr *)&cliaddr, &clilen);
                        if (connfd < 0) {
                            printf("\n[x] Accept failed\n");
                            return 0;
                        }

						printf("\nForked:");
                        printf("[v] Data connection from: %s:%d connect.\n", inet_ntoa(cliaddr.sin_addr), htons(cliaddr.sin_port));
                        proxy_func(data_port, connfd, 1);
						printf("\nForked:");
                        printf("[!] End of data connection!\n");
                        exit(0);
                    }
                }


                if (write(clifd, buffer, byte_num) < 0) {
                    printf("[x] Write to client failed.\n");
                    break;
                }

				gettimeofday(&current_time, NULL);
				total_download_bytes += byte_num;
				rate_control(start_time, current_time, total_download_bytes);
            }
        } else {
            printf("\n[x] Select() returns -1. ERROR!\n");
            return -1;
        }
    }
    return 0;
}

int create_server(int port) {
    int listenfd;
    struct sockaddr_in servaddr;

	int reuseaddr = 1;
    listenfd = socket(AF_INET, SOCK_STREAM, 0);

	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int));

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *)&servaddr , sizeof(servaddr)) < 0) {
        //print the error message
        perror("bind failed. Error");
        return -1;
    }

    listen(listenfd, 3);
    return listenfd;
}

void rate_control(struct timeval start_time, struct timeval current_time, unsigned long total_send_bytes) {
    /**
     * Implement your main logic of rate control here.
     * Add return variable or parameters you need.
     * **/

	// Real elapsed time from the beginning in us
	double elapsed_time = (current_time.tv_usec - start_time.tv_usec) + (current_time.tv_sec - start_time.tv_sec) * 1000000.0;
	// Ideal elapsed time from the beginning in us
	double ideal_elapsed_time = (double)total_send_bytes / ((double)rate * 1024.0 / 1000000);
	
	if(ideal_elapsed_time - elapsed_time > 0)
		usleep(ideal_elapsed_time - elapsed_time);
		
}

void print_is_forked(int is_forked){
	if(is_forked)
		printf("\nForked:");
	else
		printf("\n______:");
}
