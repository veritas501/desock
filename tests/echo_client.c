// https://mohsensy.github.io/programming/2019/09/25/echo-server-and-client-using-sockets-in-c.html

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT (12345)

int main(int argc, char **argv) {
    int server_fd;
    struct sockaddr_in server;

    int len;
    char *out_buffer = "I'm client";

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("[-] Cannot create socket");
        exit(1);
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(SERVER_IP);
    server.sin_port = htons(SERVER_PORT);
    if (connect(server_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("[-] Cannot connect to server");
        exit(1);
    }

    printf("[*] Sending \"%s\" to server\n", out_buffer);
    if (send(server_fd, out_buffer, strlen(out_buffer), 0) < 0) {
        perror("[-] Cannot write");
        exit(1);
    }

    char in_buffer[0x200] = {0};
    if (recv(server_fd, in_buffer, sizeof(in_buffer), 0) < 0) {
        perror("[-] Cannot read");
        exit(1);
    }
    printf("[+] Received \"%s\" from server\n", in_buffer);

    close(server_fd);
    return 0;
}