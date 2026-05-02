// File: gateway/server.js

const express = require('express');
const cors = require('cors');
const http = require('http');
const net = require('net');
const { Server } = require('socket.io');

const app = express();
const server = http.createServer(app);
const io = new Server(server, {
  cors: { origin: "*", methods: ["GET", "POST"] }
});

app.use(cors());
app.use(express.json());

const CPP_HOST = '127.0.0.1';
const RAFT_PORTS = [8080, 8081, 8082]; // All the nodes in your cluster

// Helper function to send a command to a SPECIFIC port
function sendToNode(port, command) {
    return new Promise((resolve) => {
        const client = new net.Socket();
        
        client.connect(port, CPP_HOST, () => {
            client.write(command + '\n');
        });

        client.once('data', (data) => {
            client.destroy(); // Close the connection after getting the answer
            resolve(data.toString().trim());
        });

        client.on('error', (err) => {
            client.destroy();
            resolve('ERROR NODE_DEAD'); // If a node crashed, skip it
        });

        // 2-second timeout just in case
        setTimeout(() => {
            client.destroy();
            resolve('ERROR TIMEOUT');
        }, 2000);
    });
}

// The "Smart Router" that hunts for the Leader
async function sendCommandToCluster(command) {
    for (const port of RAFT_PORTS) {
        const response = await sendToNode(port, command);
        
        // If the node responds and DOES NOT say it's not the leader, we found our guy!
        if (!response.includes('NOT_LEADER') && !response.includes('NODE_DEAD') && !response.includes('TIMEOUT')) {
            console.log(`👑 Found Leader on port ${port}!`);
            return { leaderPort: port, response: response };
        }
    }
    throw new Error("Could not find the Raft Leader. Is the whole cluster down?");
}

// --- WEB API ROUTES FOR REACT ---

app.get('/', (req, res) => {
    res.send("Node.js Gateway is running!");
});

app.post('/api/command', async (req, res) => {
    const { action, key, value } = req.body;
    
    try {
        let commandString = "";
        if (action === "SET") {
            commandString = `SET ${key} ${value}`;
        } else if (action === "GET") {
            commandString = `GET ${key}`;
        } else {
            return res.status(400).json({ error: "Invalid action" });
        }

        console.log(`\n🔍 Routing command: ${commandString}`);
        
        // Let the smart router find the leader and send the command
        const result = await sendCommandToCluster(commandString);
        
        res.json({ success: true, processedBy: result.leaderPort, response: result.response });
    } catch (error) {
        console.error(error.message);
        res.status(500).json({ success: false, error: error.message });
    }
});

// --- START GATEWAY ---
const PORT = 5000;
server.listen(PORT, () => {
    console.log(`🚀 Smart Node.js Gateway listening on http://localhost:${PORT}`);
});