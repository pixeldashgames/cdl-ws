#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    int max_clients = MAX_CLIENTS;
    int max_sd, activity, i, sd;
    char buffer[BUFFER_SIZE];
    fd_set readfds;
    int clients[MAX_CLIENTS];
    for (i = 0; i < MAX_CLIENTS; i++) clients[i] = 0;

    // create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(1);
    }

    // set server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(1234);

    // bind socket to address
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }

    // listen for incoming connections
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        exit(1);
    }
    
    printf("Waiting for connections...\n");

    while (1) {
        // clear file descriptor set
        FD_ZERO(&readfds);

        // add server socket to set
        FD_SET(server_fd, &readfds);
        max_sd = server_fd;

        // add child sockets to set
        for (i = 0; i < max_clients; i++) {
            sd = clients[i];

            // add valid socket to set
            if (sd > 0) {
                FD_SET(sd, &readfds);
            }

            // update max socket descriptor
            if (sd > max_sd) {
                max_sd = sd;
            }
        }

        // wait for activity on sockets
        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if (activity < 0) {
            perror("select");
            exit(1);
        }

        // handle incoming connection request
        if (FD_ISSET(server_fd, &readfds)) {
            socklen_t client_len = sizeof(client_addr);
            if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len)) < 0) {
                perror("accept");
                exit(1);
            }

            // add new client to list of clients
            for (i = 0; i < max_clients; i++) {
                if (clients[i] == 0) {
                    clients[i] = client_fd;
                    printf("New connection from %s:%d, socket fd: %d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), client_fd);
                    break;
                }
            }
        }

        // handle incoming data from client
        for (i = 0; i < max_clients; i++) {
            sd = clients[i];

            if (FD_ISSET(sd, &readfds)) {
                if ((read(sd, buffer, BUFFER_SIZE)) == 0) {
                    // client disconnected
                    printf("Client disconnected, socket fd: %d\n", sd);
                    close(sd);
                    clients[i] = 0;
                } else {
                    // send message to all other clients
                    for (int j = 0; j < max_clients; j++) {
                        if (i != j && clients[j] > 0) {
                            if (write(clients[j], buffer, strlen(buffer)) < 0) {
                                perror("write");
                                exit(1);
                            }
                        }
                    }
                }
            }
        }
    }

    return 0;
}