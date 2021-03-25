#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define ENV_ISSET(name) (getenv(name) && (strcmp(getenv(name), "1") == 0))

//
// ------------------------- preeny logging module -------------------------
//
int cfg_debug_on = 0;       // show debug msg
int cfg_info_on = 0;        // show info msg
int cfg_error_on = 1;       // show error msg
int cfg_exec_fast = 0;      // use poll timeout = -1 && pthread_kill to execute faster
int cfg_close_not_exit = 0; // don't exit when close() called

__attribute__((constructor)) void read_config() {
    cfg_debug_on |= ENV_ISSET("PREENY_DEBUG");
    cfg_info_on |= ENV_ISSET("PREENY_INFO");
    cfg_error_on |= ENV_ISSET("PREENY_ERROR");
    cfg_exec_fast |= ENV_ISSET("PREENY_EXEC_FAST");
    cfg_close_not_exit |= ENV_ISSET("PREENY_CLOSE_NOT_EXIT");
}

void preeny_debug(char *fmt, ...) {
    if (!cfg_debug_on)
        return;

    printf("[DEBUG] ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    fflush(stdout);
}

void preeny_info(char *fmt, ...) {
    if (!cfg_info_on)
        return;

    printf("[info] ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    fflush(stdout);
}

void preeny_error(char *fmt, ...) {
    if (!cfg_error_on)
        return;

    fprintf(stderr, "[ERROR] ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fflush(stderr);
}

__attribute__((constructor)) void set_io_buf() {
    setvbuf(stdin, 0, 2, 0);
    setvbuf(stdout, 0, 2, 0);
    setvbuf(stderr, 0, 2, 0);
}

#define PREENY_MAX_FD 8192
#define READ_BUF_SIZE 0x10000
#define PREENY_SIN_PORT 13337

int preeny_desock_shutdown_flag = 0;
int preeny_desock_accepted_sock = -1;

pthread_t *preeny_socket_threads_to_front[PREENY_MAX_FD] = {0};
pthread_t *preeny_socket_threads_to_back[PREENY_MAX_FD] = {0};

int preeny_socket_sync(int from, int to, int timeout) {
    struct pollfd poll_in = {from, POLLIN, 0};
    char read_buf[READ_BUF_SIZE];
    int total_n;
    char error_buf[1024];
    int n;
    int r;

    r = poll(&poll_in, 1, timeout);
    if (r < 0) {
        strerror_r(errno, error_buf, 1024);
        preeny_debug("read poll() received error '%s' on fd %d\n", error_buf, from);
        return 0;
    } else if (poll_in.revents == 0) {
        preeny_debug("read poll() timed out on fd %d\n", from);
        return 0;
    }

    total_n = read(from, read_buf, READ_BUF_SIZE);
    if (total_n < 0) {
        strerror_r(errno, error_buf, 1024);
        preeny_info("synchronization of fd %d to %d shutting down due to read error '%s'\n", from, to, error_buf);
        return -1;
    } else if (total_n == 0 && from == 0) {
        preeny_info("synchronization of fd %d to %d shutting down due to EOF\n", from, to);
        return -1;
    }
    preeny_debug("read %d bytes from %d (will write to %d)\n", total_n, from, to);

    n = 0;
    while (n != total_n) {
        r = write(to, read_buf, total_n - n);
        if (r < 0) {
            strerror_r(errno, error_buf, 1024);
            preeny_info("synchronization of fd %d to %d shutting down due to read error '%s'\n", from, to, error_buf);
            return -1;
        }
        n += r;
    }

    preeny_debug("wrote %d bytes to %d (had read from %d)\n", total_n, to, from);
    return total_n;
}

__attribute__((destructor)) void preeny_desock_shutdown() {
    int i;
    int to_sync[PREENY_MAX_FD] = {};

    preeny_debug("shutting down desock...\n");
    preeny_desock_shutdown_flag = 1;

    for (i = 0; i < PREENY_MAX_FD; i++) {
        if (preeny_socket_threads_to_front[i]) {
            preeny_debug("sending SIGINT to thread %d...\n", i);
            if (cfg_exec_fast) {
                pthread_kill(*preeny_socket_threads_to_front[i], SIGINT);
                pthread_kill(*preeny_socket_threads_to_back[i], SIGINT);
            } else {
                pthread_join(*preeny_socket_threads_to_front[i], NULL);
                pthread_join(*preeny_socket_threads_to_back[i], NULL);
            }
            preeny_debug("... sent!\n");
            to_sync[i] = 1;
        }
    }

    for (i = 0; i < PREENY_MAX_FD; i++) {
        if (to_sync[i]) {
            while (preeny_socket_sync(PREENY_SOCKET(i), 1, 0) > 0) {
                // loop
            }
        }
    }

    preeny_debug("... shutdown complete!\n");
}

void preeny_socket_sync_loop(int from, int to) {
    preeny_debug("starting forwarding from %d to %d!\n", from, to);

    int poll_timeout = cfg_exec_fast ? -1 : 15;
    while (!preeny_desock_shutdown_flag) {
        if (preeny_socket_sync(from, to, poll_timeout) < 0) {
            return;
        }
    }
}

#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"

void *preeny_socket_sync_to_back(void *back_fd) {
    preeny_socket_sync_loop((int)back_fd, 1);
    return NULL;
}

void *preeny_socket_sync_to_front(void *back_fd) {
    preeny_socket_sync_loop(0, (int)back_fd);
    shutdown((int)back_fd, SHUT_WR);
    return NULL;
}

//
// originals
//
int (*original_socket)(int, int, int);
int (*original_bind)(int, const struct sockaddr *, socklen_t);
int (*original_listen)(int, int);
int (*original_accept)(int, struct sockaddr *, socklen_t *);
int (*original_connect)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int (*original_close)(int fd);
int (*original_shutdown)(int sockfd, int how);
int (*original_getsockname)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

__attribute__((constructor)) void preeny_desock_orig() {
    original_socket = dlsym(RTLD_NEXT, "socket");
    original_listen = dlsym(RTLD_NEXT, "listen");
    original_accept = dlsym(RTLD_NEXT, "accept");
    original_bind = dlsym(RTLD_NEXT, "bind");
    original_connect = dlsym(RTLD_NEXT, "connect");
    original_close = dlsym(RTLD_NEXT, "close");
    original_shutdown = dlsym(RTLD_NEXT, "shutdown");
    original_getsockname = dlsym(RTLD_NEXT, "getsockname");
}

int socket(int domain, int type, int protocol) {
    int fds[2];
    int front_socket;
    int back_socket;

    if (domain != AF_INET && domain != AF_INET6) {
        preeny_info("Ignoring non-internet socket.");
        return original_socket(domain, type, protocol);
    }

    int r = socketpair(AF_UNIX, type, 0, fds);
    preeny_debug("Intercepted socket()!\n");

    if (r != 0) {
        perror("preeny socket emulation failed:");
        return -1;
    }

    preeny_debug("... created socket pair (%d, %d)\n", fds[0], fds[1]);

    front_socket = fds[0];
    back_socket = fds[1];

    preeny_socket_threads_to_front[fds[0]] = malloc(sizeof(pthread_t));
    preeny_socket_threads_to_back[fds[0]] = malloc(sizeof(pthread_t));

    r = pthread_create(preeny_socket_threads_to_front[fds[0]], NULL, preeny_socket_sync_to_front, (void *)back_socket);
    if (r) {
        perror("failed creating front-sync thread");
        return -1;
    }

    r = pthread_create(preeny_socket_threads_to_back[fds[0]], NULL, preeny_socket_sync_to_back, (void *)back_socket);
    if (r) {
        perror("failed creating back-sync thread");
        return -1;
    }

    return fds[0];
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    if (preeny_desock_accepted_sock >= 0) {
        errno = ECONNRESET;
        return -1;
    }

    //initialize a sockaddr_in for the peer
    struct sockaddr_in peer_addr;
    memset(&peer_addr, '0', sizeof(struct sockaddr_in));

    //Set the contents in the peer's sock_addr.
    //Make sure the contents will simulate a real client that connects with the intercepted server, as the server may depend on the contents to make further decisions.
    //The followings set-up should be fine with Nginx.
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    peer_addr.sin_port = htons(PREENY_SIN_PORT);

    //copy the initialized peer_addr back to the original sockaddr. Note the space for the original sockaddr, namely addr, has already been allocated
    if (addr) {
        memcpy(addr, &peer_addr, sizeof(struct sockaddr_in));
    }

    if (preeny_socket_threads_to_front[sockfd]) {
        preeny_desock_accepted_sock = dup(sockfd);
        return preeny_desock_accepted_sock;
    } else {
        return original_accept(sockfd, addr, addrlen);
    }
}

int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags) {
    return accept(sockfd, addr, addrlen);
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    if (preeny_socket_threads_to_front[sockfd]) {
        preeny_info("Emulating bind on port %d\n", ntohs(((struct sockaddr_in *)addr)->sin_port));
        return 0;
    }
    return original_bind(sockfd, addr, addrlen);
}

int listen(int sockfd, int backlog) {
    if (preeny_socket_threads_to_front[sockfd]) {
        return 0;
    }

    return original_listen(sockfd, backlog);
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    if (preeny_socket_threads_to_front[sockfd]) {
        return 0;
    }

    return original_connect(sockfd, addr, addrlen);
}

int close(int fd) {
    if (preeny_desock_accepted_sock != -1 && preeny_desock_accepted_sock == fd) {
        if (cfg_close_not_exit) {
            preeny_desock_accepted_sock = -1;
            return 0;
        } else {
            exit(0);
        }
    }

    return original_close(fd);
}

int shutdown(int sockfd, int how) {
    if (preeny_desock_accepted_sock != -1 && preeny_desock_accepted_sock == sockfd) {
        exit(0);
    }

    return original_shutdown(sockfd, how);
}

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    struct sockaddr_in target;
    socklen_t copylen = sizeof(target);

    if (!preeny_socket_threads_to_front[sockfd]) {
        return original_getsockname(sockfd, addr, addrlen);
    }

    if (!addr || !addrlen) {
        return -1;
    }

    if (*addrlen < sizeof(target)) {
        copylen = *addrlen;
    }

    target.sin_family = AF_INET;
    target.sin_addr.s_addr = htonl(INADDR_ANY);
    target.sin_port = htons(PREENY_SIN_PORT);

    memcpy(addr, &target, copylen);
    *addrlen = copylen;

    return 0;
}
