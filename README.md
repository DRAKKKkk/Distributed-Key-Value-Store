# Distributed Key-Value Store (Raft Consensus)

A high-performance, fault-tolerant Distributed Key-Value Store built from scratch in modern C++. This project implements the **Raft Consensus Algorithm** to ensure high availability and strong data consistency across a cluster of nodes.

## 🚀 Features

* **Raft Consensus Engine:** Custom implementation of leader election, log replication, and quorum-based commits.
* **Fault Tolerance:** Survives node crashes and network partitions. Automatically handles leader re-election and log synchronization upon node recovery.
* **Write-Ahead Logging (WAL):** Ensures durability by logging operations to disk before applying them to the state machine, allowing for full recovery after total cluster failure.
* **Log Snapshotting:** Prevents infinite memory growth by periodically compressing the Raft log and WAL into snapshots.
* **Consistent Hashing:** Evenly distributes the data workload across the cluster using a hash ring.
* **Multithreaded Event Loop:** Uses `epoll` and a custom thread pool for high-throughput, non-blocking network I/O.
* **Load Testing Client:** Includes a multithreaded C++ client capable of spamming the cluster with thousands of requests to verify stability and quorum safety.

## 🛠️ Architecture

1. **Client Request:** A client sends a `SET` or `GET` request to any node.
2. **Hash Ring Routing:** If the key belongs to a different node, the server returns a `REDIRECT`.
3. **Leader Proposal:** The Leader appends the command to its Raft log and broadcasts `RAFT_APPEND` RPCs to Follower nodes.
4. **Majority Consensus:** Once a majority of nodes acknowledge the log, the Leader commits it, writes to the WAL, updates the local Hash Map, and responds `OK` to the client.

## 💻 Tech Stack
* **Language:** C++17
* **Networking:** TCP/IP Sockets, `epoll` (Linux)
* **Build System:** CMake / Make
* **Concurrency:** `std::thread`, `std::mutex`, `std::shared_mutex`, `std::condition_variable`

## 🏃‍♂️ How to Run

### 1. Build the Project
```bash
mkdir build && cd build
cmake ..
make
