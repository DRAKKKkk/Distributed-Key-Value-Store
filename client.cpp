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

std::atomic<int> success_count(0);
std::atomic<int> redirect_count(0);
std::atomic<int> error_count(0);

void send_requests(int port, int thread_id, int num_requests) {
    for (int i = 0; i < num_requests; ++i) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            error_count++;
            continue;
        }

        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

        if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            error_count++;
            close(sock);
            continue;
        }

        std::string key = "user_" + std::to_string(thread_id) + "_" + std::to_string(i);
        std::string value = "active_" + std::to_string(i);
        std::string command = "SET " + key + " " + value + "\n";

        send(sock, command.c_str(), command.length(), 0);

        char buffer[256] = {0};
        int bytes_read = read(sock, buffer, 255);
        if (bytes_read > 0) {
            std::string response(buffer);
            if (response.find("OK") != std::string::npos) {
                success_count++;
            } else if (response.find("REDIRECT") != std::string::npos) {
                redirect_count++;
            } else {
                error_count++; // Captures "ERROR NOT_LEADER"
            }
        } else {
            error_count++;
        }
        close(sock);
    }
}

int main(int argc, char* argv[]) {
    int port = 8080;
    int num_threads = 10;
    int requests_per_thread = 100; // 1,000 total requests

    if (argc > 1) port = std::stoi(argv[1]);
    if (argc > 2) num_threads = std::stoi(argv[2]);
    if (argc > 3) requests_per_thread = std::stoi(argv[3]);

    std::cout << "Starting Stress Test on port " << port << "...\n";
    std::cout << "Threads: " << num_threads << " | Requests per thread: " << requests_per_thread << "\n";
    
    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(send_requests, port, i, requests_per_thread);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;

    std::cout << "\n--- Test Completed in " << elapsed.count() << " seconds ---\n";
    std::cout << "Success (OK):           " << success_count << "\n";
    std::cout << "Redirected (Hash Ring): " << redirect_count << "\n";
    std::cout << "Errors (Not Leader):    " << error_count << "\n";

    return 0;
}