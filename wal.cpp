#include "wal.hpp"
#include <iostream>
#include <sstream>

WAL::WAL(const std::string& filename) : filename_(filename) {
    file_.open(filename_, std::ios::app | std::ios::out);
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
    std::ifstream infile(filename_);
    if (!infile.is_open()) return;

    std::string line;
    while (std::getline(infile, line)) {
        std::istringstream iss(line);
        std::string op, key, value;
        iss >> op >> key;
        if (op == "SET") {
            iss >> value;
            store[key] = value;
        }
    }
}