#include "server.hpp"

int main() {
    Server server(8080, 4, "kvstore.wal", 1, "node1:8080");
    server.add_cluster_node("node2:8081");
    server.add_cluster_node("node3:8082");
    server.run();
    return 0;
}