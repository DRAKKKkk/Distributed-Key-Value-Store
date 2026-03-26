#pragma once
#include <string>
#include <map>
#include <vector>
#include <mutex>

class ConsistentHash {
public:
    ConsistentHash(int num_replicas = 3);
    void add_node(const std::string& node_id);
    void remove_node(const std::string& node_id);
    std::string get_node(const std::string& key);

private:
    int num_replicas_;
    std::map<size_t, std::string> ring_;
    std::mutex mutex_;
    size_t hash(const std::string& key);
};