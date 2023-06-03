#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

int main(int argc, char *argv[])
{
    int port = 31431;

    char *response_http = "HTTP/1.1 200 OK\nContent-Type: text/html\nConnection: Closed\n\n";
    char *response_html = "<html><body><h1><a href=\"asd\">Hello World</h1></body></html>";

    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    int max_clients = MAX_CLIENTS;

    // max_sd : Max socket descriptor, ceiling for select operation
    // sd : iterator variable (foreach sd in sockets basically)
    int max_sd, i, sd;

    // buffer : Input buffer.
    char buffer[BUFFER_SIZE];
    // set of file descriptors for client read 'streams'
    fd_set readfds;
    int clients[MAX_CLIENTS];
    for (i = 0; i < MAX_CLIENTS; i++)
        clients[i] = 0;

    // create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("Couldn't create server socket on the specified port.");
        exit(1);
    }

    printf("Server socket created...");

    // set server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // bind socket to address
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Couldn't bind server address to socket.");
        exit(1);
    }

    printf("Server socket bound to port...");

    // listen for incoming connections
    if (listen(server_fd, MAX_CLIENTS) < 0)
    {
        perror("Error trying to listen to incoming connections.");
        exit(1);
    }

    printf("Waiting for connections...\n");

    while (1)
    {
        // clear file descriptor set
        FD_ZERO(&readfds);

        // add server socket to set
        FD_SET(server_fd, &readfds);
        max_sd = server_fd;

        // add child sockets to set
        for (i = 0; i < max_clients; i++)
        {
            sd = clients[i];

            // add valid socket to set
            if (sd > 0)
            {
                FD_SET(sd, &readfds);
            }

            // update max socket descriptor
            if (sd > max_sd)
            {
                max_sd = sd;
            }
        }

        // wait for activity on sockets
        if (select(max_sd + 1, &readfds, NULL, NULL, NULL) < 0)
        {
            perror("Error trying to wait for activity from clients.");
            exit(1);
        }

        // handle incoming connection request
        // FD_ISSET checks whether there is a connection request on the server
        if (FD_ISSET(server_fd, &readfds))
        {
            socklen_t client_len = sizeof(client_addr);
            if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len)) < 0)
            {
                perror("Error accepting connection request from client.");
                exit(1);
            }

            // add new client to list of clients
            for (i = 0; i < max_clients; i++)
            {
                if (clients[i] == 0)
                {
                    clients[i] = client_fd;
                    printf("New connection from %s:%d, socket fd: %d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), client_fd);
                    break;
                }
            }
        }

        // handle incoming data from client
        for (i = 0; i < max_clients; i++)
        {
            sd = clients[i];

            // Check whether the client is sending data.
            if (FD_ISSET(sd, &readfds))
            {
                if ((read(sd, buffer, BUFFER_SIZE)) == 0)
                {
                    // client disconnected
                    printf("Client disconnected, socket fd: %d\n", sd);
                    close(sd);
                    clients[i] = 0;
                }
                else
                {
                    // HANDLING INCOMING DATA
                    printf("%s\n", buffer);
                    write(sd, response_http, strlen(response_http));
                    write(sd, response_html, strlen(response_html));
                }
            }
        }
    }

    return 0;
}