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
#include <sys/poll.h>

#define MAX_CLIENTS 1024
#define BUFFER_SIZE 16384

enum OrderBy {
    Name,
    Size,
    Date
};

#define RETURN_LINK_TEMPLATE "<p>&#x1F519 <a href=\"%s/.\">Go Back</a></p>"
#define DIR_LINK_TEMPLATE "<p>&#x1F4C1 <a href=\"%s/.\">%s/</a></p>"
#define FILE_LINK_TEMPLATE "<p>&#x1F4C4 <a href=\"%s\">%s</a></p>"

#define HTTP_HTML_HEADER "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: keep-alive\r\n\r\n"
#define HTTP_FILE_HEADER "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Disposition: attachment; filename=\"%s\"\nContent-Length: %zu\r\n\r\n"
#define HTTP_404_HEADER "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"

#define HTML_404_BODY "<html><head><title>404 Not Found</title></head><body><h1>404 Not Found</h1><p>The requested URL was not found on this server.</p></body></html>"
#define HTML_TR_TEMPLATE "<tr class=\"rows\">\n<td class=\"text\">\n%s\n</td>\n<td class=\"number\">\n%s\n</td>\n<td class=\"text\">\n%s\n</td>\n<td class=\"text\">\n%s\n</td>\n</tr>\n"

#define DATA_UNITS {"B", "KB", "MB", "GB", "TB", "EB", "ZB", "YB"};
#define UNITS_COUNT 8

typedef struct TableRow TableRow;
struct TableRow {
    char *row_string;
    char *item_name;
    size_t item_size;
    char *modification_date;
    bool isDir;
};

typedef struct Request Request;
struct Request {
    char *path;
    struct Dictionary options;
    bool valid;
};

// returns a <a> html link for a given path.
char *ptoa(char *p, bool isdir) {
    char *itemname = basename(p);

    bool isreturn = strcmp(itemname, "..") == 0;

    char *template = isreturn ? RETURN_LINK_TEMPLATE : (isdir ? DIR_LINK_TEMPLATE : FILE_LINK_TEMPLATE);

    size_t templen = strlen(template);


    size_t namelen = strlen(itemname);
    //    size_t plen = strlen(p);

    char *link = malloc((templen + namelen * 2 + 1) * sizeof(char));

    if (isreturn) {
        sprintf(link, template, itemname);
        return link;
    }

    sprintf(link, template, itemname, itemname);

    // free(itemname);
    return link;
}

char *clearpath(char *path) {
    size_t path_length = strlen(path);
    char *result = calloc(path_length + 1, sizeof(char));
    strcpy(result, path);
    int index;
    while ((index = findstr(result, "%20")) != -1) {
        size_t length = strlen(result);
        char *right = calloc(length - (index + 2), sizeof(char));
        strcpy(right, result + index + 3);
        result[index] = ' ';
        result[index + 1] = '\0';
        strcat(result, right);
        free(right);
    }
    return result;
}

// returns a request path extracted from a given client message
Request cmtor(char *message) {
    if (message == NULL) {
        Request ret = {NULL, {NULL, 0}, false};
        return ret;
    }

    int end = findc(message, '\n');
    if (end == -1)
        end = (int) strlen(message);

    struct JaggedCharArray req = splitnstr(message, ' ', end, true);

    if (req.count < 2) {
        Request ret = {NULL, {NULL, 0}, false};
        return ret;
    }

    req.arr++;
    req.count--;
    bool delslash = false;
    if (strlen(*req.arr) == 0) {
        Request ret = {NULL, {NULL, 0}, false};
        return ret;
    }

    if (req.arr[0][0] == '/') {
        req.arr[0]++;
        delslash = true;
    }

    char *req_str = joinarr(req, ' ', req.count - 1);

    if (delslash)
        req.arr[0]--;
    req.arr--;
    req.count++;

    for (int i = 0; i < req.count; ++i)
        free(req.arr[i]);
    free(req.arr);

    char *fixed_spaces = clearpath(req_str);
    free(req_str);

    if (strlen(fixed_spaces) == 0) {
        Request ret = {"", {NULL, 0}, true};
        return ret;
    }

    struct JaggedCharArray split_req = splitstr(fixed_spaces, '?', true);

    char *path = split_req.arr[0];

    if (split_req.count == 1 || strlen(split_req.arr[1]) == 0) {
        Request ret = {path, {NULL, 0}, true};
        return ret;
    }

    struct JaggedCharArray opts = splitstr(split_req.arr[1], '&', false);

    free(split_req.arr);

    struct Dictionary options;

    options.pairs = malloc(opts.count * sizeof(struct KeyValuePair));
    options.count = opts.count;
    for (int i = 0; i < options.count; ++i) {
        options.pairs[i].hasValue = false;
    }

    for (int i = 0; i < opts.count; ++i) {
        struct JaggedCharArray pair = splitstr(opts.arr[i], '=', false);
        if (pair.count < 2) {
            free(pair.arr);
            continue;
        }
        dset(&options, pair.arr[0], pair.arr[1]);
        free(pair.arr);
    }

    Request ret = {path, options, true};

    return ret;
}

