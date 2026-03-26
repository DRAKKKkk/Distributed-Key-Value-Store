#!/bin/bash

# 1. Setup a cleanup trap so we don't leave zombie processes running
cleanup() {
    echo -e "\n🧹 Cleaning up cluster processes..."
    kill $NODE1_PID $NODE2_PID $NODE3_PID 2>/dev/null
    exit
}
trap cleanup EXIT INT TERM

echo "🚀 Starting 3-Node Raft Cluster..."
# Start nodes in the background and redirect their spammy output to log files
./kvstore 8080 1 > node1.log 2>&1 &
NODE1_PID=$!

./kvstore 8081 2 > node2.log 2>&1 &
NODE2_PID=$!

./kvstore 8082 3 > node3.log 2>&1 &
NODE3_PID=$!

echo "⏳ Waiting 3 seconds for the cluster to elect a LEADER..."
sleep 3

echo -e "\n============================================="
echo "🧪 TEST 1: Baseline Load Test (All Nodes Alive)"
echo "============================================="
# Hitting Node 1. If it's not the leader, it should return REDIRECT or ERROR NOT_LEADER
./client 8080 10 50

echo -e "\n💀 INITIATING CHAOS: Killing Node 1 (PID: $NODE1_PID)..."
kill $NODE1_PID
echo "⏳ Waiting 3 seconds for the remaining 2 nodes to detect the timeout and elect a NEW leader..."
sleep 3

echo -e "\n============================================="
echo "🧪 TEST 2: Fault Tolerance Test (Node 1 is DEAD)"
echo "============================================="
# We now hit Node 2. The cluster only has 2 nodes left, but that is still a majority (2/3). 
# It should successfully process requests!
./client 8081 10 50

echo -e "\n🧟 REVIVING Node 1..."
./kvstore 8080 1 >> node1.log 2>&1 &
NODE1_PID=$!
echo "⏳ Waiting 2 seconds for Node 1 to sync its log from the new Leader..."
sleep 2

echo -e "\n============================================="
echo "🧪 TEST 3: Recovery Test (Node 1 rejoined)"
echo "============================================="
./client 8082 10 50

echo -e "\n✅ Chaos Test Complete! Check node1.log, node2.log, and node3.log for the detailed Raft chatter."