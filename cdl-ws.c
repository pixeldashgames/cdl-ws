#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <libgen.h>
#include <time.h>
#include "cdl-utils.h"

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

#define DIR_LINK_TEMPLATE "<p>&#x1F4C1 <a href=\"%s\">%s/</a></p>"
#define FILE_LINK_TEMPLATE "<p>&#x1F4C4 <a href=\"%s\">%s</a></p>"

#define HTTP_HTML_HEADER "HTTP/1.1 200 OK\nContent-Type: text/plain\nConnection: close\n\n"
#define HTTP_FILE_HEADER "HTTP/1.1 200 OK\nContent-Type: application/octet-stream\nContent-Disposition: attachment; filename=\"%s\"\nContent-Length: %zu\n\n"

// returns a <a> html link for a given path.
char *ptoa(char *p, bool isdir) {
    char *template = isdir ? DIR_LINK_TEMPLATE : FILE_LINK_TEMPLATE;

    size_t templen = strlen(template);

    char *itemname = basename(p);

    size_t namelen = strlen(itemname);
    size_t plen = strlen(p);

    char *link = malloc((templen + plen + namelen + 1) * sizeof(char));

    sprintf(link, template, p, itemname);

    //free(itemname);
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

// sends a file to a client socket
size_t sendfile_p(char *p, int clientfd) {
    int fd = open(p, O_RDONLY);

    // file info, size etc.
    struct stat st;
    fstat(fd, &st);

    char *fname = basename(p);

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

//    bool name_test0 = strcmp(sbasename("/home/user/mydir/"), "mydir") == 0;
//    printf("basename Dir Test: %s\n", name_test0 ? "✅" : "❌");
//
//    bool name_test1 = strcmp(sbasename("/home/user/myfile"), "myfile") == 0;
//    printf("basename File Test: %s\n", name_test1 ? "✅" : "❌");

    bool cmtor_test0 = strcmp(cmtorp("GET /home/user/my dir HTTP/1.1\nRandom Browser Data\nConnection Request"),
                              "/home/user/my dir") == 0;
    printf("cmtorp Test 0: %s\n", cmtor_test0 ? "✅" : "❌");

    bool cmtor_test1 = strcmp(cmtorp("GET /home/my user/my dir/my    spaced    dir HTTP/1.1"),
                              "/home/my user/my dir/my    spaced    dir") == 0;
    printf("cmtorp Test 1: %s\n", cmtor_test1 ? "✅" : "❌");

    int correctCount = ptoa_test0 + ptoa_test1 + /*name_test0 + name_test1 +*/ cmtor_test0 + cmtor_test1;
    int total = 6;
    bool allCorrect = correctCount == total;
    printf("Testing Finished. Results %d/%d %s", correctCount, total, allCorrect ? "✅" : "❌");

    return allCorrect ? 0 : 1;
}

int count_entries(char *path) {
    DIR *dir;
    int count = 0;
    if ((dir = opendir(path)) != NULL) {
        while (readdir(dir) != NULL) count++;
        closedir(dir);
    } else {
        perror("Unable to open directory");
        return -1;
    }
    return count;
}

char *create_tr(char *path) {
    struct stat file_stat;
    if (stat(path, &file_stat) != 0) {
        perror("Error getting stats");
        return NULL;
    }

    size_t length = 2048;

    char *name = ptoa(path, S_ISDIR(file_stat.st_mode));
//    length += strlen(name) + 9;
    printf("NAME: %s\n", name);

    size_t size = S_ISDIR(file_stat.st_mode) ? 0 : file_stat.st_size;
    char c_size[24];
    sprintf(c_size, "%zu", size);
//    length += strlen(c_size);
    printf("SIZE: %ld\n", size);

    char *date = ctime(&file_stat.st_mtime);
    date[strlen(date) - 1] = '\0';
//    length += strlen(date) + 9;
    printf("DATE: %s\n", date);

    char *row = malloc((length + 1) * sizeof(char));

    sprintf(row, "<tr>%s</tr><tr>%s</tr><tr>%s</tr>", name, c_size, date);
    printf("ROW: %s\n", row);
    return row;
}

// TODO: Allow to create a table with multiples properties
char *build_page(char *path) {
    struct dirent *dir_entry;
    int entries_count = count_entries(path);
    int i;
    // TODO: make a page for wrong paths

    size_t page_len = strlen(HTTP_HTML_HEADER);
    size_t table_len = 0;
    char **entries = malloc(entries_count * sizeof(char *));
    for (i = 0; i < entries_count; ++i) {
        entries[i] = NULL;
    }

    i = 0;
    DIR *dir = opendir(path);

    if (dir != NULL) {
        printf("open dir OK\n");
        while ((dir_entry = readdir(dir)) != NULL) {
            if (strcmp(dir_entry->d_name, ".") == 0 || strcmp(dir_entry->d_name, "..") == 0) {
                continue; // skip current and parent directories
            }
            printf("read dir OK\n");
            size_t fp_len = strlen(path) + strlen(dir_entry->d_name) + 2;
            printf("fp_len: %ld\n", fp_len);
            char full_path[fp_len];
            sprintf(full_path, "%s/%s", path, dir_entry->d_name);
            entries[i] = create_tr(full_path);
            printf("Entry %d :%s\n", i, entries[i]);
            size_t row_len = strlen(entries[i]);
            table_len += row_len;

            i++;
        }
    }

    page_len += table_len;
    printf("table_len: %ld\n", table_len);
    printf("f_row %s\n", entries[0]);
    //Create table
    char *table = calloc(table_len + 1, sizeof(char));

    char *en = NULL;

    for (i = 0; i < entries_count; i++) {
        en = entries[i];
        if (en == NULL) {
            printf("Entry %d is NULL\n", i);
            continue;
        }

        printf("entry size: %ld\n", strlen(en));
        strcat(table, en);
        free(en);
    }

    free(entries);

    // Create html
    char *html = calloc(table_len + 1, sizeof(char));

    strcpy(html, table);
    free(table);

    //Create page
    char *page = calloc(page_len + 1, sizeof(char));
    sprintf(page, "%s%s", HTTP_HTML_HEADER, html);
    free(html);

    return page;
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

    if (access(root, F_OK) == -1) {
        printf("%s is not a valid path.\n", root);
    }
    //if (chdir(root) != 0) {
    //    perror("Couldn't create server on the specified path.\n");
    //    exit(1);
    //}

    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    int max_clients = MAX_CLIENTS;

    // max_sd : Max socket descriptor, ceiling for select operation
    // sd : iterator variable (foreach sd in sockets basically)
    int max_sd, i, sd;

    // buffer : Input buffer.
    char buffer[BUFFER_SIZE] = "";

    // set of file descriptors for client read 'streams'
    fd_set readfds;
    int clients[MAX_CLIENTS] = {};
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
                    bool sum = true;
                    if (strcmp(path, "/") == 0)
                        sum = false;
                    char *full_path = malloc((strlen(root) + ((sum) ? strlen(path) : 0) + 1) * sizeof(char));
                    strcpy(full_path, root);
                    if (sum)
                        strcat(full_path, path);
                    char *page = build_page(full_path);
                    free(full_path);
                    write(sd, page, strlen(page));
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
                    free(path);
                }
            }
        }
    }

    return 0;
}