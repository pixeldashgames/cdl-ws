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
#include "cdl-utils.h"

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

#define DIR_LINK_TEMPLATE "<p>&#x1F4C1 <a href=\"%s\">%s/</a></p>"
#define FILE_LINK_TEMPLATE "<p>&#x1F4C4 <a href=\"%s\">%s</a></p>"

char *sbasename(char *p)
{
    int n = (int)strlen(p);

    printf("a %d\n", n);
    fflush(stdout);

    char *name = calloc(1, (n + 1) * sizeof(char));
    if (name == NULL)
    {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }

    printf("b %d\n", n);
    fflush(stdout);

    char *pcpy = calloc(1, (n + 1) * sizeof(char));
    if (pcpy == NULL)
    {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }
    strcpy(pcpy, p);

    printf("c %d\n", n);
    fflush(stdout);
    if (pcpy[n - 1] == '/')
    {
        pcpy[--n] = '\0';
    }
    for (int i = (int)n - 1; i >= 0; i--)
    {
        if (pcpy[i] != '/')
            continue;

        strcpy(name, pcpy + i + 1);
        break;
    }

    free(pcpy);

    return name;
}

// returns a <a> html link for a given path.
char *ptoa(char *p, bool isdir)
{
    printf("path: %s", p);
    fflush(stdout);

    char *template = isdir ? DIR_LINK_TEMPLATE : FILE_LINK_TEMPLATE;

    size_t templen = strlen(template);

    printf("path: %s", p);
    fflush(stdout);

    char *itemname = sbasename(p);

    size_t namelen = strlen(itemname);
    size_t plen = strlen(p);

    printf("templen: %zu, plen: %zu, namelen: %zu", templen, plen, namelen);
    fflush(stdout);

    char *link = malloc((templen + plen + namelen + 1) * sizeof(char));

    sprintf(link, template, p, itemname);

    // free(itemname);
    return link;
}

char *cmtorp(char *message)
{
    int end = findc(message, '\n');
    if (end == -1)
        end = (int)strlen(message);

    struct JaggedCharArray req = splitnstr(message, ' ', end, true);
    req.arr += 1;
    req.count -= 1;
    return joinarr(req, ' ', req.count - 1);
}

int run_tests(__attribute__((unused)) int argc, __attribute__((unused)) char *argv[])
{
    bool ptoa_test0 =
        strcmp(ptoa("/home/user/mydir", true), "<p>&#x1F4C1 <a href=\"/home/user/mydir\">mydir/</a></p>") == 0;
    printf("ptoa Dir Test: %s\n", ptoa_test0 ? "✅" : "❌");

    bool ptoa_test1 =
        strcmp(ptoa("/home/user/myfile", false), "<p>&#x1F4C4 <a href=\"/home/user/myfile\">myfile</a></p>") == 0;
    printf("ptoa File Test: %s\n", ptoa_test1 ? "✅" : "❌");

    bool name_test0 = strcmp(sbasename("/home/user/mydir/"), "mydir") == 0;
    printf("basename Dir Test: %s\n", name_test0 ? "✅" : "❌");

    bool name_test1 = strcmp(sbasename("/home/user/myfile"), "myfile") == 0;
    printf("basename File Test: %s\n", name_test1 ? "✅" : "❌");

    bool cmtor_test0 = strcmp(cmtorp("GET /home/user/my dir HTTP/1.1\nRandom Browser Data\nConnection Request"),
                              "/home/user/my dir") == 0;
    printf("cmtor Test 0: %s\n", cmtor_test0 ? "✅" : "❌");

    bool cmtor_test1 = strcmp(cmtorp("GET /home/my user/my dir/my    spaced    dir HTTP/1.1"),
                              "/home/my user/my dir/my    spaced    dir") == 0;
    printf("cmtor Test 1: %s\n", cmtor_test1 ? "✅" : "❌");

    int correctCount = ptoa_test0 + ptoa_test1 + name_test0 + name_test1 + cmtor_test0 + cmtor_test1;
    int total = 6;
    bool allCorrect = correctCount == total;
    printf("Testing Finished. Results %d/%d %s", correctCount, total, allCorrect ? "✅" : "❌");

    return allCorrect ? 0 : 1;
}

const char *response_http = "HTTP/1.1 200 OK\nContent-Type: text/html\nConnection: Closed\n\n";
const char *response_html = "<html><head><title>Root</title></head><body><table><tr><th>Name</th><th>Size</th><th>Date</th></tr>%s</table></body></html>";

int count_entries(char *path)
{
    DIR *dir;
    struct dirent *dir_entry;
    int count = 0;
    if ((dir = opendir(path)) != NULL)
    {
        while ((dir_entry = readdir(dir)) != NULL)
            count++;
        closedir(dir);
    }
    else
    {
        perror("Unable to open directory");
        return -1;
    }
    return count;
}

