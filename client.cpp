#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <iomanip>

std::atomic<int> success_count(0);
std::atomic<int> error_count(0);
int cluster_ports[] = {8080, 8081, 8082};

void send_requests(int start_port, int thread_id, int num_requests, std::vector<double>& thread_latencies) {
    int current_port_idx = 0;
    for (int i = 0; i < 3; ++i) {
        if (cluster_ports[i] == start_port) {
            current_port_idx = i;
            break;
        }
    }

    for (int i = 0; i < num_requests; ++i) {
        bool success = false;
        int attempts = 0;
        
        auto req_start = std::chrono::high_resolution_clock::now();

        while (!success && attempts < 3) {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                attempts++;
                continue;
            }

            sockaddr_in serv_addr{};
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(cluster_ports[current_port_idx]);
            inet_pton(AF_INET, "0.0.0.0", &serv_addr.sin_addr);

            if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
                close(sock);
                current_port_idx = (current_port_idx + 1) % 3;
                attempts++;
                continue;
            }

            std::string key = "user_" + std::to_string(thread_id) + "_" + std::to_string(i);
            std::string value = "active_" + std::to_string(i);
            std::string command = "SET " + key + " " + value + "\n";

            send(sock, command.c_str(), command.length(), 0);

            char buffer[256] = {0};
            int bytes_read = read(sock, buffer, 255);
            close(sock);

            if (bytes_read > 0) {
                std::string response(buffer);
                if (response.find("OK") != std::string::npos) {
                    success_count++;
                    success = true;
                    
                    auto req_end = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double, std::milli> req_elapsed = req_end - req_start;
                    thread_latencies.push_back(req_elapsed.count());
                    
                } else {
                    current_port_idx = (current_port_idx + 1) % 3;
                    attempts++;
                }
            } else {
                current_port_idx = (current_port_idx + 1) % 3;
                    attempts++;
            }
        }
        if (!success) {
            error_count++;
        }
    }
}

int main(int argc, char* argv[]) {
    int port = 8080;
    int num_threads = 10;
    int requests_per_thread = 50;

    if (argc > 1) port = std::stoi(argv[1]);
    if (argc > 2) num_threads = std::stoi(argv[2]);
    if (argc > 3) requests_per_thread = std::stoi(argv[3]);

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    std::vector<std::vector<double>> all_latencies(num_threads); 

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(send_requests, port, i, requests_per_thread, std::ref(all_latencies[i]));
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;

    std::vector<double> merged_latencies;
    for (const auto& thread_lats : all_latencies) {
        merged_latencies.insert(merged_latencies.end(), thread_lats.begin(), thread_lats.end());
    }

    std::cout << "\n--- Test Completed in " << elapsed.count() << " seconds ---\n";
    std::cout << "Success (OK):        " << success_count << "\n";
    std::cout << "Errors (No Leader):  " << error_count << "\n";

    if (!merged_latencies.empty()) {
        std::sort(merged_latencies.begin(), merged_latencies.end());
        
        double sum = std::accumulate(merged_latencies.begin(), merged_latencies.end(), 0.0);
        double avg = sum / merged_latencies.size();
        
        double p50 = merged_latencies[merged_latencies.size() * 0.50];
        double p99 = merged_latencies[merged_latencies.size() * 0.99];

        std::cout << "\n--- Latency Metrics (ms) ---\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Average Latency: " << avg << " ms\n";
        std::cout << "p50 Latency:     " << p50 << " ms\n";
        std::cout << "p99 Latency:     " << p99 << " ms\n";
    }

    return 0;
}