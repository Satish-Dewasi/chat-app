#include "server.h"

// ── Global flag ────────────────────────────────────────────────────
// Starts as 1 (true = keep running)
// signalHandler sets it to 0 when Ctrl+C is pressed
// The main select() loop checks this every iteration
volatile sig_atomic_t g_running = 1;


// ═══════════════════════════════════════════════════════════════════
//  signalHandler()
//  Called automatically by the OS when Ctrl+C (SIGINT) is pressed
//
//  Rules for signal handlers — they must be simple:
//  - No cout/printf (not signal-safe)
//  - No malloc/new
//  - Only set flags — let the main loop do the real cleanup
//  - write() is signal-safe, cout is NOT
// ═══════════════════════════════════════════════════════════════════
void signalHandler(int signal)
{
    if (signal == SIGINT) {
        // Signal-safe way to print — write() is allowed, cout is not
        const char* msg = "\n[SERVER] Ctrl+C received. Shutting down...\n";
        write(STDOUT_FILENO, msg, strlen(msg));
        g_running = 0;  // tell the main loop to stop
    }
}


// ═══════════════════════════════════════════════════════════════════
//  sendAll()
//  Problem: send() does not guarantee all bytes are sent in one call
//
//  Why does this happen?
//  send() writes to the kernel's socket send buffer. If that buffer
//  is temporarily full (network congestion, slow receiver), the OS
//  only accepts as many bytes as fit right now and returns that count.
//  The rest of your data is silently ignored — you must retry.
//
//  Example without sendAll():
//    send(fd, "Hello World", 11, 0) → returns 6
//    Only "Hello " was sent. " World" was dropped silently.
//
//  sendAll() loops until every byte is delivered:
// ═══════════════════════════════════════════════════════════════════
bool sendAll(int fd, const std::string& message)
{
    // c_str() gives a const char* pointer to the string's data
    const char* data      = message.c_str();
    int         total     = message.size();  // total bytes to send
    int         sent_so_far = 0;             // bytes sent so far

    while (sent_so_far < total)
    {
        // Send from wherever we left off last iteration
        // data + sent_so_far → pointer arithmetic, moves start forward
        int bytes = send(fd, data + sent_so_far, total - sent_so_far, 0);

        if (bytes == -1) {
            // Real error — connection broken
            return false;
        }

        sent_so_far += bytes;
        // Loop continues if sent_so_far < total (partial send happened)
    }

    return true; // all bytes delivered
}


// ═══════════════════════════════════════════════════════════════════
//  broadcastMessage()
//  UPGRADED from Phase 3:
//  - Now uses sendAll() instead of send()
//  - Message format already contains sender's name (built by caller)
// ═══════════════════════════════════════════════════════════════════
void broadcastMessage(const std::vector<int>&         client_fds,
                      const std::map<int,std::string>& usernames,
                      int                              sender_fd,
                      const std::string&               message)
{
    for (int fd : client_fds)
    {
        if (fd == sender_fd) continue;  // don't echo back to sender

        if (!sendAll(fd, message)) {
            // Log but don't crash — select() loop handles the broken fd
            std::string name = "unknown";
            auto it = usernames.find(fd);
            if (it != usernames.end()) name = it->second;
            std::cerr << "[WARN] sendAll() failed for " << name
                      << " (fd " << fd << ")\n";
        }
    }
}


// ═══════════════════════════════════════════════════════════════════
//  createServerSocket() — unchanged from Phase 3
// ═══════════════════════════════════════════════════════════════════
int createServerSocket(int port)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "[ERROR] socket() failed\n";
        return -1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        std::cerr << "[ERROR] bind() failed\n";
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 10) == -1) {
        std::cerr << "[ERROR] listen() failed\n";
        close(server_fd);
        return -1;
    }

    std::cout << "[INFO] Server listening on port " << port << "...\n";
    return server_fd;
}


