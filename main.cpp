#include <iostream>
#include <string>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

int set_nonblock(int fd) {

#if defined(O_NONBLOCK)
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        flags = 0;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    return ioctl(fd, FIOBIO, 1);
#endif

}

void* worker(void* param) {

    intptr_t cur_sock = reinterpret_cast<intptr_t>(param);
    std::string req;
    req.reserve(4096);
    while (true) {

        char buf[4096] = {0};
        ssize_t received = recv(cur_sock, buf, 4095, MSG_NOSIGNAL);
        if (received <= 0) {
            break;
        }
        req += buf;

    }

    send(cur_sock, req.c_str(), req.length(), MSG_NOSIGNAL);
    close(cur_sock);

    return 0;
}

int run(int srvr) {

    int EPoll = epoll_create1(0);
    if (EPoll == -1) {
        std::cout << "Error epoll_create: " << strerror(errno) << std::endl;
        return 2;
    }

    epoll_event Event;
    Event.data.fd = srvr;
    Event.events = EPOLLIN;
    if (epoll_ctl(EPoll, EPOLL_CTL_ADD, srvr, &Event) == -1) {
        std::cout << "Error epoll_ctl: " << strerror(errno) << std::endl;
        return 3;
    }

    const int max_events = 32;
    while (true) {

        epoll_event Events[max_events];
        int ev_count = epoll_wait(EPoll, Events, max_events, -1);
        if (ev_count == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                std::cout << "Error epoll_wait: " << strerror(errno) << std::endl;
                return 4;
            }
        }
        for (int index = 0; index < ev_count; ++index) {

            int cur_sock = Events[index].data.fd;
            if (cur_sock == srvr) {

                int cli = accept(srvr, nullptr, nullptr);
                if (cli == -1) {
                    std::cout << "Error accept: " << strerror(errno) << std::endl;
                    return 5;
                }
                set_nonblock(cli);
                Event.data.fd = cli;
                Event.events = EPOLLIN | EPOLLET;
                epoll_ctl(EPoll, EPOLL_CTL_ADD, cli, &Event);

            } else {

                if (Events[index].events & EPOLLIN){

                    pthread_t t;
                    pthread_create(&t, nullptr, worker,
                                   reinterpret_cast<void*>(cur_sock));
                    pthread_detach(t);

                } else {
                    shutdown(cur_sock, SHUT_RDWR);
                    close(cur_sock);
                }

            }

        }

    }

}

int main(int argc, char** argv) {

    std::string ip;
    std::string port;
    std::string dir;
    int opt = -1;
    while ((opt = getopt(argc, argv, "h:p:d:")) != -1) {
        switch(opt) {
            case 'h':
                ip = optarg;
                break;

            case 'p':
                port = optarg;
                break;

            case 'd':
                dir = optarg;
                break;

            default:
                std::cout << "Error getopt" << std::endl;
                return 1;
        }
    }

    chdir(dir.c_str());
    int srvr = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(atoi(port.c_str()));
    if (inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) != 1) {
        std::cout << "Error inet_pton: " << strerror(errno) << std::endl;
        return 1;
    }

    if (bind(srvr, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == -1) {
        std::cout << "Error bind: " << strerror(errno) << std::endl;
        return 1;
    }
    set_nonblock(srvr);
    if (listen(srvr, SOMAXCONN) == -1) {
        std::cout << "Error listen: " << strerror(errno) << std::endl;
        return 1;
    }

    return run(srvr);
}