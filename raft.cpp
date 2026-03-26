#include "raft.hpp"
#include <iostream>
#include <random>
#include <sstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

// ... (Constructor/Destructor remain same as previous) ...

void Raft::propose(const std::string& command) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != RaftState::LEADER) return;

    log_.push_back({current_term_, command});
    std::cout << "[Raft] Node " << node_id_ << " appended log: " << command << " at index " << log_.size() - 1 << "\n";
}

void Raft::send_heartbeats() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& peer : peers_) {
        int p_idx = next_index_[peer] - 1;
        int p_term = (p_idx >= 0) ? log_[p_idx].term : 0;
        
        std::string entries_str = "";
        for (size_t i = next_index_[peer]; i < log_.size(); ++i) {
            entries_str += "|" + log_[i].command; // Simple serialization
        }

        std::string message = "RAFT_APPEND " + std::to_string(current_term_) + " " + 
                             std::to_string(node_id_) + " " + std::to_string(p_idx) + " " + 
                             std::to_string(p_term) + " " + std::to_string(commit_index_) + 
                             entries_str + "\n";
                             
        send_rpc_async(peer, message, false);
    }
}

bool Raft::append_entries(int leader_term, int leader_id, int prev_log_index, int prev_log_term, const std::vector<LogEntry>& entries, int leader_commit) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (leader_term < current_term_) return false;

    current_term_ = leader_term;
    state_ = RaftState::FOLLOWER;
    reset_election_timer();

    // Log matching logic
    if (prev_log_index >= 0 && (prev_log_index >= (int)log_.size() || log_[prev_log_index].term != prev_log_term)) {
        return false;
    }

    // Append new entries
    for (const auto& entry : entries) {
        log_.push_back(entry);
    }

    if (leader_commit > commit_index_) {
        commit_index_ = std::min(leader_commit, (int)log_.size() - 1);
    }

    return true;
}