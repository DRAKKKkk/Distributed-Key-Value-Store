#include "server.hpp"
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <cstring>
#include <sstream>

#define MAX_EVENTS 10
#define BUFFER_SIZE 1024

Server::Server(int port) : port_(port), server_fd_(-1), epoll_fd_(-1) {
    setup_server();
}

Server::~Server() {
    if (server_fd_ != -1) close(server_fd_);
    if (epoll_fd_ != -1) close(epoll_fd_);
}

void Server::set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void Server::setup_server() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    set_non_blocking(server_fd_);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    bind(server_fd_, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd_, SOMAXCONN);

    epoll_fd_ = epoll_create1(0);
    epoll_event event{};
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = server_fd_;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_fd_, &event);
}

void Server::run() {
    epoll_event events[MAX_EVENTS];
    while (true) {
        int num_events = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
        for (int i = 0; i < num_events; ++i) {
            if (events[i].data.fd == server_fd_) {
                handle_new_connection();
            } else {
                handle_client_data(events[i].data.fd);
            }
        }
    }
}

void Server::handle_new_connection() {
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    
    while (true) {
        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            break;
        }
        
        set_non_blocking(client_fd);
        epoll_event event{};
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = client_fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &event);
    }
}

void Server::handle_client_data(int client_fd) {
    char buffer[BUFFER_SIZE];
    
    while (true) {
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);

        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            process_command(client_fd, std::string(buffer));
        } else if (bytes_read == -1 && errno == EAGAIN) {
            break;
        } else {
            close(client_fd);
            break;
        }
    }
}

void Server::process_command(int client_fd, const std::string& command) {
    std::istringstream iss(command);
    std::string op, key, value, response;
    iss >> op >> key;

    if (op == "SET") {
        iss >> value;
        store_[key] = value;
        response = "OK\n";
    } else if (op == "GET") {
        if (store_.find(key) != store_.end()) {
            response = store_[key] + "\n";
        } else {
            response = "(nil)\n";
        }
    } else {
        response = "ERROR\n";
    }

    write(client_fd, response.c_str(), response.length());
}