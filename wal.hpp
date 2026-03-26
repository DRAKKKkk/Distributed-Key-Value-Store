#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <unordered_map>

class WAL {
public:
    WAL(const std::string& filename);
    ~WAL();
    void append(const std::string& op, const std::string& key, const std::string& value);
    void recover(std::unordered_map<std::string, std::string>& store);

private:
    std::string filename_;
    std::ofstream file_;
    std::mutex mutex_;
};