char *stobytes(size_t size) {
    char *units[] = DATA_UNITS;

    int c = 0;
    int rem;
    while (size > 1000 && c < UNITS_COUNT - 1) {
        size_t newSize = size / 1000;
        rem = (int) (size - (newSize * 1000));
        size = newSize;
        c++;
    }

    char *ret = calloc(30, sizeof(char));

    if (c == 0)
        sprintf(ret, "%zu %s", size, units[c]);
    else
        sprintf(ret, "%zu.%d %s", size, rem / 100, units[c]);

    return ret;
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

    struct pollfd pfd = {.fd = clientfd, .events = POLLOUT};
    off_t offset = 0;
    size_t remaining_bytes = st.st_size;
    do {
        size_t ret = sendfile(clientfd, fd, &offset, remaining_bytes);
        if (ret >= 0) {
            remaining_bytes -= ret;
        } else if (errno == EAGAIN) {
            int pollresult = poll(&pfd, 1, -1);
            if (pollresult == -1) {
                perror("poll");
                return remaining_bytes;
            } else {
                if (pfd.revents & POLLOUT) {
                    continue;
                } else {
                    printf("Unexpected event on file descriptor\n");
                    return remaining_bytes;
                }
            }
        } else {
            perror("sendfile");
            return remaining_bytes;
        }
    } while (remaining_bytes > 0);

    close(fd);

    return 0;
}

int count_entries(char *path) {
    DIR *dir;
    int count = 0;
    if ((dir = opendir(path)) != NULL) {
        while (readdir(dir) != NULL)
            count++;
        closedir(dir);
        return count;
    }

    perror("Unable to open directory");
    return -1;
}

int compare_name(const void *a, const void *b) {
    TableRow *ta = (TableRow *) a;
    TableRow *tb = (TableRow *) b;

    if (strcmp(ta->item_name, "..") == 0)
        return -1;
    if (strcmp(tb->item_name, "..") == 0)
        return 1;

    if (ta->isDir != tb->isDir) {
        return ta->isDir ? 1 : -1;
    }

    return -strcmp(ta->item_name, tb->item_name);
}

int compare_name_a(const void *a, const void *b) {
    TableRow *ta = (TableRow *) a;
    TableRow *tb = (TableRow *) b;

    if (strcmp(ta->item_name, "..") == 0)
        return -1;
    if (strcmp(tb->item_name, "..") == 0)
        return 1;

    return compare_name(a, b) * -1;
}

int compare_size(const void *a, const void *b) {
    TableRow *ta = (TableRow *) a;
    TableRow *tb = (TableRow *) b;

    if (strcmp(ta->item_name, "..") == 0)
        return -1;
    if (strcmp(tb->item_name, "..") == 0)
        return 1;

    if (ta->isDir != tb->isDir) {
        return ta->isDir ? 1 : -1;
    }

    long long dif = (long long) ta->item_size - (long long) tb->item_size;
    return dif > 0 ? -1 : (dif == 0 ? 0 : 1);
}

int compare_size_a(const void *a, const void *b) {
    TableRow *ta = (TableRow *) a;
    TableRow *tb = (TableRow *) b;

    if (strcmp(ta->item_name, "..") == 0)
        return -1;
    if (strcmp(tb->item_name, "..") == 0)
        return 1;

    return compare_size(a, b) * -1;
}

