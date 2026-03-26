#include "raft.hpp"
#include <iostream>
#include <random>
#include <sstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

Raft::Raft(int node_id) 
    : node_id_(node_id), current_term_(0), voted_for_(-1), 
      commit_index_(0), last_applied_(0), votes_received_(0), 
      state_(RaftState::FOLLOWER), stop_thread_(false) {
    
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
}

void Raft::reset_election_timer() {
    last_heartbeat_ = std::chrono::steady_clock::now();
}

void Raft::run_background_loop() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(300, 600); // 300ms to 600ms timeout

    while (!stop_thread_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_).count();

        if (state_ == RaftState::LEADER) {
            if (elapsed >= 100) { // Send heartbeat every 100ms
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
    votes_received_ = 1; // Vote for self
    reset_election_timer();
    
    std::cout << "[Raft] Node " << node_id_ << " starting election for term " << current_term_ << "\n";

    if (peers_.empty()) {
        state_ = RaftState::LEADER; // Single node cluster auto-wins
        std::cout << "[Raft] Node " << node_id_ << " became LEADER\n";
        return;
    }

    std::string message = "RAFT_VOTE " + std::to_string(current_term_) + " " + std::to_string(node_id_) + " 0 0\n";
    for (const auto& peer : peers_) {
        send_rpc_async(peer, message, true);
    }
}

void Raft::send_heartbeats() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string message = "RAFT_APPEND " + std::to_string(current_term_) + " " + std::to_string(node_id_) + " 0 0 " + std::to_string(commit_index_) + "\n";
    for (const auto& peer : peers_) {
        send_rpc_async(peer, message, false);
    }
}

void Raft::send_rpc_async(const std::string& peer, const std::string& message, bool is_vote) {
    std::thread([this, peer, message, is_vote]() {
        size_t colon = peer.find(':');
        if (colon == std::string::npos) return;
        
        std::string ip = "127.0.0.1"; // Assuming localhost for testing, normally parse `peer.substr(0, colon)`
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
                int term, granted;
                iss >> type >> term >> granted;

                std::lock_guard<std::mutex> lock(mutex_);
                if (term > current_term_) {
                    current_term_ = term;
                    state_ = RaftState::FOLLOWER;
                    voted_for_ = -1;
                } else if (is_vote && type == "VOTE_ACK" && granted) {
                    votes_received_++;
                    if (state_ == RaftState::CANDIDATE && votes_received_ > (peers_.size() + 1) / 2) {
                        state_ = RaftState::LEADER;
                        std::cout << "[Raft] Node " << node_id_ << " became LEADER\n";
                        send_heartbeats();
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

    if (voted_for_ == -1 || voted_for_ == candidate_id) {
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
    return true;
}

RaftState Raft::get_state() const { std::lock_guard<std::mutex> lock(mutex_); return state_; }
int Raft::get_term() const { std::lock_guard<std::mutex> lock(mutex_); return current_term_; }