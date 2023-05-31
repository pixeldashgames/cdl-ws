#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>

#define PORT 8080
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

int main()
{
    int server_fd, new_socket, valread;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};
    char *response = NULL;
    int *fds = malloc((MAX_CLIENTS + 1) * sizeof(int));
    bzero(fds, (MAX_CLIENTS + 1));
    bool *used = malloc((MAX_CLIENTS + 1) * sizeof(bool));
    bzero(used, (MAX_CLIENTS + 1));
    int nfds = 0;

    // Create server socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options to allow reuse of address and port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    // Set server address properties
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind server socket to address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, MAX_CLIENTS) < 0)
    {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    // Add server socket to pollfd array
    fds[0] = server_fd;
    used[0] = true;
    nfds++;

    while (1)
    {
        struct pollfd *pfds = malloc(nfds * sizeof(struct pollfd));
        // Add server file descriptor
        pfds[0].fd = fds[0];
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;

        // Add other file descriptors
        int added = 1;
        for (int i = 1; i <= MAX_CLIENTS; i++)
        {
            if (used[i])
            {
                pfds[added].fd = fds[i];
                pfds[added].events = POLLIN | POLLOUT;
                pfds[added].revents = 0;
                added++;
            }
        }

        // Wait for events on any of the file descriptors
        if (poll(pfds, nfds, -1) < 0)
        {
            perror("poll failed");
            exit(EXIT_FAILURE);
        }

        // Check for events on server socket
        if (pfds[0].revents & POLLIN)
        {
            // Accept new connection
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
            {
                perror("accept failed");
                exit(EXIT_FAILURE);
            }

            // Add new client socket to pollfd array
            if (nfds < MAX_CLIENTS + 1)
            {
                for (int i = 1; i <= MAX_CLIENTS; i++)
                {
                    if (!used[i])
                    {
                        fds[i] = new_socket;
                        used[i] = true;
                        nfds++;
                        break;
                    }
                }
            }
        }

        // Check for events on active client sockets
        for (int i = 1; i < added; i++)
        {
            int fdsi = -1;
            // Find fds array index
            for (int j = 1; j <= MAX_CLIENTS; j++)
            {
                if (used[j] && fds[j] == pfds[i].fd)
                {
                    fdsi = j;
                    break;
                }
            }
            if (fdsi < 0)
            {
                continue;
            }

            if ((pfds[i].revents & POLLIN) && (pfds[i].revents & POLLOUT))
            {
                bzero(buffer, BUFFER_SIZE);
                // Read client request
                if (read(pfds[i].fd, buffer, BUFFER_SIZE) > 0)
                {
                    // Ignore client request
                    printf("New request\n\n\"%s\"\n\n", buffer);
                }
                // Send response
                response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
                send(new_socket, response, strlen(response), 0);
                int indexFd = open("index.html", O_RDONLY);
                bzero(buffer, BUFFER_SIZE);
                while ((valread = read(indexFd, buffer, BUFFER_SIZE)) > 0)
                {
                    send(pfds[i].fd, buffer, valread, 0);
                }
                close(indexFd);
                close(pfds[i].fd);
                used[fdsi] = false;
                nfds--;
            }
        }
        free(pfds);
    }
    return 0;
}