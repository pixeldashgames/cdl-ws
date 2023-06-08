#define _XOPEN_SOURCE 700

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
#include <errno.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

enum OrderBy {
    Name,
    Size,
    Date
};

#define DIR_LINK_TEMPLATE "<p>&#x1F4C1 <a href=\"%s\">%s/</a></p>"
#define FILE_LINK_TEMPLATE "<p>&#x1F4C4 <a href=\"%s\">%s</a></p>"

#define HTTP_HTML_HEADER "HTTP/1.1 200 OK\nContent-Type: text/html\nConnection: close\n\n"
#define HTTP_FILE_HEADER "HTTP/1.1 200 OK\nContent-Type: application/octet-stream\nContent-Disposition: attachment; filename=\"%s\"\nContent-Length: %zu\n\n"
#define HTTP_404_HEADER "HTTP/1.1 404 Not Found\nContent-Type: text/html\nConnection: close\n\n"

#define HTML_404_BODY "<html><head><title>404 Not Found</title></head><body><h1>404 Not Found</h1><p>The requested URL was not found on this server.</p></body></html>"
#define HTML_TR_TEMPLATE "<tr class=\"rows\">\n<td class=\"text\">\n%s\n</td>\n<td class=\"number\">\n%zu\n</td>\n<td class=\"text\">\n%s\n</td>\n</tr>\n"

typedef struct TableRow TableRow;
struct TableRow {
    char *row_string;
    char *item_name;
    size_t item_size;
    char *modification_date;
    bool isDir;
};

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
    int total = 4;
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

int compare_name(const void *a, const void *b) {
    TableRow *ta = (TableRow *) a;
    TableRow *tb = (TableRow *) b;

    if (ta->isDir != tb->isDir) {
        return ta->isDir ? -1 : 1;
    }

    return strcmp(ta->item_name, tb->item_name);;
}

int compare_name_a(const void *a, const void *b) {
    return compare_name(a, b) * -1;
}


int compare_size(const void *a, const void *b) {
    TableRow *ta = (TableRow *) a;
    TableRow *tb = (TableRow *) b;

    if (ta->isDir != tb->isDir) {
        return ta->isDir ? 1 : -1;
    }

    long long dif = (long long) ta->item_size - (long long) tb->item_size;
    return dif > 0 ? 1 : (dif == 0 ? 0 : -1);
}

int compare_size_a(const void *a, const void *b) {
    return compare_size(a, b) * -1;
}

int compare_date(const void *a, const void *b) {
    TableRow *ta = (TableRow *) a;
    TableRow *tb = (TableRow *) b;

    if (ta->isDir != tb->isDir) {
        return ta->isDir ? 1 : -1;
    }

    struct tm tm1;
    struct tm tm2;
    strptime(ta->modification_date, "%a %b %d %H:%M:%S %Y", &tm1);
    strptime(tb->modification_date, "%a %b %d %H:%M:%S %Y", &tm2);
    time_t t1 = mktime(&tm1);
    time_t t2 = mktime(&tm2);

    double dif = difftime(t1, t2);

    return dif > 0 ? 1 : (dif == 0 ? 0 : -1);
}

int compare_date_a(const void *a, const void *b) {
    return compare_date(a, b) * -1;
}

void sort_entries(TableRow *entries, int count, enum OrderBy order_by, bool ascending) {
    switch (order_by) {
        case Name:
            qsort(entries, count, sizeof(TableRow), ascending ? compare_name_a : compare_name);
            break;
        case Size:
            qsort(entries, count, sizeof(TableRow), ascending ? compare_size_a : compare_size);
            break;
        case Date:
            qsort(entries, count, sizeof(TableRow), ascending ? compare_date_a : compare_date);
            break;
    }
}

char *create_tr(char *path, size_t *itemSize, char *modificationDate, bool *isDir) {
    struct stat file_stat;
    if (stat(path, &file_stat) != 0) {
        perror("Error getting stats");
        return NULL;
    }

    size_t length = strlen(HTML_TR_TEMPLATE);

    *isDir = S_ISDIR(file_stat.st_mode);

    char *name = ptoa(path, *isDir);
    printf("name ptoa: %s\n", name);
    length += strlen(name);

    *itemSize = *isDir ? 0 : file_stat.st_size;
    length += 20; // 20 is the max length of a size_t (18446744073709551615)

    char *date = ctime(&file_stat.st_mtime);
    size_t date_len = strlen(date);
    date[date_len] = '\0';
    length += date_len;

    char *row = calloc(length + 1, sizeof(char));

    sprintf(row, HTML_TR_TEMPLATE, name, *itemSize, date);

    strcpy(modificationDate, date);

    return row;
}

