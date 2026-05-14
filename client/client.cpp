#include "client.h"

// ═══════════════════════════════════════════════════════════════════
//  connectToServer()
//  Goal: Create a socket and connect it to the server
//  Compare with server: server does socket→bind→listen→accept
//                       client does socket→connect  (much simpler)
// ═══════════════════════════════════════════════════════════════════
int connectToServer(const std::string& server_ip, int port)
{
    // ── STEP 1: socket() ───────────────────────────────────────────
    // Identical to the server — we need a socket fd too
    // AF_INET = IPv4, SOCK_STREAM = TCP, 0 = let OS pick protocol
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd == -1) {
        std::cerr << "[ERROR] socket() failed\n";
        return -1;
    }
    std::cout << "[INFO] Socket created. fd = " << sock_fd << "\n";

    // ── STEP 2: connect() ─────────────────────────────────────────
    // This is what the CLIENT has instead of bind+listen+accept.
    // connect() says: "reach out to THIS specific server address"
    //
    // It triggers the TCP 3-way handshake:
    //   Client → SYN       → Server
    //   Client ← SYN-ACK   ← Server
    //   Client → ACK       → Server
    // After this, the connection is established and data can flow.
    //
    // We fill the same sockaddr_in struct — but this time it holds
    // the SERVER's address (not our own address like in bind())
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(port);

    // inet_addr() converts a human-readable IP string like "127.0.0.1"
    // into the 32-bit binary format the OS understands
    // It's the reverse of inet_ntoa() which we used in the server
    server_addr.sin_addr.s_addr = inet_addr(server_ip.c_str());

    if (server_addr.sin_addr.s_addr == INADDR_NONE) {
        std::cerr << "[ERROR] Invalid IP address: " << server_ip << "\n";
        close(sock_fd);
        return -1;
    }

    std::cout << "[INFO] Connecting to " << server_ip << ":" << port << "...\n";

    // connect() BLOCKS until the handshake completes or fails
    // If the server isn't running → "Connection refused" error
    // If server is unreachable  → times out after ~75 seconds
    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        std::cerr << "[ERROR] connect() failed — is the server running?\n";
        close(sock_fd);
        return -1;
    }

    std::cout << "[INFO] Connected to server successfully!\n";
    std::cout << "─────────────────────────────────────────\n";
    std::cout << "  Type a message and press Enter to send  \n";
    std::cout << "  Type 'exit' to disconnect               \n";
    std::cout << "─────────────────────────────────────────\n";

    return sock_fd;
}


// ═══════════════════════════════════════════════════════════════════
//  runPhase2Loop()
//  Goal: Simple send → receive → send → receive loop
//
//  LIMITATION (intentional for Phase 2):
//  This is SEQUENTIAL — we send, then we wait for reply, then repeat.
//  Problem: what if the server sends a message we didn't ask for?
//           (e.g. another client's broadcast in Phase 3)
//           We'd miss it because we're stuck waiting for user input.
//  Fix: Phase 4 solves this with select() on the client side too.
// ═══════════════════════════════════════════════════════════════════
void runPhase2Loop(int sock_fd)
{
    char    recv_buffer[BUFFER_SIZE];   // for incoming data from server
    std::string user_input;             // for what the user types

    while (true)
    {
        // ── Read from keyboard ────────────────────────────────────
        // std::getline() reads a full line from stdin (the keyboard)
        // It BLOCKS here until the user presses Enter
        std::cout << "You: ";
        std::cout.flush(); // make sure "You: " appears before user types

        if (!std::getline(std::cin, user_input)) {
            // getline fails if stdin closes (e.g. Ctrl+D)
            std::cout << "\n[INFO] Input closed. Disconnecting...\n";
            break;
        }

        // ── Check for exit command ────────────────────────────────
        if (user_input == "exit" || user_input == "quit") {
            std::cout << "[INFO] Disconnecting...\n";
            break;
        }

        // ── Skip empty messages ───────────────────────────────────
        if (user_input.empty()) {
            continue;
        }

        // ── STEP 3: send() ────────────────────────────────────────
        // send(fd, data_pointer, data_length, flags)
        // flags = 0 → default behaviour, no special options
        //
        // Returns bytes actually sent.
        // Could be less than requested on congested networks —
        // we'll add a robust send() helper in Phase 5.
        // For now, assume it sends everything.
        int bytes_sent = send(sock_fd, user_input.c_str(), user_input.size(), 0);

        if (bytes_sent == -1) {
            std::cerr << "[ERROR] send() failed. Server may have closed.\n";
            break;
        }

        // ── STEP 4: recv() ────────────────────────────────────────
        // Now wait for the server's reply
        // This BLOCKS until the server sends something back
        memset(recv_buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(sock_fd, recv_buffer, BUFFER_SIZE - 1, 0);

        if (bytes_received == 0) {
            // Server closed the connection from its side
            std::cout << "[INFO] Server disconnected.\n";
            break;
        }

        if (bytes_received == -1) {
            std::cerr << "[ERROR] recv() failed.\n";
            break;
        }

        // Print the server's reply
        std::cout << "Server: " << recv_buffer << "\n";
    }
}


// ═══════════════════════════════════════════════════════════════════
//  main()
//  Usage: ./chat_client <server_ip> <port>
//  e.g.   ./chat_client 127.0.0.1 8080
// ═══════════════════════════════════════════════════════════════════
int main(int argc, char* argv[])
{
    // Validate arguments
    if (argc < 3) {
        std::cerr << "[USAGE] ./chat_client <server_ip> <port>\n";
        std::cerr << "        ./chat_client 127.0.0.1 8080\n";
        return 1;
    }

    std::string server_ip = argv[1];
    int         port      = std::stoi(argv[2]);

    std::cout << "╔══════════════════════════════╗\n";
    std::cout << "║   TCP Chat Client — Phase 2  ║\n";
    std::cout << "╚══════════════════════════════╝\n";

    // Connect to the server
    int sock_fd = connectToServer(server_ip, port);
    if (sock_fd == -1) return 1;

    // Start the chat loop
    runPhase2Loop(sock_fd);

    // Always close the socket cleanly
    // This sends a TCP FIN → server's recv() will return 0
    close(sock_fd);
    std::cout << "[INFO] Socket closed. Goodbye.\n";
    return 0;
}