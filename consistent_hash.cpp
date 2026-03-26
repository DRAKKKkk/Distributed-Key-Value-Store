#include "consistent_hash.hpp"
#include <functional>

ConsistentHash::ConsistentHash(int num_replicas) : num_replicas_(num_replicas) {}

size_t ConsistentHash::hash(const std::string& key) {
    return std::hash<std::string>{}(key);
}

void ConsistentHash::add_node(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < num_replicas_; ++i) {
        std::string vnode_key = node_id + "#" + std::to_string(i);
        size_t hash_val = hash(vnode_key);
        ring_[hash_val] = node_id;
    }
}

void ConsistentHash::remove_node(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < num_replicas_; ++i) {
        std::string vnode_key = node_id + "#" + std::to_string(i);
        size_t hash_val = hash(vnode_key);
        ring_.erase(hash_val);
    }
}

std::string ConsistentHash::get_node(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ring_.empty()) return "";

    size_t hash_val = hash(key);
    auto it = ring_.lower_bound(hash_val);

    if (it == ring_.end()) {
        return ring_.begin()->second;
    }
    return it->second;
}