#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    int client_fd, n;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in server_addr;

    // create socket
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        perror("socket");
        exit(1);
    }

    // set server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(1234);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // connect to server
    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        exit(1);
    }

    while (1) {
        // prompt user for input
        printf("Enter message: ");
        fgets(buffer, BUFFER_SIZE, stdin);

        // send message to server
        if (send(client_fd, buffer, strlen(buffer), 0) < 0) {
            perror("send");
            exit(1);
        }
    }

    return 0;
}