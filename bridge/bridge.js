// ═══════════════════════════════════════════════════════════════════
//  TCP ↔ WebSocket Bridge
//
//  What this file does:
//  - Runs a WebSocket server that browsers connect to
//  - For each browser connection, opens a raw TCP connection to C++ server
//  - Forwards messages in both directions in real time
//
//  Modules used:
//  'ws'  → WebSocket server (npm package)
//  'net' → Raw TCP client   (built into Node.js, no install needed)
// ═══════════════════════════════════════════════════════════════════

const WebSocket = require("ws"); // WebSocket server
const net = require("net"); // TCP client

// ── Configuration ──────────────────────────────────────────────────
const BRIDGE_PORT = 8081; // browser connects here (WebSocket)
const CPP_HOST = "127.0.0.1"; // where C++ server is running
const CPP_PORT = 8080; // C++ server's TCP port

// ── Start WebSocket server ──────────────────────────────────────────
// This is what the browser's JavaScript connects to
// ws://localhost:8081
const wss = new WebSocket.Server({ port: BRIDGE_PORT });

console.log("╔══════════════════════════════════════╗");
console.log("║      TCP ↔ WebSocket Bridge          ║");
console.log("╚══════════════════════════════════════╝");
console.log(
  `[BRIDGE] WebSocket server listening on ws://localhost:${BRIDGE_PORT}`,
);
console.log(`[BRIDGE] Forwarding to C++ server at ${CPP_HOST}:${CPP_PORT}`);
console.log("─────────────────────────────────────────");

// ═══════════════════════════════════════════════════════════════════
//  EVENT: new browser connects via WebSocket
//
//  'ws' here is the individual WebSocket connection to this browser
//  Not to be confused with the 'wss' WebSocket Server above
// ═══════════════════════════════════════════════════════════════════
wss.on("connection", (ws) => {
  console.log("[BRIDGE] Browser connected via WebSocket");

  // ── Open a TCP connection to the C++ server ───────────────────
  // net.createConnection() is Node.js's way of doing what
  // socket() + connect() does in C++ — creates a TCP client socket
  // and connects it to the given host and port
  //
  // Each browser gets its OWN dedicated TCP connection to C++ server
  // C++ server treats this bridge as just another TCP client
  const tcpSocket = net.createConnection({
    host: CPP_HOST,
    port: CPP_PORT,
  });

  // ── TCP connection established ────────────────────────────────
  tcpSocket.on("connect", () => {
    console.log(
      `[BRIDGE] TCP connection opened to C++ server (for this browser)`,
    );
  });

  // ── DATA: C++ server → TCP → Bridge → WebSocket → Browser ────
  // When the C++ server sends anything (welcome message, chat
  // message, server announcement), it arrives here as raw bytes.
  // We convert it to a string and forward to the browser.
  tcpSocket.on("data", (data) => {
    const message = data.toString(); // Buffer → string

    // Only forward if the browser WebSocket is still open
    // ws.readyState === WebSocket.OPEN checks the connection state
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(message);
      process.stdout.write(`[C++→Browser] ${message}`);
    }
  });

  // ── DATA: Browser → WebSocket → Bridge → TCP → C++ server ────
  // When the browser sends a message (user typed something),
  // it arrives here as a WebSocket message.
  // We forward it as raw bytes to the C++ server over TCP.
  ws.on("message", (message) => {
    const text = message.toString(); // Buffer → string
    process.stdout.write(`[Browser→C++] ${text}`);

    // tcpSocket.write() is the Node.js equivalent of send()
    // Writes raw bytes to the TCP socket
    if (tcpSocket.writable) {
      tcpSocket.write(text);
    }
  });

  // ── Browser disconnected ──────────────────────────────────────
  // When browser closes the tab / page or types 'exit'
  // We close the TCP connection to C++ server too — clean teardown
  ws.on("close", () => {
    console.log("[BRIDGE] Browser WebSocket closed → closing TCP connection");
    tcpSocket.destroy(); // forcibly close the TCP socket
  });

  // ── C++ server disconnected ───────────────────────────────────
  // If the C++ server shuts down (Ctrl+C), the TCP socket closes.
  // We notify the browser and close its WebSocket connection.
  tcpSocket.on("end", () => {
    console.log("[BRIDGE] C++ server closed TCP connection");
    if (ws.readyState === WebSocket.OPEN) {
      ws.send("[BRIDGE] Server disconnected.\n");
      ws.close();
    }
  });

  // ── Error handling ────────────────────────────────────────────
  // TCP errors: C++ server not running, connection refused, etc.
  tcpSocket.on("error", (err) => {
    console.error(`[BRIDGE] TCP error: ${err.message}`);
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(
        `[BRIDGE ERROR] Could not connect to chat server: ${err.message}\n`,
      );
      ws.close();
    }
  });

  // WebSocket errors
  ws.on("error", (err) => {
    console.error(`[BRIDGE] WebSocket error: ${err.message}`);
    tcpSocket.destroy();
  });
});

// ── WebSocket server error ────────────────────────────────────────
// e.g. port 8081 already in use
wss.on("error", (err) => {
  console.error(`[BRIDGE] Server error: ${err.message}`);
  process.exit(1);
});

// ── Graceful shutdown on Ctrl+C ───────────────────────────────────
// Close all connections before exiting
process.on("SIGINT", () => {
  console.log("\n[BRIDGE] Shutting down...");
  wss.close(() => {
    console.log("[BRIDGE] All connections closed. Goodbye.");
    process.exit(0);
  });
});
