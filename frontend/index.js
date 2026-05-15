// ═══════════════════════════════════════════════════════════════════
//  TCP Chat Frontend
//  Connects to Node.js bridge via WebSocket
//  Bridge forwards to C++ server via raw TCP
// ═══════════════════════════════════════════════════════════════════

// ── Config ─────────────────────────────────────────────────────────
const BRIDGE_URL = "ws://localhost:8081";
const COLORS = [
  "#00d4aa",
  "#f59e0b",
  "#818cf8",
  "#f472b6",
  "#34d399",
  "#fb923c",
  "#60a5fa",
  "#a78bfa",
];

// ── State ───────────────────────────────────────────────────────────
let ws = null;
let myUsername = "";
let onlineUsers = []; // list of names we've seen join
let userColors = {}; // username → color
let colorIndex = 0;
let waitingForUsernamePrompt = true;
let messageCount = 0;
let lastSender = "";

// ── DOM refs ────────────────────────────────────────────────────────
const modalOverlay = document.getElementById("modal-overlay");
const usernameInput = document.getElementById("username-input");
const joinBtn = document.getElementById("join-btn");
const modalError = document.getElementById("modal-error");
const app = document.getElementById("app");
const statusDot = document.getElementById("status-dot");
const statusText = document.getElementById("status-text");
const usersList = document.getElementById("users-list");
const onlineCount = document.getElementById("online-count");
const messages = document.getElementById("messages");
const emptyState = document.getElementById("empty-state");
const msgInput = document.getElementById("msg-input");
const sendBtn = document.getElementById("send-btn");

// ── Utility: get or assign a color for a username ───────────────────
function getColor(name) {
  if (!userColors[name]) {
    userColors[name] = COLORS[colorIndex % COLORS.length];
    colorIndex++;
  }
  return userColors[name];
}

// ── Utility: initials from username ────────────────────────────────
function initials(name) {
  return name.slice(0, 2).toUpperCase();
}

// ── Utility: format timestamp ───────────────────────────────────────
function timestamp() {
  const now = new Date();
  return now.toLocaleTimeString([], {
    hour: "2-digit",
    minute: "2-digit",
  });
}

// ── Update connection status dot + text ────────────────────────────
function setStatus(state, text) {
  statusDot.className = "status-dot " + state;
  statusText.textContent = text;
}

// ── Add/remove user from sidebar ───────────────────────────────────
function addUser(name) {
  if (onlineUsers.includes(name)) return;
  onlineUsers.push(name);
  renderUsers();
}

function removeUser(name) {
  onlineUsers = onlineUsers.filter((u) => u !== name);
  renderUsers();
}

function renderUsers() {
  usersList.innerHTML = "";
  onlineUsers.forEach((name) => {
    const color = getColor(name);
    const isMe = name === myUsername;
    const item = document.createElement("div");
    item.className = "user-item";
    item.innerHTML = `
      <div class="user-avatar" style="background:${color}">${initials(name)}</div>
      <div class="user-name ${isMe ? "you" : ""}">${name}</div>
      ${isMe ? '<span class="you-badge">you</span>' : ""}
    `;
    usersList.appendChild(item);
  });
  onlineCount.textContent = onlineUsers.length + " online";
}

// ── Append a SERVER / system message ───────────────────────────────
function appendServerMessage(text, isAccent = false) {
  const el = document.createElement("div");
  el.className = "msg-server";
  el.innerHTML = `<span class="msg-server-text ${isAccent ? "accent" : ""}">${text}</span>`;
  messages.appendChild(el);
  scrollToBottom();
  lastSender = "__server__";
}

// ── Append a CHAT message bubble ────────────────────────────────────
function appendChatMessage(author, text) {
  const isMine = author === myUsername;
  const isConsec = lastSender === author;
  const color = getColor(author);

  const wrap = document.createElement("div");
  wrap.className = `msg-wrap ${isMine ? "mine" : "theirs"} ${isConsec ? "consecutive" : "first-in-group"}`;

  wrap.innerHTML = `
    <div class="msg-meta">
      <span class="msg-author" style="color:${color}">${author}</span>
      <span class="msg-time">${timestamp()}</span>
    </div>
    <div class="msg-bubble">${escapeHTML(text)}</div>
  `;

  // Hide empty state
  emptyState.classList.remove("visible");

  messages.appendChild(wrap);
  scrollToBottom();
  lastSender = author;
  messageCount++;
}

