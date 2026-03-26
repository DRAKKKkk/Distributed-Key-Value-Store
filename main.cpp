#include "server.hpp"

int main(int argc, char* argv[]) {
    int port = 8080;
    int node_id = 1;
    if (argc > 1) port = std::stoi(argv[1]);
    if (argc > 2) node_id = std::stoi(argv[2]);

    std::string node_name = "127.0.0.1:" + std::to_string(port);
    std::string wal_file = "kvstore_" + std::to_string(port) + ".wal";

    Server server(port, 4, wal_file, node_id, node_name);
    
    server.add_cluster_node("127.0.0.1:8080");
    server.add_cluster_node("127.0.0.1:8081");
    server.add_cluster_node("127.0.0.1:8082");

    if (port != 8080) server.get_raft().add_peer("127.0.0.1:8080");
    if (port != 8081) server.get_raft().add_peer("127.0.0.1:8081");
    if (port != 8082) server.get_raft().add_peer("127.0.0.1:8082");

    server.run();
    return 0;
}