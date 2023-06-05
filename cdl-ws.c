#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>
#include "cdl-utils.h"

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

#define DIR_LINK_TEMPLATE "<p>&#x1F4C1 <a href=\"%s\">%s/</a></p>"
#define FILE_LINK_TEMPLATE "<p>&#x1F4C4 <a href=\"%s\">%s</a></p>"

#define HTTP_HTML_HEADER "HTTP/1.1 200 OK\nContent-Type: text/plain\nConnection: close\n\n"
#define HTTP_FILE_HEADER "HTTP/1.1 200 OK\nContent-Type: application/octet-stream\nContent-Disposition: attachment; filename=\"%s\"\nContent-Length: %zu\n\n"

// returns the directory or file name indicated by a path, i.e. "myitem" in /home/user/myitem
char *sbasename(char *p) {
    uint n = strlen(p);

    char *pcpy = malloc((n + 1) * sizeof(char));
    strcpy(pcpy, p);

    char *name = malloc((n + 1) * sizeof(char));
    name[0] = '\0';

    if (pcpy[n - 1] == '/') {
        pcpy[--n] = '\0';
    }
    for (int i = (int) n - 1; i >= 0; i--) {
        if (pcpy[i] != '/')
            continue;

        strcpy(name, pcpy + i + 1);
        break;
    }

    if (strcmp(name, "") == 0)
        strcpy(name, pcpy);

    free(pcpy);

    return name;
}

// returns a <a> html link for a given path.
char *ptoa(char *p, bool isdir) {
    char *template = isdir ? DIR_LINK_TEMPLATE : FILE_LINK_TEMPLATE;

    size_t templen = strlen(template);

    char *itemname = sbasename(p);

    size_t namelen = strlen(itemname);
    size_t plen = strlen(p);

    char *link = malloc((templen + plen + namelen + 1) * sizeof(char));

    sprintf(link, template, p, itemname);

    free(itemname);
    return link;
}

// returns a request path extracted from a given client message
char *cmtorp(char *message) {
    int end = findc(message, '\n');
    if (end == -1)
        end = (int) strlen(message);

    struct JaggedCharArray req = splitnstr(message, ' ', end, true);
    req.arr += 1;
    req.count -= 1;
    return joinarr(req, ' ', req.count - 1);
}

// sends a file     to a client socket
size_t sendfile_p(char *p, int clientfd) {
    int fd = open(p, O_RDONLY);

    // file info, size etc.
    struct stat st;
    fstat(fd, &st);

    char *fname = sbasename(p);

    // http header for the file
    char *header = malloc((strlen(HTTP_FILE_HEADER) + strlen(fname) + 24) * sizeof(char));
    sprintf(header, HTTP_FILE_HEADER, fname, st.st_size);

    write(clientfd, header, strlen(header));

    // how much of the file has been sent
    off_t offset = 0;

    size_t rc = sendfile(clientfd, fd, &offset, (size_t) st.st_size);

    close(fd);

    return rc;
}

int run_tests(__attribute__((unused)) int argc, __attribute__((unused)) char *argv[]) {
    bool ptoa_test0 =
            strcmp(ptoa("/home/user/mydir", true), "<p>&#x1F4C1 <a href=\"/home/user/mydir\">mydir/</a></p>") == 0;
    printf("ptoa Dir Test: %s\n", ptoa_test0 ? "✅" : "❌");

    bool ptoa_test1 =
            strcmp(ptoa("/home/user/myfile", false), "<p>&#x1F4C4 <a href=\"/home/user/myfile\">myfile</a></p>") == 0;
    printf("ptoa File Test: %s\n", ptoa_test1 ? "✅" : "❌");

    bool name_test0 = strcmp(sbasename("/home/user/mydir/"), "mydir") == 0;
    printf("sbasename Dir Test: %s\n", name_test0 ? "✅" : "❌");

    bool name_test1 = strcmp(sbasename("/home/user/myfile"), "myfile") == 0;
    printf("sbasename File Test: %s\n", name_test1 ? "✅" : "❌");

    bool cmtor_test0 = strcmp(cmtorp("GET /home/user/my dir HTTP/1.1\nRandom Browser Data\nConnection Request"),
                              "/home/user/my dir") == 0;
    printf("cmtorp Test 0: %s\n", cmtor_test0 ? "✅" : "❌");

    bool cmtor_test1 = strcmp(cmtorp("GET /home/my user/my dir/my    spaced    dir HTTP/1.1"),
                              "/home/my user/my dir/my    spaced    dir") == 0;
    printf("cmtorp Test 1: %s\n", cmtor_test1 ? "✅" : "❌");

    int correctCount = ptoa_test0 + ptoa_test1 + name_test0 + name_test1 + cmtor_test0 + cmtor_test1;
    int total = 6;
    bool allCorrect = correctCount == total;
    printf("Testing Finished. Results %d/%d %s", correctCount, total, allCorrect ? "✅" : "❌");

    return allCorrect ? 0 : 1;
}

// To run the server : gcc server.c -o server
//                     ./server 31431 /rootpath
int main(int argc, char *argv[]) {

#pragma region Server Initialization
    if (strcmp(argv[argc - 1], "test") == 0) {
        return run_tests(argc, argv);
    }

    if (argc < 3) {
        printf("usage: %s <server_port> <root_directory>\n", argv[0]);
        exit(1);
    }

    int port = (int) strtol(argv[1], NULL, 10);

    char *root = argv[2];

    if (chdir(root) != 0) {
        perror("Couldn't create server on the specified path.\n");
        exit(1);
    }

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

#pragma endregion

    while (1) {

#pragma region File Descriptor Set Initialization
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
#pragma endregion

        // wait for activity on sockets
        if (select(max_sd + 1, &readfds, NULL, NULL, NULL) < 0) {
            perror("Error trying to wait for activity from clients.\n");
            exit(1);
        }

#pragma region Connection Requests
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
#pragma endregion

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

                    char *path = cmtorp(buffer);
                    printf("Path: %s\n", path);
                    printf("End test-------------\n");

                    if (strcmp(path, "/coco.jpeg") == 0) {
                        size_t sent = sendfile_p("coco.jpeg", sd);
                        printf("Sent %zu bytes\n", sent);
                    } else {
                        write(sd, HTTP_HTML_HEADER, strlen(HTTP_HTML_HEADER));
                        char *html = ptoa("coco.jpeg", false);
                        write(sd, html, strlen(html));
                        free(html);
                    }
                }
            }
        }
    }

    return 0;
}