#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/select.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

#define DIR_LINK_TEMPLATE "<p>&#x1F4C1 <a href=\"%s\">%s/</a></p>"
#define FILE_LINK_TEMPLATE "<p>&#x1F4C4 <a href=\"%s\">%s</a></p>"

char *get_file_name(char *p) {
    int n = strlen(p);
    char *name = malloc((n + 1) * sizeof(char));

    if (p[n - 1] == '/')
        p[--n] = '\0';
    for (int i = n - 1; i >= 0; i--) {
        if (p[i] != '/')
            continue;

        strcpy(name, p + i + 1);
        break;
    }

    return name;
}

// void list_files(const char *path)
// {
//     struct dirent *entry;
//     DIR *dir = opendir(path);
//
//     if (dir == NULL)
//     {
//         return;
//     }
//
//     while ((entry = readdir(dir)) != NULL)
//     {
//         if (entry->d_type == DT_DIR)
//         {
//             // Found a directory, but ignore "." and ".." entries
//             if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
//             {
//                 continue;
//             }
//             printf("Directory: %s/%s\n", path, entry->d_name);
//             char subpath[1024];
//             snprintf(subpath, sizeof(subpath), "%s/%s", path, entry->d_name);
//             list_files(subpath);
//         }
//         else
//         {
//             // Found a file
//             printf("File: %s/%s\n", path, entry->d_name);
//         }
//     }
//     closedir(dir);
// }

// returns a <a> html link for a given path.
char *ptoa(char *p, bool isdir) {
    char *template = isdir ? DIR_LINK_TEMPLATE : FILE_LINK_TEMPLATE;

    int templen = strlen(template);

    char *itemname = get_file_name(p);

    int namelen = strlen(itemname);
    int plen = strlen(p);

    char *link = malloc((templen + plen + namelen + 1) * sizeof(char));

    sprintf(link, template, p, itemname);

    free(itemname);
    return link;
}

// returns the index of the first time tok is matched in str, left to right.
int findstr(char *str, char *tok) {
    size_t len = strlen(str);
    size_t toklen = strlen(tok);
    size_t q = 0; // matched chars so far

    for (int i = 0; i < len; i++) {
        if (q > 0 && str[i] != tok[q])
            q = 0; // this works since in the use cases for this function tokens are space separated and
        // don't start with a space, so there is no need for a prefix function.
        if (str[i] == tok[q])
            q++;
        if (q == toklen)
            return i - q + 1;
    }

    return -1;
}

char *cmtor(char *message) {
    int start_offset = 4;
    int end = findstr(message, "HTTP") - 1;
    int path_len = end - start_offset;
    // printf("Path_len: %i\n", path_len);
    char *path = malloc((path_len + 1) * sizeof(char));
    strncpy(path, message + start_offset, path_len);
    return path;
}

// To run the server : gcc server.c -o server
//                     ./server 31431 /rootpath
int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("usage: %s <server_port> <root_directory>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);

    char *root = argv[2];

    if (chdir(root) != 0) {
        perror("Couldn't create server on the specified path.\n");
        exit(1);
    }

    char *response_http = "HTTP/1.1 200 OK\nContent-Type: text/html\nConnection: Closed\n\n";
    // char *response_html = "<html><head><h1>Root</h1></head><body><h1><a href=\"asd\">Hello World</h1></body></html>";

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
    if (server_fd < 0) {
        perror("Couldn't create server socket on the specified port.\n");
        exit(1);
    }

    printf("Server socket created...\n");

    // set server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // bind socket to address
    if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("Couldn't bind server address to socket.\n");
        exit(1);
    }

    printf("Server socket bound to port...\n");

    // listen for incoming connections
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Error trying to listen to incoming connections.\n");
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
        if (select(max_sd + 1, &readfds, NULL, NULL, NULL) < 0) {
            perror("Error trying to wait for activity from clients.\n");
            exit(1);
        }

        // handle incoming connection request
        // FD_ISSET checks whether there is a connection request on the server
        if (FD_ISSET(server_fd, &readfds)) {
            socklen_t client_len = sizeof(client_addr);
            if ((client_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_len)) < 0) {
                perror("Error accepting connection request from client.\n");
                exit(1);
            }

            // add new client to list of clients
            for (i = 0; i < max_clients; i++) {
                if (clients[i] == 0) {
                    clients[i] = client_fd;
                    printf("New connection from %s:%d, socket fd: %d\n", inet_ntoa(client_addr.sin_addr),
                           ntohs(client_addr.sin_port), client_fd);
                    break;
                }
            }
        }

        // handle incoming data from client
        for (i = 0; i < max_clients; i++) {
            sd = clients[i];

            // Check whether the client is sending data.
            if (FD_ISSET(sd, &readfds)) {
                if ((read(sd, buffer, BUFFER_SIZE)) == 0) {
                    // client disconnected
                    printf("Client disconnected, socket fd: %d\n", sd);
                    close(sd);
                    clients[i] = 0;
                } else {
                    // HANDLING INCOMING DATA
                    printf("Start buffer-------------\n");
                    printf("%s\n", buffer);
                    printf("End buffer --------------\n");
                    printf("Start test-------------\n");

                    
                    // char *path = cmtor(buffer);
                    // printf("Path: %s\n", path);
                    printf("End test-------------\n");
                    write(sd, response_http, strlen(response_http));

                    char *html = ptoa(root, true);
                    write(sd, html, strlen(html));

                    free(html);
                }
            }
        }
    }

    return 0;
}