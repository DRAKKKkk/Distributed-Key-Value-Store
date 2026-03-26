void Server::process_command(int client_fd, const std::string& command) {
    // ... (Keep existing Raft internal command handling) ...

    if (op == "SET") {
        if (raft_.get_state() != RaftState::LEADER) {
            response = "ERROR NOT_LEADER\n";
        } else {
            iss >> value;
            // 1. Propose to Raft
            raft_.propose(command); 
            
            // 2. Write to WAL
            wal_.append(op, key, value);
            
            // 3. Update local store
            std::unique_lock<std::shared_mutex> lock(store_mutex_);
            store_[key] = value;
            response = "OK\n";
        }
    }
    // ... (Keep GET handling) ...
}