// ── Escape HTML to prevent injection ───────────────────────────────
function escapeHTML(str) {
  return str.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

// ── Scroll chat to bottom ───────────────────────────────────────────
function scrollToBottom() {
  messages.scrollTop = messages.scrollHeight;
}

// ── Parse incoming message from server ─────────────────────────────
// Server sends either:
//   "[SERVER] some announcement"
//   "[BRIDGE ...] something"
//   "Username: message text"
function parseAndDisplay(raw) {
  const text = raw.trim();
  if (!text) return;

  // Server / system messages
  if (text.startsWith("[SERVER]") || text.startsWith("[BRIDGE")) {
    const content = text
      .replace(/^\[SERVER\]\s*/, "")
      .replace(/^\[BRIDGE[^\]]*\]\s*/, "");

    // Detect join/leave for user list management
    const joinMatch = content.match(/^(.+) has joined the chat/);
    const leftMatch = content.match(/^(.+) has left the chat/);

    if (joinMatch) addUser(joinMatch[1]);
    if (leftMatch) removeUser(leftMatch[1]);

    appendServerMessage(content, joinMatch !== null);
    return;
  }

  // Chat message — format: "Author: message text"
  const colonIdx = text.indexOf(": ");
  if (colonIdx !== -1) {
    const author = text.slice(0, colonIdx);
    const msgText = text.slice(colonIdx + 2);
    appendChatMessage(author, msgText);
    return;
  }

  // Fallback — show as server message
  appendServerMessage(text);
}

// ── Connect to Bridge ───────────────────────────────────────────────
function connectWebSocket() {
  setStatus("", "connecting...");

  ws = new WebSocket(BRIDGE_URL);

  ws.onopen = () => {
    setStatus("connected", "connected");
  };

  ws.onmessage = (event) => {
    const text = event.data.toString();

    // ── Username handshake ────────────────────────────────────────
    // First message from server = "[SERVER] Enter your username: "
    // We're waiting for this before showing the modal as active
    if (waitingForUsernamePrompt) {
      if (text.includes("Enter your username")) {
        waitingForUsernamePrompt = false;
        // Modal is already visible — user can now type their name
        joinBtn.disabled = false;
        usernameInput.focus();
      }
      return; // don't display the prompt text in chat
    }

    // ── Welcome message — after username sent ─────────────────────
    if (text.includes("Welcome,") && text.includes(myUsername)) {
      // We're in! Hide modal, show app
      addUser(myUsername);
      revealApp();
      // Show welcome as system message
      const content = text.replace(/^\[SERVER\]\s*/, "").trim();
      appendServerMessage(content, true);
      return;
    }

    // Normal message — parse and display
    parseAndDisplay(text);
  };

  ws.onclose = () => {
    setStatus("error", "disconnected");
    msgInput.disabled = true;
    sendBtn.disabled = true;
    appendServerMessage("Disconnected from server.");
  };

  ws.onerror = () => {
    setStatus("error", "connection failed");
    modalError.textContent =
      "Could not connect to bridge. Is it running on port 8081?";
  };
}

// ── Reveal app, hide modal ──────────────────────────────────────────
function revealApp() {
  modalOverlay.style.display = "none";
  app.classList.add("visible");
  emptyState.classList.add("visible");
  msgInput.disabled = false;
  sendBtn.disabled = false;
  msgInput.focus();
}

// ── Send a message ──────────────────────────────────────────────────
function sendMessage() {
  const text = msgInput.value.trim();
  if (!text || !ws || ws.readyState !== WebSocket.OPEN) return;

  ws.send(text + "\n");
  appendChatMessage(myUsername, text); // show own message immediately
  msgInput.value = "";
  msgInput.style.height = "auto";
}

// ── Join button click ───────────────────────────────────────────────
joinBtn.addEventListener("click", () => {
  const name = usernameInput.value.trim();
  if (!name) {
    modalError.textContent = "Please enter a username.";
    return;
  }
  if (waitingForUsernamePrompt) {
    modalError.textContent = "Still connecting... please wait.";
    return;
  }
  myUsername = name;
  modalError.textContent = "";
  ws.send(name + "\n"); // send username to server
  joinBtn.disabled = true;
  joinBtn.textContent = "Joining...";
});

// ── Enter key in modal ──────────────────────────────────────────────
usernameInput.addEventListener("keydown", (e) => {
  if (e.key === "Enter") joinBtn.click();
});

// ── Enter/Shift+Enter in message input ─────────────────────────────
msgInput.addEventListener("keydown", (e) => {
  if (e.key === "Enter" && !e.shiftKey) {
    e.preventDefault();
    sendMessage();
  }
});

// ── Auto-resize textarea ────────────────────────────────────────────
msgInput.addEventListener("input", () => {
  msgInput.style.height = "auto";
  msgInput.style.height = Math.min(msgInput.scrollHeight, 120) + "px";
});

// ── Send button click ───────────────────────────────────────────────
sendBtn.addEventListener("click", sendMessage);

// ── Start ───────────────────────────────────────────────────────────
joinBtn.disabled = true; // disable until WebSocket connects
connectWebSocket();
