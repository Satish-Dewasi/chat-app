#include "server.h"

// ═══════════════════════════════════════════════════════════════════
//  createServerSocket()
//  Goal: Get a socket ready to accept client connections
//  Returns: the server's file descriptor (a number like 3, 4, 5...)
// ═══════════════════════════════════════════════════════════════════
int createServerSocket(int port)
{
    // ── STEP 1: socket() ───────────────────────────────────────────
    // socket(domain, type, protocol)
    //   AF_INET     = IPv4  (Layer 3 — IP addressing)
    //   SOCK_STREAM = TCP   (Layer 4 — reliable, ordered stream)
    //   0           = let OS pick the right protocol (TCP for STREAM)
    //
    // Returns a file descriptor (fd) — just an integer
    // Think of it like: int fd = open("network_connection")
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1) {
        std::cerr << "[ERROR] socket() failed\n";
        return -1;
    }
    std::cout << "[INFO] Socket created. fd = " << server_fd << "\n";

    // ── SO_REUSEADDR trick ────────────────────────────────────────
    // Problem: If you restart the server quickly, the OS still holds
    // the port in TIME_WAIT state for ~60 seconds → "Address already
    // in use" error. This option tells the OS: "let me reuse it now."
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // ── STEP 2: bind() ────────────────────────────────────────────
    // We need to tell the OS: "this socket belongs to port 8080 on
    // this machine." We do that by filling a sockaddr_in struct.
    //
    // sockaddr_in is just a container:
    //   sin_family  = address family (IPv4)
    //   sin_port    = port number in "network byte order"
    //   sin_addr    = IP address (INADDR_ANY = all interfaces on this machine)
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); // zero out — no garbage values

    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(port);       // htons = Host TO Network Short
                                                     // CPUs store numbers differently
                                                     // than networks expect — htons
                                                     // converts to the right format
    server_addr.sin_addr.s_addr = INADDR_ANY;        // accept connections on ANY
                                                     // network interface (WiFi, eth0...)

    // bind() links the socket fd to this address struct
    // (void*) cast is required — bind() takes a generic sockaddr pointer
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        std::cerr << "[ERROR] bind() failed — port " << port << " may be in use\n";
        close(server_fd);
        return -1;
    }
    std::cout << "[INFO] Socket bound to port " << port << "\n";

    // ── STEP 3: listen() ──────────────────────────────────────────
    // listen(fd, backlog)
    //   backlog = 10 → OS will queue up to 10 pending connections
    //   before we call accept(). If more arrive, they're refused.
    //
    // After this call, the socket is PASSIVE — it won't send data,
    // it just waits for incoming connection requests.
    if (listen(server_fd, 10) == -1) {
        std::cerr << "[ERROR] listen() failed\n";
        close(server_fd);
        return -1;
    }
    std::cout << "[INFO] Server listening on port " << port << "...\n";

    return server_fd; // hand the fd back to main()
}


// ═══════════════════════════════════════════════════════════════════
//  runPhase1Loop()
//  Goal: Accept ONE client, echo everything they send, then exit
//  (Phase 2 will loop this; Phase 3 will add multi-client)
// ═══════════════════════════════════════════════════════════════════
void runPhase1Loop(int server_fd)
{
    // ── STEP 4: accept() ──────────────────────────────────────────
    // accept() BLOCKS here — the program pauses until a client
    // connects. Think of it as: "stand at the door and wait."
    //
    // When a client arrives:
    //   - OS completes the TCP 3-way handshake automatically
    //   - accept() returns a BRAND NEW file descriptor just for
    //     talking to this one client
    //   - server_fd is still open, waiting for the next client
    //   - client_addr gets filled with the client's IP + port
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    std::cout << "[INFO] Waiting for a client to connect...\n";

    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

    if (client_fd == -1) {
        std::cerr << "[ERROR] accept() failed\n";
        return;
    }

    // inet_ntoa() converts the binary IP address back to a readable
    // string like "127.0.0.1". ntohs() reverses htons() for the port.
    std::cout << "[INFO] Client connected! IP: "
              << inet_ntoa(client_addr.sin_addr)
              << "  Port: " << ntohs(client_addr.sin_port)
              << "  fd: " << client_fd << "\n";

    // ── STEP 5: recv() + send() loop ─────────────────────────────
    // Now we just read whatever the client sends and echo it back.
    // recv() also BLOCKS — waits until data arrives.
    //
    // recv() return values:
    //   > 0  → bytes received, data is in buffer
    //   = 0  → client disconnected gracefully
    //   = -1 → error
    char buffer[BUFFER_SIZE];

    while (true)
    {
        memset(buffer, 0, BUFFER_SIZE); // clear buffer before every read

        int bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        //                                              ^^^^^^^^^^^
        //                        -1 leaves room for null terminator '\0'
        //                        so we can safely print it as a C-string

        if (bytes_received == 0) {
            std::cout << "[INFO] Client disconnected.\n";
            break;
        }

        if (bytes_received == -1) {
            std::cerr << "[ERROR] recv() failed\n";
            break;
        }

        std::cout << "[CLIENT says]: " << buffer << "\n";

        // Echo it back — send() returns number of bytes actually sent
        // In real apps you'd check if send() sent ALL bytes (it might not)
        // We'll handle that properly in later phases
        std::string reply = "[ECHO]: " + std::string(buffer);
        send(client_fd, reply.c_str(), reply.size(), 0);
    }

    // ── STEP 6: close() ───────────────────────────────────────────
    // Always close file descriptors when done.
    // This sends a FIN packet → graceful TCP teardown (4-way handshake)
    close(client_fd);
    std::cout << "[INFO] Client fd closed.\n";
}


// ═══════════════════════════════════════════════════════════════════
//  main()
// ═══════════════════════════════════════════════════════════════════
int main(int argc, char* argv[])
{
    // Allow port to be passed as command-line argument
    // e.g.  ./server 9090
    // Otherwise default to DEFAULT_PORT (8080)
    int port = (argc >= 2) ? std::stoi(argv[1]) : DEFAULT_PORT;

    std::cout << "╔══════════════════════════════╗\n";
    std::cout << "║   TCP Chat Server — Phase 1  ║\n";
    std::cout << "╚══════════════════════════════╝\n";

    int server_fd = createServerSocket(port);
    if (server_fd == -1) return 1;

    runPhase1Loop(server_fd);

    // Close the listening socket — server shuts down cleanly
    close(server_fd);
    std::cout << "[INFO] Server shut down.\n";
    return 0;
}