int compare_date(const void *a, const void *b) {
    TableRow *ta = (TableRow *) a;
    TableRow *tb = (TableRow *) b;

    if (strcmp(ta->item_name, "..") == 0)
        return -1;
    if (strcmp(tb->item_name, "..") == 0)
        return 1;

//    if (ta->isDir != tb->isDir) {
//        return ta->isDir ? 1 : -1;
//    }

    struct tm tm1;
    struct tm tm2;
    strptime(ta->modification_date, "%a %b %d %H:%M:%S %Y", &tm1);
    strptime(tb->modification_date, "%a %b %d %H:%M:%S %Y", &tm2);
    time_t t1 = mktime(&tm1);
    time_t t2 = mktime(&tm2);

    double dif = difftime(t1, t2);

    return dif > 0 ? -1 : (dif == 0 ? 0 : 1);
}

int compare_date_a(const void *a, const void *b) {
    TableRow *ta = (TableRow *) a;
    TableRow *tb = (TableRow *) b;

    if (strcmp(ta->item_name, "..") == 0)
        return -1;
    if (strcmp(tb->item_name, "..") == 0)
        return 1;

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
    length += strlen(name);

    *itemSize = *isDir ? 0 : file_stat.st_size;
    length += 20; // 20 is the max length of a size_t (18446744073709551615)

    char *date = ctime(&file_stat.st_mtime);
    size_t date_len = strlen(date);
    date[date_len] = '\0';
    length += date_len;

    // get file permissions
    char *permissions = calloc(4, sizeof(char));
    strcat(permissions, (file_stat.st_mode & S_IRUSR) ? "r" : "");
    strcat(permissions, (file_stat.st_mode & S_IWUSR) ? "w" : "");
    strcat(permissions, (file_stat.st_mode & S_IXUSR) ? "x" : "");

    length += 3;

    char *row = calloc(length + 1, sizeof(char));

    char *sizeStr = *isDir ? "-" : stobytes(*itemSize);

    sprintf(row, HTML_TR_TEMPLATE, name, sizeStr, date, permissions);

    strcpy(modificationDate, date);

    return row;
}