// ═══════════════════════════════════════════════════════════════════
//  runSelectLoop()
//  UPGRADED from Phase 3:
//
//  NEW: std::map<int, std::string> usernames
//       Maps each fd to the username that client chose
//       e.g. {4: "Alice", 5: "Bob", 6: "Charlie"}
//
//  NEW: Username handshake
//       First message from any new client = their chosen username
//       Server stores it, then announces them to the room
//
//  NEW: g_running check — loop exits cleanly on Ctrl+C
//  NEW: sendAll() used everywhere instead of send()
//  NEW: Graceful shutdown — notifies all clients before closing
// ═══════════════════════════════════════════════════════════════════
void runSelectLoop(int server_fd)
{
    std::vector<int>             client_fds;  // connected client fds
    std::map<int, std::string>   usernames;   // fd → username

    char buffer[BUFFER_SIZE];

    std::cout << "[INFO] Waiting for clients. Press Ctrl+C to shut down.\n";
    std::cout << "─────────────────────────────────────────\n";

    // ── MAIN EVENT LOOP ───────────────────────────────────────────
    // g_running starts as 1. Signal handler sets it to 0 on Ctrl+C.
    while (g_running)
    {
        // ── Build watch set ───────────────────────────────────────
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        int max_fd = server_fd;

        for (int fd : client_fds) {
            FD_SET(fd, &read_fds);
            if (fd > max_fd) max_fd = fd;
        }

        // ── select() with a timeout ───────────────────────────────
        // NEW in Phase 5: we give select() a 1-second timeout
        // instead of NULL (block forever).
        //
        // Why? If we block forever and Ctrl+C is pressed, select()
        // gets interrupted (returns -1, errno = EINTR). The loop
        // would then check g_running = 0 and exit cleanly.
        // But with a timeout, we check g_running every second even
        // if no network activity happens — more responsive shutdown.
        //
        // timeval: { seconds, microseconds }
        struct timeval timeout;
        timeout.tv_sec  = 1;   // wake up every 1 second at minimum
        timeout.tv_usec = 0;

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (activity == -1) {
            // EINTR means a signal interrupted select() — not a real error
            // Just loop back and check g_running
            if (errno == EINTR) continue;
            std::cerr << "[ERROR] select() failed\n";
            break;
        }

        // Timeout expired with no activity — loop back, check g_running
        if (activity == 0) continue;

        // ── New client connecting ─────────────────────────────────
        if (FD_ISSET(server_fd, &read_fds))
        {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int new_fd = accept(server_fd,
                               (struct sockaddr*)&client_addr,
                               &client_len);
            if (new_fd == -1) {
                std::cerr << "[ERROR] accept() failed\n";
                continue;
            }

            if ((int)client_fds.size() >= MAX_CLIENTS) {
                std::string full = "[SERVER] Chat is full. Try later.\n";
                sendAll(new_fd, full);
                close(new_fd);
                continue;
            }

            client_fds.push_back(new_fd);

            // ── USERNAME HANDSHAKE ────────────────────────────────
            // We don't know this client's name yet.
            // Store a placeholder — real name arrives as first message.
            // The client is coded to send their username immediately
            // after connecting, before any chat messages.
            usernames[new_fd] = ""; // empty = "hasn't sent username yet"

            // Ask them for their name
            std::string prompt = "[SERVER] Enter your username: ";
            sendAll(new_fd, prompt);

            std::cout << "[CONNECT] New connection → fd " << new_fd
                      << "  IP: " << inet_ntoa(client_addr.sin_addr)
                      << " (waiting for username)\n";
        }

        // ── Check each client fd ──────────────────────────────────
        int i = 0;
        while (i < (int)client_fds.size())
        {
            int fd = client_fds[i];

            if (!FD_ISSET(fd, &read_fds)) {
                i++;
                continue;
            }

            memset(buffer, 0, BUFFER_SIZE);
            int bytes = recv(fd, buffer, BUFFER_SIZE - 1, 0);

            // ── Client disconnected ───────────────────────────────
            if (bytes <= 0)
            {
                std::string name = usernames.count(fd) ? usernames[fd] : "unknown";

                if (name.empty()) name = "unnamed";

                std::cout << "[DISCONNECT] " << name
                          << " (fd " << fd << ") left.\n";

                // Announce departure only if they had a username
                if (!name.empty() && name != "unnamed") {
                    std::string bye = "[SERVER] " + name + " has left the chat.\n";
                    broadcastMessage(client_fds, usernames, fd, bye);
                }

                close(fd);
                usernames.erase(fd);
                client_fds.erase(client_fds.begin() + i);
                continue; // don't increment i
            }

            // Remove trailing newline from received data
            int len = strlen(buffer);
            if (len > 0 && buffer[len-1] == '\n') buffer[len-1] = '\0';

            // ── USERNAME HANDSHAKE — receive username ─────────────
            // If this client's username is still empty,
            // this first message IS their username
            if (usernames[fd].empty())
            {
                std::string chosen_name(buffer);

                // Basic validation — trim and reject blank names
                if (chosen_name.empty()) {
                    sendAll(fd, "[SERVER] Invalid username. Try again: ");
                    i++;
                    continue;
                }

                // Enforce max length
                if (chosen_name.size() > USERNAME_SIZE) {
                    chosen_name = chosen_name.substr(0, USERNAME_SIZE);
                }

                // Store the username
                usernames[fd] = chosen_name;

                std::cout << "[JOIN] " << chosen_name
                          << " joined the chat (fd " << fd << ").\n";

                // Welcome the new user
                std::string welcome = "[SERVER] Welcome, " + chosen_name
                                    + "! " + std::to_string(client_fds.size())
                                    + " user(s) in the chat.\n";
                sendAll(fd, welcome);

                // Announce to everyone else
                std::string announce = "[SERVER] " + chosen_name
                                     + " has joined the chat!\n";
                broadcastMessage(client_fds, usernames, fd, announce);

                i++;
                continue;
            }

            // ── Normal chat message ───────────────────────────────
            // Username is known — format and broadcast the message
            std::string sender_name = usernames[fd];
            std::string message     = sender_name + ": " + buffer + "\n";

            // Print on server terminal too — useful for monitoring
            std::cout << "[CHAT] " << message;

            // Broadcast to everyone except sender
            broadcastMessage(client_fds, usernames, fd, message);

            i++;
        }

    } // end while(g_running)

    // ── GRACEFUL SHUTDOWN ─────────────────────────────────────────
    // Ctrl+C was pressed — notify all connected clients before closing
    std::cout << "[INFO] Shutting down — notifying "
              << client_fds.size() << " client(s)...\n";

    std::string shutdown_msg = "[SERVER] Server is shutting down. Goodbye!\n";
    for (int fd : client_fds) {
        sendAll(fd, shutdown_msg);
        close(fd);
    }

    std::cout << "[INFO] All clients notified and disconnected.\n";
}


// ═══════════════════════════════════════════════════════════════════
//  main()
// ═══════════════════════════════════════════════════════════════════
int main(int argc, char* argv[])
{
    int port = (argc >= 2) ? std::stoi(argv[1]) : DEFAULT_PORT;

    std::cout << "╔══════════════════════════════╗\n";
    std::cout << "║   TCP Chat Server — Phase 5  ║\n";
    std::cout << "╚══════════════════════════════╝\n";

    // ── Register signal handler ───────────────────────────────────
    // signal(SIGINT, signalHandler) means:
    // "when SIGINT arrives (Ctrl+C), call signalHandler() instead of
    //  the default behaviour (immediate kill)"
    signal(SIGINT, signalHandler);

    int server_fd = createServerSocket(port);
    if (server_fd == -1) return 1;

    runSelectLoop(server_fd);

    close(server_fd);
    std::cout << "[INFO] Server shut down cleanly.\n";
    return 0;
}