// TODO: Allow to create a table with multiples properties
char *build_page(char *path, char *page_template, enum OrderBy order_by, bool sort_asc) {
    bool endsInSlash = path[strlen(path) - 1] == '/';

    char *pcpy = NULL;
    if (strcmp(path, "/") == 0) {
        pcpy = calloc(2, sizeof(char));
        strcpy(pcpy, ".");
    } else {
        pcpy = calloc(strlen(path) + (endsInSlash ? 0 : 1), sizeof(char));
        strncpy(pcpy, path, strlen(path) - (endsInSlash ? 1 : 0));
    }

    if (pcpy == NULL)
        exit(1);

    DIR *dir = opendir(pcpy);

    if (dir == NULL) {
        char *ret = calloc(strlen(HTML_404_BODY) + strlen(HTTP_404_HEADER) + 1, sizeof(char));
        sprintf(ret, "%s%s", HTTP_404_HEADER, HTML_404_BODY);

        free(pcpy);

        return ret;
    }

    struct dirent *dir_entry;
    int i;
    int entries_count = count_entries(pcpy);

    size_t page_len = strlen(page_template);
    size_t table_len = 0;

    TableRow *entries = malloc(entries_count * sizeof(TableRow));
//    for (i = 0; i < entries_count; ++i)
//        entries[i] = NULL;

    i = 0;

    if (dir != NULL) {
        while ((dir_entry = readdir(dir)) != NULL) {
            if (strcmp(dir_entry->d_name, ".") == 0 /*|| strcmp(dir_entry->d_name, "..") == 0*/)
                continue; // skip current directory
            if (dir_entry->d_name[0] == '.' && strcmp(dir_entry->d_name, "..") != 0)
                continue; // skip hidden files
            // full path = path/dir
            size_t namelen = strlen(dir_entry->d_name);
            size_t fp_len = strlen(pcpy) + namelen + 2;
            char full_path[fp_len];
            sprintf(full_path, "%s/%s", pcpy, dir_entry->d_name);
            printf("full_path: %s\n", full_path);

            size_t item_size = 0;
            // Dates only go up to 24 characters before the year 10000, so I guess this is future proofing
            char *item_date = calloc(30, sizeof(char));
            bool isDir = false;

            char *row_string = create_tr(full_path, &item_size, item_date, &isDir);

            char *item_name = calloc(namelen + 1, sizeof(char));
            strcpy(item_name, dir_entry->d_name);

            TableRow row = {row_string, item_name, item_size, item_date, isDir};
            entries[i] = row;

            size_t row_len = strlen(row_string);
            table_len += row_len;

            i++;
        }
    }

    free(pcpy);

    entries_count = i;

    sort_entries(entries, entries_count, order_by, sort_asc);

    page_len += table_len;

    //Create tableRows
    char *tableRows = calloc(table_len + 1, sizeof(char));

    char *en = NULL;

    for (i = 0; i < entries_count; i++) {
        en = entries[i].row_string;
        if (en == NULL)
            continue;

        strcat(tableRows, en);
        free(en);
        free(entries[i].modification_date);
        free(entries[i].item_name);
    }

    free(entries);

    // Create html
    char *page = calloc(page_len + 1, sizeof(char));

    sprintf(page, page_template, tableRows);

    free(tableRows);

    char *ret = calloc(strlen(page) + strlen(HTTP_HTML_HEADER) + 1, sizeof(char));
    sprintf(ret, "%s%s", HTTP_HTML_HEADER, page);

    free(page);

    return ret;
}

// Remove entirely "%20" from path to create a path understandable for console introducing a backslash before the space
char *clnp(char *path) {
    char *pcpy = calloc(strlen(path) + 1, sizeof(char));
    strcpy(pcpy, path);

    char *pcpy2 = calloc(strlen(path) + 1, sizeof(char));
    strcpy(pcpy2, path);

    char *token = strtok(pcpy, "%20");
    char *token2 = strtok(pcpy2, "%20");

    char *ret = calloc(strlen(path) + 1, sizeof(char));

    while (token != NULL) {
        strcat(ret, token);
        strcat(ret, "\\ ");
        token = strtok(NULL, "%20");
    }

    ret[strlen(ret) - 2] = '\0';

    free(pcpy);
    free(pcpy2);

    return ret;
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

//    if (access(root, F_OK) == -1) {
//        printf("%s is not a valid path.\n", root);
//    }

    // Read the page HTML template
    FILE *templatef = fopen("template.html", "r");
    struct stat templateStats = {};
    if (fstat(fileno(templatef), &templateStats) != 0) {
        perror("Failed to find template.html");
        exit(1);
    }
    char *page_template = readtoend(templatef);
    fclose(templatef);

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
                    char *path = cmtorp(buffer);
                    path = clnp(path);
                    printf("PATH: %s\n", path);
                    printf("BUFFER----------------------------------\n");
                    printf("%s\n", buffer);

                    // Handling path
                    char full_path[strlen(path) + 1];
                    sprintf(full_path, ".%s", path);
                    printf("FULL PATH: %s\n", full_path);

                    struct stat file_stat;
                    if (stat(full_path, &file_stat) != 0) {
                        perror("Error getting stats");
                        printf("errno = %d\n", errno);
                    }
                    printf("after stat method\n");
                    if (S_ISDIR(file_stat.st_mode)) {
                        printf("IS DIR\n");
                        char *page = build_page(full_path, page_template, Name, false);
                        write(sd, page, strlen(page));
                    } else {
                        printf("IS FILE\n");
                        size_t sent = sendfile_p(path, sd);
                        printf("Sent %zu bytes\n", sent);
                    }
                    free(path);
                }
            }
        }
    }
}