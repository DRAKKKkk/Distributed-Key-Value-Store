#include "server.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <cstring>
#include <sstream>
#include <iostream>

#define MAX_EVENTS 10
#define BUFFER_SIZE 1024

Server::Server(int port, size_t num_threads, const std::string& wal_file, int node_id, const std::string& node_name) 
    : port_(port), node_name_(node_name), server_fd_(-1), epoll_fd_(-1), thread_pool_(num_threads), wal_(wal_file), raft_(node_id), ring_(3) {
    wal_.recover(store_);
    setup_server();
}

Server::~Server() {
    if (server_fd_ != -1) close(server_fd_);
    if (epoll_fd_ != -1) close(epoll_fd_);
}

void Server::add_cluster_node(const std::string& node_name) {}

Raft& Server::get_raft() {
    return raft_;
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
                int client_fd = events[i].data.fd;
                thread_pool_.enqueue([this, client_fd]() {
                    this->handle_client_data(client_fd);
                });
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
        event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
        event.data.fd = client_fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &event);
    }
}

void Server::handle_client_data(int client_fd) {
    char buffer[BUFFER_SIZE];
    std::string command;
    bool connection_closed = false;

    while (true) {
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);

        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            command += buffer;
        } else if (bytes_read == -1 && errno == EAGAIN) {
            break;
        } else {
            connection_closed = true;
            break;
        }
    }

    if (connection_closed) {
        close(client_fd);
        return;
    }

    if (!command.empty()) {
        process_command(client_fd, command);
    }

    epoll_event event{};
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    event.data.fd = client_fd;
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd, &event);
}

void Server::process_command(int client_fd, const std::string& raw_command) {
    std::string command = raw_command;
    bool is_http = false;

    // --- HTTP INTERCEPTOR & CORS HANDLER ---
    if (command.find("HTTP/") != std::string::npos) {
        is_http = true;
        std::istringstream stream(command);
        std::string method, path;
        stream >> method >> path;

        // 1. Handle CORS Preflight from React
        if (method == "OPTIONS") {
    std::string cors_response = 
        "HTTP/1.1 200 OK\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, ngrok-skip-browser-warning\r\n"
        "Access-Control-Max-Age: 86400\r\n" // Cache preflight for 24 hours
        "Connection: close\r\n"
        "Content-Length: 0\r\n\r\n";
    write(client_fd, cors_response.c_str(), cors_response.length());
    return;
}
        
        // 2. Translate URL paths to database commands (e.g., "/SET/mykey/myvalue" -> "SET mykey myvalue")
        if (path.length() > 1 && path != "/favicon.ico") {
            command = path.substr(1); 
            for (char& c : command) {
                if (c == '/') c = ' ';
            }
        } else {
            // Read from body if path is just "/"
            size_t body_pos = raw_command.find("\r\n\r\n");
            if (body_pos != std::string::npos) {
                command = raw_command.substr(body_pos + 4);
            }
        }
    }

    std::istringstream iss(command);
    std::string op, key, value, json_response, raw_response;
    iss >> op;

    // --- RAFT CLUSTER TRAFFIC (Ignored by HTTP) ---
    if (op == "RAFT_VOTE") {
        int c_term, c_id, c_log_idx, c_log_term;
        iss >> c_term >> c_id >> c_log_idx >> c_log_term;
        bool granted = raft_.request_vote(c_term, c_id, c_log_idx, c_log_term);
        std::string response = "VOTE_ACK " + std::to_string(raft_.get_term()) + " " + (granted ? "1\n" : "0\n");
        write(client_fd, response.c_str(), response.length());
        return;
    } 
    if (op == "RAFT_APPEND") {
        int l_term, l_id, p_log_idx, p_log_term, l_commit;
        iss >> l_term >> l_id >> p_log_idx >> p_log_term >> l_commit;
        
        std::vector<LogEntry> entries;
        std::string entry_str;
        while (std::getline(iss, entry_str, '|')) {
            if (!entry_str.empty() && entry_str != " " && entry_str != "\n") {
                entries.push_back({l_term, entry_str});
            }
        }

        bool success = raft_.append_entries(l_term, l_id, p_log_idx, p_log_term, entries, l_commit);
        
        if (success && raft_.get_log_size() > 10000) {
            raft_.take_snapshot(raft_.get_commit_index());
            wal_.truncate();
        }

        std::string response = "APPEND_ACK " + std::to_string(raft_.get_term()) + " " + (success ? "1\n" : "0\n");
        write(client_fd, response.c_str(), response.length());
        return;
    }

    // --- CLIENT DATABASE TRAFFIC ---
    iss >> key;
    
    if (op == "SET") {
        int target_idx = raft_.propose(command);
        if (target_idx == -1) {
            json_response = "{\"status\": \"error\", \"message\": \"NOT_LEADER\"}";
            raw_response = "ERROR NOT_LEADER";
        } else {
            iss >> value;
            int wait_ms = 0;
            // Increased timeout to account for heavy thread pool queuing
            while (raft_.get_commit_index() < target_idx && raft_.get_state() == RaftState::LEADER && wait_ms < 200) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                wait_ms++;
            }

            if (raft_.get_commit_index() >= target_idx) {
                wal_.append(op, key, value);
                std::unique_lock<std::shared_mutex> lock(store_mutex_);
                store_[key] = value;
                
                if (raft_.get_log_size() > 10000) {
                    raft_.take_snapshot(raft_.get_commit_index());
                    wal_.truncate();
                }

                json_response = "{\"status\": \"success\", \"message\": \"OK\"}";
                raw_response = "OK";
            } else {
                json_response = "{\"status\": \"error\", \"message\": \"NO_QUORUM\"}";
                raw_response = "ERROR NO_QUORUM";
            }
        }
    } else if (op == "GET") {
        std::shared_lock<std::shared_mutex> lock(store_mutex_);
        if (store_.find(key) != store_.end()) {
            json_response = "{\"status\": \"success\", \"value\": \"" + store_[key] + "\"}";
            raw_response = store_[key];
        } else {
            json_response = "{\"status\": \"error\", \"message\": \"(nil)\"}";
            raw_response = "(nil)";
        }
    } else {
        json_response = "{\"status\": \"error\", \"message\": \"INVALID_COMMAND\"}";
        raw_response = "ERROR";
    }

    // --- RESPONSE ROUTER ---
    if (is_http) {
        // Send a beautifully formatted Web JSON response
        std::string http_response = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n"
            "Content-Length: " + std::to_string(json_response.length()) + "\r\n"
            "\r\n" + 
            json_response;
        write(client_fd, http_response.c_str(), http_response.length());
    } else {
        // Fallback for internal C++ tools testing via raw TCP
        raw_response += "\n";
        write(client_fd, raw_response.c_str(), raw_response.length());
    }
}