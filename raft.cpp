#include "raft.hpp"
#include <iostream>
#include <random>

Raft::Raft(int node_id) 
    : node_id_(node_id), current_term_(0), voted_for_(-1), 
      commit_index_(0), last_applied_(0), state_(RaftState::FOLLOWER), stop_thread_(false) {
    
    reset_election_timer();
    background_thread_ = std::thread(&Raft::run_background_loop, this);
}

Raft::~Raft() {
    stop_thread_ = true;
    if (background_thread_.joinable()) {
        background_thread_.join();
    }
}

void Raft::reset_election_timer() {
    last_heartbeat_ = std::chrono::steady_clock::now();
}

void Raft::run_background_loop() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(150, 300);

    while (!stop_thread_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_).count();

        if (state_ == RaftState::LEADER) {
            if (elapsed >= 50) {
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
    reset_election_timer();
    
    std::cout << "Node " << node_id_ << " term " << current_term_ << "\n";

    state_ = RaftState::LEADER;
    std::cout << "Node " << node_id_ << " LEADER term " << current_term_ << "\n";
}

void Raft::send_heartbeats() {
}

bool Raft::request_vote(int candidate_term, int candidate_id, int last_log_index, int last_log_term) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (candidate_term < current_term_) {
        return false;
    }

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

    if (leader_term < current_term_) {
        return false;
    }

    current_term_ = leader_term;
    state_ = RaftState::FOLLOWER;
    reset_election_timer();
    
    return true;
}

RaftState Raft::get_state() const { return state_; }
int Raft::get_term() const { return current_term_; }