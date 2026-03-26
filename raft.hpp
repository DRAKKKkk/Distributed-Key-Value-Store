#pragma once
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

enum class RaftState { FOLLOWER, CANDIDATE, LEADER };

struct LogEntry {
    int term;
    std::string command;
};

class Raft {
public:
    Raft(int node_id);
    ~Raft();

    void add_peer(const std::string& peer_address);

    bool request_vote(int candidate_term, int candidate_id, int last_log_index, int last_log_term);
    bool append_entries(int leader_term, int leader_id, int prev_log_index, int prev_log_term, const std::vector<LogEntry>& entries, int leader_commit);

    RaftState get_state() const;
    int get_term() const;

private:
    int node_id_;
    int current_term_;
    int voted_for_;
    std::vector<LogEntry> log_;
    int commit_index_;
    int last_applied_;

    std::vector<std::string> peers_;
    int votes_received_;

    std::atomic<RaftState> state_;
    mutable std::mutex mutex_;

    std::thread background_thread_;
    std::atomic<bool> stop_thread_;
    std::chrono::time_point<std::chrono::steady_clock> last_heartbeat_;

    void run_background_loop();
    void start_election();
    void send_heartbeats();
    void reset_election_timer();
    void send_rpc_async(const std::string& peer, const std::string& message, bool is_vote);
};