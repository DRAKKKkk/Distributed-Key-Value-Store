#pragma once
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include "thread_pool.hpp"
#include "wal.hpp"
#include "raft.hpp"
#include "consistent_hash.hpp"

class Server {
public:
    Server(int port, size_t num_threads, const std::string& wal_file, int node_id);
    ~Server();
    void run();
    void add_cluster_node(const std::string& node_name);

private:
    int port_;
    int server_fd_;
    int epoll_fd_;
    std::unordered_map<std::string, std::string> store_;
    std::shared_mutex store_mutex_;
    ThreadPool thread_pool_;
    WAL wal_;
    Raft raft_;
    ConsistentHash ring_;

    void setup_server();
    void set_non_blocking(int fd);
    void handle_new_connection();
    void handle_client_data(int client_fd);
    void process_command(int client_fd, const std::string& command);
};