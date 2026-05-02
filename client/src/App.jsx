import { useState } from 'react';

function App() {
  const [logs, setLogs] = useState(["System Initialized. Ready for commands."]);
  const [leaderPort, setLeaderPort] = useState(null);
  
  // Form States
  const [setKey, setSetKey] = useState('');
  const [setVal, setSetVal] = useState('');
  const [getKey, setGetKey] = useState('');

  const addLog = (message) => {
    // Add new logs to the top of the list
    setLogs((prev) => [`[${new Date().toLocaleTimeString()}] ${message}`, ...prev]);
  };

  const sendCommand = async (action, key, value = "") => {
    if (!key) return;
    
    addLog(`> ${action} ${key} ${value}`);
    
    try {
      const response = await fetch("http://localhost:5000/api/command", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ action, key, value })
      });
      
      const data = await response.json();
      
      if (data.success) {
        setLeaderPort(data.processedBy);
        addLog(`✅ Node ${data.processedBy} replied: ${data.response}`);
      } else {
        addLog(`❌ Error: ${data.error}`);
      }
    } catch (error) {
      addLog(`🚨 Network Error: Is the Node.js Gateway running?`);
    }
  };

  const handleSet = (e) => {
    e.preventDefault();
    sendCommand("SET", setKey, setVal);
    setSetKey('');
    setSetVal('');
  };

  const handleGet = (e) => {
    e.preventDefault();
    sendCommand("GET", getKey);
    setGetKey('');
  };

  // Helper to draw the visual nodes
  const renderNode = (port) => {
    const isLeader = leaderPort === port;
    return (
      <div key={port} style={{
        padding: '20px',
        borderRadius: '10px',
        backgroundColor: isLeader ? '#2ecc71' : '#34495e',
        color: 'white',
        textAlign: 'center',
        width: '120px',
        border: isLeader ? '4px solid #27ae60' : '4px solid transparent',
        transition: 'all 0.3s ease'
      }}>
        <h3 style={{ margin: 0 }}>Node {port - 8079}</h3>
        <p style={{ margin: '5px 0 0 0', fontSize: '12px', opacity: 0.8 }}>Port {port}</p>
        <div style={{ marginTop: '10px', fontSize: '12px', fontWeight: 'bold' }}>
          {isLeader ? '👑 LEADER' : 'FOLLOWER'}
        </div>
      </div>
    );
  };

  return (
    <div style={{ fontFamily: 'system-ui, sans-serif', maxWidth: '900px', margin: '40px auto', padding: '20px' }}>
      <h1 style={{ textAlign: 'center', color: '#2c3e50', marginBottom: '40px' }}>Raft Distributed Database</h1>

      {/* CLUSTER VISUALIZER */}
      <div style={{ display: 'flex', justifyContent: 'center', gap: '30px', marginBottom: '40px' }}>
        {[8080, 8081, 8082].map(renderNode)}
      </div>

      <div style={{ display: 'flex', gap: '20px' }}>
        {/* LEFT COLUMN: CONTROLS */}
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: '20px' }}>
          
          {/* SET COMMAND FORM */}
          <form onSubmit={handleSet} style={{ padding: '20px', backgroundColor: '#ecf0f1', borderRadius: '10px' }}>
            <h3 style={{ marginTop: 0, color: '#2c3e50' }}>Write Data (SET)</h3>
            <input 
              type="text" placeholder="Key" value={setKey} onChange={e => setSetKey(e.target.value)} required
              style={{ width: '100%', padding: '10px', marginBottom: '10px', boxSizing: 'border-box' }} 
            />
            <input 
              type="text" placeholder="Value" value={setVal} onChange={e => setSetVal(e.target.value)} required
              style={{ width: '100%', padding: '10px', marginBottom: '10px', boxSizing: 'border-box' }} 
            />
            <button type="submit" style={{ width: '100%', padding: '10px', backgroundColor: '#3498db', color: 'white', border: 'none', cursor: 'pointer', fontWeight: 'bold' }}>
              Send SET Command
            </button>
          </form>

          {/* GET COMMAND FORM */}
          <form onSubmit={handleGet} style={{ padding: '20px', backgroundColor: '#ecf0f1', borderRadius: '10px' }}>
            <h3 style={{ marginTop: 0, color: '#2c3e50' }}>Read Data (GET)</h3>
            <input 
              type="text" placeholder="Key" value={getKey} onChange={e => setGetKey(e.target.value)} required
              style={{ width: '100%', padding: '10px', marginBottom: '10px', boxSizing: 'border-box' }} 
            />
            <button type="submit" style={{ width: '100%', padding: '10px', backgroundColor: '#9b59b6', color: 'white', border: 'none', cursor: 'pointer', fontWeight: 'bold' }}>
              Send GET Command
            </button>
          </form>

        </div>

        {/* RIGHT COLUMN: TERMINAL */}
        <div style={{ flex: 1, backgroundColor: '#2c3e50', color: '#ecf0f1', padding: '20px', borderRadius: '10px', height: '400px', overflowY: 'auto', fontFamily: 'monospace' }}>
          <h3 style={{ marginTop: 0, color: '#bdc3c7', borderBottom: '1px solid #7f8c8d', paddingBottom: '10px' }}>System Logs</h3>
          <div style={{ display: 'flex', flexDirection: 'column', gap: '8px', fontSize: '14px' }}>
            {logs.map((log, index) => (
              <div key={index} style={{ color: log.includes('Error') || log.includes('🚨') ? '#e74c3c' : (log.includes('✅') ? '#2ecc71' : 'white') }}>
                {log}
              </div>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}

export default App;