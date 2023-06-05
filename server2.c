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
#include <libgen.h>
#include <time.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

#define DIR_LINK_TEMPLATE "<p>&#x1F4C1 <a href=\"%s\">%s/</a></p>"
#define FILE_LINK_TEMPLATE "<p>&#x1F4C4 <a href=\"%s\">%s</a></p>"

// returns a <a> html link for a given path.
char *ptoa(char *p, bool isdir) {
    char *template = isdir ? DIR_LINK_TEMPLATE : FILE_LINK_TEMPLATE;

    int templen = strlen(template);

    char *itemname = basename(p);

    int namelen = strlen(itemname);
    int plen = strlen(p);

    char *link = malloc((templen + plen + namelen + 1) * sizeof(char));

    sprintf(link, template, p, itemname);

    //free(itemname);
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

const char *response_http = "HTTP/1.1 200 OK\nContent-Type: text/html\nConnection: Closed\n\n";
const char *response_html = "<html><head><h1>Root</h1></head><body><table><tr><th>Name</th><th>Size</th><th>Date</th></tr>%s</table></body></html>";

int count_entries(char *path) {
    DIR *dir;
    struct dirent *dir_entry;
    int count = 0;
    if ((dir = opendir(path)) != NULL) {
        while ((dir_entry = readdir(dir)) != NULL) count++;
        closedir(dir);
    } else {
        perror("Unable to open directory");
        return -1;
    }
    return count;
}

char *create_tr(char *path) {
    DIR *dir;
    struct dirent *dir_entry;
    struct stat file_stat;
    if (stat(path, &file_stat) != 0) {
        perror("Error getting stats");
        return NULL;
    }

    size_t length = 0;

    char *name = ptoa(path, S_ISDIR(file_stat.st_mode));
    length += strlen(name) + 9;
    printf("NAME: %s\n", name);

    long size = S_ISDIR(file_stat.st_mode) ? 0 : file_stat.st_size;
    char c_size[sizeof(size_t)];
    strcpy(c_size, "%ld");
    sprintf(c_size, "%ld", size);
    length += strlen(c_size);
    printf("SIZE: %ld\n", size);

    char *date = ctime(&file_stat.st_mtime);
    date[strlen(date) - 1] = '\0';
    length += strlen(date) + 9;
    printf("DATE: %s\n", date);


    char *row = malloc((length + 1) * sizeof(char));
    row[0] = '\0';

    sprintf(row, "<tr>%s</tr><tr>%s</tr><tr>%s</tr>", name, c_size, date);
    printf("ROW: %s\n", row);
    return row;
}

// TODO: Allow to create a table with multiples properties
char *build_page(char *path) {
    DIR *dir;
    struct dirent *dir_entry;
    int entries_count = count_entries(path);
    int i = 0;
    // TODO: make a page for wrong paths

    size_t page_len = strlen(response_http) + strlen(response_html);
    size_t table_len = 0;
    char **entries = malloc(entries_count * sizeof(char *));

    if ((dir = opendir(path)) != NULL) {
        printf("open dir OK\n");
        while ((dir_entry = readdir(dir)) != NULL) {
            if (strcmp(dir_entry->d_name, ".") == 0 || strcmp(dir_entry->d_name, "..") == 0) {
                continue; // skip current and parent directories
            }
            printf("read dir OK\n");
            size_t fp_len = strlen(path) + strlen(dir_entry->d_name) + 2;
            printf("fp_len: %ld\n", fp_len);
            char *full_path = malloc(fp_len * sizeof(char));
            full_path[0] = '\0';
            sprintf(full_path, "%s/%s", path, dir_entry->d_name);
            char *row = create_tr(full_path);
            size_t row_len = strlen(row);
            table_len += row_len;
            entries[i++] = row;
            free(full_path);
        }
    }

    page_len += table_len;
    printf("table_len: %ld\n", table_len);
    printf("f_row %s\n", entries[0]);
    //Create table
    char *table = malloc((table_len + 1) * sizeof(char));
    memset(table, 0, (table_len + 1) * sizeof(char));
    for (int j = 0; j < entries_count; j++) {
        printf("entry size: %ld\n", strlen(entries[j]));
        strcat(table, entries[j]);
    }

    // Create html
    char *html = malloc((strlen(response_html) + table_len + 1) * sizeof(char));

    sprintf(html, response_html, table);
    free(table);

    //Create page
    char *page = malloc((page_len + 1) * sizeof(char));
    strcpy(page, response_http);
    strcat(page, html);
    free(html);

    return page;
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
                    char *path = cmtor(buffer);
                    bool sum = true;
                    if (strcmp(path, "/") == 0)
                        sum = false;
                    char *full_path = malloc((strlen(root) + ((sum) ? strlen(path) : 0) + 1) * sizeof(char));
                    strcpy(full_path, root);
                    if (sum)
                        strcat(full_path, path);
                    free(path);
                    char *page = build_page(full_path);
                    free(full_path);
                    write(sd, page, strlen(page));
                    printf("End test-------------\n");
                }
            }
        }
    }

    return 0;
}