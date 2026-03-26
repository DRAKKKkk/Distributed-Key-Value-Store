#include "wal.hpp"
#include <sstream>

WAL::WAL(const std::string& filename) : filename_(filename) {
    file_.open(filename_, std::ios::app);
}

WAL::~WAL() {
    if (file_.is_open()) {
        file_.close();
    }
}

void WAL::append(const std::string& op, const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_ << op << " " << key << " " << value << "\n";
        file_.flush();
    }
}

void WAL::recover(std::unordered_map<std::string, std::string>& store) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream infile(filename_);
    std::string line, op, key, value;

    while (std::getline(infile, line)) {
        std::istringstream iss(line);
        if (iss >> op >> key) {
            if (op == "SET") {
                iss >> value;
                store[key] = value;
            }
        }
    }
}

void WAL::truncate() {
    std::lock_guard<std::mutex> lock(mutex_);
    file_.close();
    file_.open(filename_, std::ios::trunc | std::ios::out);
    file_.close();
    file_.open(filename_, std::ios::app);
}