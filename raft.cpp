#include "raft.hpp"
#include <iostream>
#include <random>
#include <sstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

Raft::Raft(int node_id) 
    : node_id_(node_id), current_term_(0), voted_for_(-1), 
      commit_index_(-1), last_applied_(-1), last_included_index_(-1), last_included_term_(0),
      votes_received_(0), state_(RaftState::FOLLOWER), stop_thread_(false) {
    reset_election_timer();
    background_thread_ = std::thread(&Raft::run_background_loop, this);
}

Raft::~Raft() {
    stop_thread_ = true;
    if (background_thread_.joinable()) {
        background_thread_.join();
    }
}

void Raft::add_peer(const std::string& peer_address) {
    std::lock_guard<std::mutex> lock(mutex_);
    peers_.push_back(peer_address);
    next_index_[peer_address] = 0;
    match_index_[peer_address] = -1;
}

void Raft::reset_election_timer() {
    last_heartbeat_ = std::chrono::steady_clock::now();
}

int Raft::get_actual_index(int log_index) const {
    return log_index - last_included_index_ - 1;
}

int Raft::get_log_term(int index) const {
    if (index == last_included_index_) return last_included_term_;
    int actual_idx = get_actual_index(index);
    if (actual_idx >= 0 && actual_idx < log_.size()) return log_[actual_idx].term;
    return 0;
}

void Raft::run_background_loop() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(300, 600);

    while (!stop_thread_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_).count();

        if (state_ == RaftState::LEADER) {
            if (elapsed >= 100) {
                send_heartbeats();
                reset_election_timer();
            }
        } else {
            if (elapsed >= dis(gen)) {
                start_election();
            }
        }
    }
}

void Raft::start_election() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = RaftState::CANDIDATE;
    current_term_++;
    voted_for_ = node_id_;
    votes_received_ = 1;
    reset_election_timer();
    
    if (peers_.empty()) {
        state_ = RaftState::LEADER;
        return;
    }

    int last_log_idx = last_included_index_ + log_.size();
    int last_log_term = get_log_term(last_log_idx);

    std::string message = "RAFT_VOTE " + std::to_string(current_term_) + " " + 
                          std::to_string(node_id_) + " " + std::to_string(last_log_idx) + " " + 
                          std::to_string(last_log_term) + "\n";
                          
    for (const auto& peer : peers_) {
        send_rpc_async(peer, message, true);
    }
}

void Raft::propose(const std::string& command) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != RaftState::LEADER) return;
    log_.push_back({current_term_, command});
}

void Raft::take_snapshot(int snapshot_index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_index <= last_included_index_) return;

    int actual_idx = get_actual_index(snapshot_index);
    if (actual_idx >= 0 && actual_idx < log_.size()) {
        last_included_term_ = log_[actual_idx].term;
        log_.erase(log_.begin(), log_.begin() + actual_idx + 1);
    }
    last_included_index_ = snapshot_index;
}

void Raft::send_heartbeats() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& peer : peers_) {
        int p_idx = next_index_[peer] - 1;
        
        if (p_idx < last_included_index_) {
            continue;
        }

        int p_term = get_log_term(p_idx);
        std::string entries_str = "";
        
        for (int i = std::max(p_idx + 1, last_included_index_ + 1); i <= last_included_index_ + log_.size(); ++i) {
            entries_str += "|" + log_[get_actual_index(i)].command;
        }

        std::string message = "RAFT_APPEND " + std::to_string(current_term_) + " " + 
                             std::to_string(node_id_) + " " + std::to_string(p_idx) + " " + 
                             std::to_string(p_term) + " " + std::to_string(commit_index_) + 
                             entries_str + "\n";
                             
        send_rpc_async(peer, message, false);
    }
}