char *create_tr(char *path)
{
    printf("a\n");

    DIR *dir;
    struct dirent *dir_entry;
    printf("a\n");

    struct stat file_stat;
    printf("a\n");

    if (stat(path, &file_stat) != 0)
    {
        perror("Error getting stats");
        return NULL;
    }
    printf("a\n");

    size_t length = 0;
    printf("a\n");

    char *name = ptoa(path, S_ISDIR(file_stat.st_mode));
    printf("a\n");

    fflush(stdout);

    length += strlen(name) + 9;
    printf("NAME: %s\n", name);
    fflush(stdout);

    size_t size = S_ISDIR(file_stat.st_mode) ? 0 : file_stat.st_size;
    char c_size[24];
    sprintf(c_size, "%zu", size);
    length += strlen(c_size);
    printf("SIZE: %zu\n", size);
    fflush(stdout);

    char *date = ctime(&file_stat.st_mtime);
    date[strlen(date) - 1] = '\0';
    length += strlen(date) + 9;
    printf("DATE: %s\n", date);
    fflush(stdout);

    char *row = malloc((length + 1) * sizeof(char));

    sprintf(row, "<tr>%s</tr><tr>%s</tr><tr>%s</tr>", name, c_size, date);
    printf("ROW: %s\n", row);
    fflush(stdout);

    return row;
}

// TODO: Allow to create a table with multiples properties
char *build_page(char *path)
{
    DIR *dir;
    struct dirent *dir_entry;
    int entries_count = count_entries(path);
    int i = 0;

    // TODO: make a page for wrong paths

    size_t page_len = strlen(response_http) + strlen(response_html);
    size_t table_len = 0;
    char *entries[entries_count];

    if ((dir = opendir(path)) != NULL)
    {
        printf("open dir OK\n");
        while ((dir_entry = readdir(dir)) != NULL)
        {
            if (strcmp(dir_entry->d_name, ".") == 0 || strcmp(dir_entry->d_name, "..") == 0)
            {
                continue; // skip current and parent directories
            }
            printf("read dir OK\n");
            size_t fp_len = strlen(path) + strlen(dir_entry->d_name) + 2;
            printf("fp_len: %ld\n", fp_len);
            char full_path[fp_len];
            sprintf(full_path, "%s/%s", path, dir_entry->d_name);
            printf("a");
            char *row = create_tr(full_path);
            printf("Created row");
            size_t row_len = strlen(row);
            table_len += row_len;
            entries[i++] = row;
            printf("Looped");
        }
    }

    page_len += table_len;
    printf("table_len: %ld\n", table_len);
    printf("f_row %s\n", entries[0]);
    // Create table
    char *table = malloc((table_len + 1) * sizeof(char));
    memset(table, 0, (table_len + 1) * sizeof(char));
    for (int j = 0; j < entries_count; j++)
    {
        printf("entry size: %ld\n", strlen(entries[j]));
        strcat(table, entries[j]);
    }

    // Create html
    char *html = malloc((strlen(response_html) + table_len + 1) * sizeof(char));

    sprintf(html, response_html, table);
    free(table);

    // Create page
    char *page = malloc((page_len + 1) * sizeof(char));
    strcpy(page, response_http);
    strcat(page, html);
    free(html);

    return page;
}

// To run the server : gcc server.c -o server
//                     ./server 31431 /rootpath
int main(int argc, char *argv[])
{
    if (strcmp(argv[argc - 1], "test") == 0)
    {
        return run_tests(argc, argv);
    }

    if (argc < 3)
    {
        printf("usage: %s <server_port> <root_directory>\n", argv[0]);
        exit(1);
    }

    int port = (int)strtol(argv[1], NULL, 10);

    char *root = argv[2];

    if (access(root, F_OK) == -1)
    {
        printf("%s is not a valid path.\n", root);
    }
    // if (chdir(root) != 0) {
    //     perror("Couldn't create server on the specified path.\n");
    //     exit(1);
    // }

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
        perror("Couldn't create server socket on the specified port.\n");
        exit(1);
    }

    printf("Server socket created...\n");

    // set server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // bind socket to address
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Couldn't bind server address to socket.\n");
        exit(1);
    }

    printf("Server socket bound to port...\n");

    // listen for incoming connections
    if (listen(server_fd, MAX_CLIENTS) < 0)
    {
        perror("Error trying to listen to incoming connections.\n");
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
            perror("Error trying to wait for activity from clients.\n");
            exit(1);
        }

        // handle incoming connection request
        // FD_ISSET checks whether there is a connection request on the server
        if (FD_ISSET(server_fd, &readfds))
        {
            socklen_t client_len = sizeof(client_addr);
            if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len)) < 0)
            {
                perror("Error accepting connection request from client.\n");
                exit(1);
            }

            // add new client to list of clients
            for (i = 0; i < max_clients; i++)
            {
                if (clients[i] == 0)
                {
                    clients[i] = client_fd;
                    printf("New connection from %s:%d, socket fd: %d\n", inet_ntoa(client_addr.sin_addr),
                           ntohs(client_addr.sin_port), client_fd);
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