char *build_page(Request req, char *page_template) {
    bool isCwdRoot = false;

    printf("PATH = %s\n", req.path);
    for (int i = 0; i < req.options.count; ++i) {
        if (req.options.pairs[i].hasValue)
            printf("%s = %s\n", req.options.pairs[i].key, req.options.pairs[i].value);
    }

    char *path = req.path;

    enum OrderBy order_by = Name;
    bool ascending = true;

    int idx = -1;
    char *order = dtryget(req.options, "orderby", &idx);
    if (idx >= 0) {
        if (strcmp(order, "name") == 0 || strcmp(order, "Name") == 0)
            order_by = Name;
        else if (strcmp(order, "size") == 0 || strcmp(order, "Size") == 0)
            order_by = Size;
        else if (strcmp(order, "date") == 0 || strcmp(order, "Date") == 0)
            order_by = Date;
    }

    char *asc = dtryget(req.options, "ascending", &idx);
    if (idx >= 0) {

        if (strcmp(asc, "false") == 0 || strcmp(asc, "False") == 0)
            ascending = false;
    }

    char *pcpy = NULL;
    if (strlen(path) == 0) {
        pcpy = calloc(2, sizeof(char));
        strcpy(pcpy, ".");
        isCwdRoot = true;
    } else {
        bool endsInSlash = path[strlen(path) - 1] == '/';

        pcpy = calloc(strlen(path) + (endsInSlash ? 0 : 1), sizeof(char));
        strncpy(pcpy, path, strlen(path) - (endsInSlash ? 1 : 0));
    }
    // Will never happen, but some linters show a warning if you don't do this.
    if (pcpy == NULL)
        return NULL;

    DIR *dir = opendir(pcpy);

    if (dir == NULL) {
        // extra 20 to account for the Content-Length
        char *ret = calloc(strlen(HTML_404_BODY) + strlen(HTTP_404_HEADER) + 20, sizeof(char));
        strcpy(ret, HTTP_404_HEADER HTML_404_BODY);

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

    while ((dir_entry = readdir(dir)) != NULL) {
        if (strcmp(dir_entry->d_name, ".") == 0
            || (isCwdRoot && strcmp(dir_entry->d_name, "..") == 0))
            continue;

        size_t namelen = strlen(dir_entry->d_name);
        size_t fp_len = isCwdRoot
                        ? (namelen + 1)
                        : (strlen(pcpy) + namelen + 2);
        char full_path[fp_len];

        if (isCwdRoot)
            strcpy(full_path, dir_entry->d_name);
        else
            sprintf(full_path, "%s/%s", pcpy, dir_entry->d_name);

        size_t item_size = 0;
        // Dates only go up to 24 characters before the year 10000, so I guess this is future proofing
        char *item_date = calloc(30, sizeof(char));
        bool isDir = false;

        char *row_string = create_tr(full_path, &item_size, item_date, &isDir);

        if (row_string == NULL)
            continue;

        char *item_name = calloc(namelen + 1, sizeof(char));
        strcpy(item_name, dir_entry->d_name);

        TableRow row = {row_string, item_name, item_size, item_date, isDir};
        entries[i] = row;

        size_t row_len = strlen(row_string);
        table_len += row_len;

        i++;
    }

    closedir(dir);

    free(pcpy);

    entries_count = i;

    sort_entries(entries, entries_count, order_by, ascending);

    page_len += table_len;

    // Create tableRows
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

    page_len += 5; // to account for the parameter substitution and the arrow, and some extra

    // Create html
    char *page = calloc(page_len + 1, sizeof(char));

    switch (order_by) {
        case Name:
            sprintf(page, page_template, (ascending) ? "false" : "true", (ascending) ? "↓" : "↑", "true", "", "true",
                    "", tableRows);
            break;
        case Size:
            sprintf(page, page_template, "true", "", (ascending) ? "false" : "true", (ascending) ? "↓" : "↑", "true",
                    "", tableRows);
            break;
        case Date:
            sprintf(page, page_template, "true", "", "true", "", (ascending) ? "false" : "true",
                    (ascending) ? "↓" : "↑", tableRows);
            break;
    }

    free(tableRows);

    char *ret = calloc(strlen(page) + strlen(HTTP_HTML_HEADER) + 1, sizeof(char));
    sprintf(ret, "%s%s", HTTP_HTML_HEADER, page);

    free(page);

    return ret;
}

void handle_client(int sd, Request req, char *page_template) {
    pid_t pid = fork();

    if (pid != 0)
        return;

    char *path = req.path;

    if (strcmp(path, "") == 0) {
        char *page = build_page(req, page_template);
        write(sd, page, strlen(page));
        shutdown(sd, SHUT_WR);
        free(page);
        exit(0);
    }

    struct stat file_stat;
    if (stat(path, &file_stat) != 0) {
        fprintf(stderr, "Error getting stats, for \"%s\" ", path);
        printf("errno = %d\n", errno);

        char *page = build_page(req, page_template);
        write(sd, page, strlen(page));
        shutdown(sd, SHUT_WR);
        free(page);
        exit(0);
    }

    if (S_ISDIR(file_stat.st_mode)) {
        char *page = build_page(req, page_template);
        write(sd, page, strlen(page));
        shutdown(sd, SHUT_WR);
        free(page);
    } else {
        size_t sent = sendfile_p(path, sd);
        shutdown(sd, SHUT_WR);
    }

    exit(0);
}

// To run the server : gcc server.c -o server
//                     ./server 31431 /rootpath
int main(int argc, char *argv[]) {
#pragma region Server Initialization
    if (argc < 3) {
        printf("usage: %s <server_port> <root_directory>\n", argv[0]);
        exit(1);
    }

    int port = (int) strtol(argv[1], NULL, 10);

    char *root = argv[2];

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
    char *buffer = calloc(BUFFER_SIZE, sizeof(char));

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
                    Request req = cmtor(buffer);

                    if (req.valid)
                        handle_client(sd, req, page_template);
                    else
                        printf("Invalid request:\n%s\n", buffer);

                    free(req.options.pairs);
                }
            }
        }
    }
}