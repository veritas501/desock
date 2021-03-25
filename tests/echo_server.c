// https://mohsensy.github.io/programming/2019/09/25/echo-server-and-client-using-sockets-in-c.html

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_PORT (12345)

int main(int argc, char **argv) {
    int server_fd = -1, client_fd = -1;
    struct sockaddr_in server, client;

    int len;
    char in_buffer[0x100] = {0};
    char out_buffer[0x200] = {0};

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("[-] Cannot create socket");
        exit(1);
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(SERVER_PORT);
    if (bind(server_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("[-] Cannot bind socket");
        exit(1);
    }
    if (listen(server_fd, 10) < 0) {
        perror("[-] Listen error");
        exit(1);
    }

    while (1) {
        printf("[*] Waiting for clients\n");
        len = sizeof(client);
        if ((client_fd = accept(server_fd, (struct sockaddr *)&client, &len)) < 0) {
            perror("[-] Accept error");
            exit(1);
        }
        char *client_ip = inet_ntoa(client.sin_addr);
        printf("[+] Accepted new connection from a client %s:%d\n", client_ip, ntohs(client.sin_port));
        int recv_len = recv(client_fd, in_buffer, sizeof(in_buffer) - 1, 0);
        if (recv_len < 0) {
            perror("[-] Read error");
            exit(1);
        }
        in_buffer[recv_len] = 0;
        if (in_buffer[recv_len - 1] == '\n') {
            in_buffer[recv_len - 1] = 0;
            recv_len -= 1;
        }
        printf("[*] Received \"%s\" from client\n", in_buffer);
        int send_len = snprintf(out_buffer, 0x200, "your input is %s", in_buffer);
        printf("[*] Sending \"%s\" to client\n", out_buffer);
        if (send(client_fd, out_buffer, send_len, 0) < 0) {
            perror("[-] Write error");
            exit(1);
        }
        close(client_fd);
    }
    close(server_fd);
    return 0;
}