void Raft::send_rpc_async(const std::string& peer, const std::string& message, bool is_vote) {
    std::thread([this, peer, message, is_vote]() {
        size_t colon = peer.find(':');
        if (colon == std::string::npos) return;
        
        std::string ip = "127.0.0.1";
        int port = std::stoi(peer.substr(colon + 1));
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return;

        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr);

        if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == 0) {
            send(sock, message.c_str(), message.length(), 0);
            char buffer[256] = {0};
            if (read(sock, buffer, 255) > 0) {
                std::istringstream iss(buffer);
                std::string type;
                iss >> type;

                if (type == "VOTE_ACK") {
                    int term, granted;
                    iss >> term >> granted;
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (term > current_term_) {
                        current_term_ = term;
                        state_ = RaftState::FOLLOWER;
                        voted_for_ = -1;
                    } else if (is_vote && granted) {
                        votes_received_++;
                        if (state_ == RaftState::CANDIDATE && votes_received_ > (peers_.size() + 1) / 2) {
                            state_ = RaftState::LEADER;
                            send_heartbeats();
                        }
                    }
                } else if (type == "APPEND_ACK") {
                    int term, success;
                    iss >> term >> success;
                    std::lock_guard<std::mutex> lock(mutex_);
                    
                    if (term > current_term_) {
                        current_term_ = term;
                        state_ = RaftState::FOLLOWER;
                        voted_for_ = -1;
                    } else if (success) {
                        match_index_[peer] = last_included_index_ + log_.size(); 
                        next_index_[peer] = match_index_[peer] + 1;

                        for (int N = last_included_index_ + log_.size(); N > commit_index_; --N) {
                            int count = 1;
                            for (const auto& p : peers_) {
                                if (match_index_[p] >= N) count++;
                            }
                            
                            if (count > (peers_.size() + 1) / 2 && get_log_term(N) == current_term_) {
                                commit_index_ = N;
                                break;
                            }
                        }
                    } else {
                        next_index_[peer] = std::max(0, next_index_[peer] - 1);
                    }
                }
            }
        }
        close(sock);
    }).detach();
}

bool Raft::request_vote(int candidate_term, int candidate_id, int last_log_index, int last_log_term) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (candidate_term < current_term_) return false;

    if (candidate_term > current_term_) {
        current_term_ = candidate_term;
        state_ = RaftState::FOLLOWER;
        voted_for_ = -1;
    }

    int my_last_idx = last_included_index_ + log_.size();
    int my_last_term = get_log_term(my_last_idx);

    bool log_ok = (candidate_term > my_last_term) || 
                  (candidate_term == my_last_term && last_log_index >= my_last_idx);

    if ((voted_for_ == -1 || voted_for_ == candidate_id) && log_ok) {
        voted_for_ = candidate_id;
        reset_election_timer();
        return true;
    }
    return false;
}

bool Raft::append_entries(int leader_term, int leader_id, int prev_log_index, int prev_log_term, const std::vector<LogEntry>& entries, int leader_commit) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (leader_term < current_term_) return false;

    current_term_ = leader_term;
    state_ = RaftState::FOLLOWER;
    reset_election_timer();

    if (prev_log_index < last_included_index_) return false;
    
    if (prev_log_index > last_included_index_ + log_.size()) return false;
    if (prev_log_index > last_included_index_ && get_log_term(prev_log_index) != prev_log_term) return false;

    int actual_prev = get_actual_index(prev_log_index);
    if (actual_prev >= 0) {
        log_.erase(log_.begin() + actual_prev + 1, log_.end());
    }
    
    for (const auto& entry : entries) {
        log_.push_back(entry);
    }

    if (leader_commit > commit_index_) {
        commit_index_ = std::min(leader_commit, last_included_index_ + (int)log_.size());
    }

    return true;
}

RaftState Raft::get_state() const { std::lock_guard<std::mutex> lock(mutex_); return state_; }
int Raft::get_term() const { std::lock_guard<std::mutex> lock(mutex_); return current_term_; }
int Raft::get_commit_index() const { std::lock_guard<std::mutex> lock(mutex_); return commit_index_; }
int Raft::get_log_size() const { std::lock_guard<std::mutex> lock(mutex_); return last_included_index_ + log_.size() + 1; }