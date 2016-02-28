#include <iostream>
#include <string>
#include <memory>

#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>

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

    auto deleter_sock = [](int* sock) {
        close(*sock);
        delete sock;
    };

    std::unique_ptr<int, decltype(deleter_sock)> cur_sock(
            static_cast<int*>(param), deleter_sock);

    std::string req;
    req.reserve(4096);
    while (true) {

        char buf[4096] = {0};
        ssize_t received = recv(*cur_sock, buf, 4095, MSG_NOSIGNAL);
        if (received <= 0) {
            break;
        }
        req += buf;

    }

    const std::string log_path = "/home/box/final_log";
    auto deleter_file = [](FILE* f) {
        fclose(f);
    };

    pthread_t self_id= pthread_self();
    std::unique_ptr<FILE, decltype(deleter_file)> log(
            fopen((log_path + std::to_string(self_id)).c_str(), "w"), deleter_file);

    if (req.empty()) {
        fprintf(log.get(), "%s\r\n", "Empty request");
        return 0;
    }

    fprintf(log.get(), "%s%s\r\n", "Request:\r\n", req.c_str());

    char cmd[4] = {0};
    strncpy(cmd, req.c_str(), 3);

    if (strncmp(cmd, "GET", 3) != 0) {
        fprintf(log.get(), "%s%s\r\n", "Invalid cmd: ", cmd);
        return 0;
    }

    size_t index = 3;
    while (index < req.length() && req[index] == ' ') ++index;

    std::string file_name;
    file_name.reserve(256);
    for (size_t i = index; i < req.length() && req[i] != ' '; ++i) {
        file_name += req[i];
    }

    if (file_name.empty()) {
        fprintf(log.get(), "%s\r\n", "Empty file_name");
        return 0;
    } else {
        fprintf(log.get(), "%s%s\r\n", "file_name: ", file_name.c_str());
        file_name = file_name.substr(1, file_name.length() - 1);
    }


    std::string resp;
    resp.reserve(4096);
    struct stat data_stat;
    if ((stat(file_name.c_str(), &data_stat) == -1) || S_ISDIR(data_stat.st_mode)) {

        const char not_found[] = "HTTP/1.0 404 Not Found\r\n\r\n";
        resp = not_found;

    } else {

        std::unique_ptr<FILE, decltype(deleter_file)> data_file(
                fopen(file_name.c_str(), "r"), deleter_file);

        if (data_file.get() != nullptr) {
            const char founded[] = "HTTP/1.0 200 OK\r\n\r\n";


            fseek(data_file.get(), 0, SEEK_END);
            long file_size = ftell(data_file.get());
            rewind(data_file.get());

            std::unique_ptr<char[]> data(new char[file_size + 1]);
            fread(data.get(), 1, file_size, data_file.get());
            data[file_size] = 0;

            resp = founded;
            resp += data.get();
        }

    }

    send(*cur_sock, resp.c_str(), resp.length(), MSG_NOSIGNAL);

    return 0;
}

void demonize() {

    umask(0);

    if (fork() != 0) {
        exit(0);
    }

    setsid();

    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGHUP, &sa, nullptr);

    if (fork() != 0) {
        exit(0);
    }
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
                    pthread_create(&t, nullptr, worker, new int(cur_sock));
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
    bool demon = true;
    while ((opt = getopt(argc, argv, "h:p:d:t")) != -1) {
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

            case 't':
                demon = false;
                break;

            default:
                std::cout << "Error getopt" << std::endl;
                return 1;
        }
    }

    if (demon) {
        demonize();
    }

    chdir(dir.c_str());
    int srvr = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(std::atoi(port.c_str())));
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