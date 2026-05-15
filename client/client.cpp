#include "client.h"

// ═══════════════════════════════════════════════════════════════════
//  connectToServer() — unchanged from Phase 4
// ═══════════════════════════════════════════════════════════════════
int connectToServer(const std::string& server_ip, int port)
{
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        std::cerr << "[ERROR] socket() failed\n";
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip.c_str());

    if (server_addr.sin_addr.s_addr == INADDR_NONE) {
        std::cerr << "[ERROR] Invalid IP address: " << server_ip << "\n";
        close(sock_fd);
        return -1;
    }

    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        std::cerr << "[ERROR] connect() failed — is the server running?\n";
        close(sock_fd);
        return -1;
    }

    return sock_fd;
}


// ═══════════════════════════════════════════════════════════════════
//  sendAll() — client side robust send, mirrors the server's version
// ═══════════════════════════════════════════════════════════════════
bool sendAll(int fd, const std::string& message)
{
    const char* data       = message.c_str();
    int         total      = message.size();
    int         sent_so_far = 0;

    while (sent_so_far < total)
    {
        int bytes = send(fd, data + sent_so_far, total - sent_so_far, 0);
        if (bytes == -1) return false;
        sent_so_far += bytes;
    }
    return true;
}


// ═══════════════════════════════════════════════════════════════════
//  runPhase5Loop()
//  Identical select() logic to Phase 4
//  Only difference: messages are formatted as "username: text"
//  which the server now does — client just sends raw text
// ═══════════════════════════════════════════════════════════════════
void runPhase5Loop(int sock_fd)
{
    char net_buffer[BUFFER_SIZE];
    char kbd_buffer[BUFFER_SIZE];
    int  max_fd = sock_fd;

    while (true)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock_fd,      &read_fds);

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity == -1) {
            if (errno == EINTR) continue; // signal interrupted — retry
            std::cerr << "[ERROR] select() failed\n";
            break;
        }

        // ── Server sent something ─────────────────────────────────
        if (FD_ISSET(sock_fd, &read_fds))
        {
            memset(net_buffer, 0, BUFFER_SIZE);
            int bytes = recv(sock_fd, net_buffer, BUFFER_SIZE - 1, 0);

            if (bytes == 0) {
                std::cout << "\n[INFO] Server disconnected.\n";
                break;
            }
            if (bytes == -1) {
                std::cerr << "\n[ERROR] recv() failed.\n";
                break;
            }

            // \r clears any partial text user may have typed
            std::cout << "\r" << net_buffer;
            std::cout << "You: ";
            std::cout.flush();
        }

        // ── User typed something ──────────────────────────────────
        if (FD_ISSET(STDIN_FILENO, &read_fds))
        {
            memset(kbd_buffer, 0, BUFFER_SIZE);

            if (fgets(kbd_buffer, BUFFER_SIZE, stdin) == NULL) {
                std::cout << "\n[INFO] Input closed.\n";
                break;
            }

            // Strip trailing newline
            int len = strlen(kbd_buffer);
            if (len > 0 && kbd_buffer[len-1] == '\n') {
                kbd_buffer[len-1] = '\0';
                len--;
            }

            if (strcmp(kbd_buffer, "exit") == 0 ||
                strcmp(kbd_buffer, "quit") == 0) {
                std::cout << "[INFO] Disconnecting...\n";
                break;
            }

            if (len == 0) continue;

            // Send raw text — server prepends "username: " for us
            if (!sendAll(sock_fd, std::string(kbd_buffer) + "\n")) {
                std::cerr << "[ERROR] sendAll() failed.\n";
                break;
            }
        }
    }
}


// ═══════════════════════════════════════════════════════════════════
//  main()
//  NEW in Phase 5: username handshake
//
//  Flow after connect():
//    Server sends: "[SERVER] Enter your username: "
//    Client receives it (via select loop or directly)
//    Client prompts user locally
//    Client sends chosen username to server
//    Server stores it, sends welcome message
//    Normal chat begins
// ═══════════════════════════════════════════════════════════════════
int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::cerr << "[USAGE] ./chat_client <server_ip> <port>\n";
        return 1;
    }

    std::string server_ip = argv[1];
    int         port      = std::stoi(argv[2]);

    std::cout << "╔══════════════════════════════╗\n";
    std::cout << "║   TCP Chat Client — Phase 5  ║\n";
    std::cout << "╚══════════════════════════════╝\n";

    // Connect to server
    int sock_fd = connectToServer(server_ip, port);
    if (sock_fd == -1) return 1;
    std::cout << "[INFO] Connected to " << server_ip << ":" << port << "\n";

    // ── USERNAME HANDSHAKE ────────────────────────────────────────
    // Step 1: receive the server's username prompt
    char prompt_buf[BUFFER_SIZE];
    memset(prompt_buf, 0, BUFFER_SIZE);
    int bytes = recv(sock_fd, prompt_buf, BUFFER_SIZE - 1, 0);
    if (bytes <= 0) {
        std::cerr << "[ERROR] Server closed before username prompt.\n";
        close(sock_fd);
        return 1;
    }
    // Print server's prompt: "[SERVER] Enter your username: "
    std::cout << prompt_buf;
    std::cout.flush();

    // Step 2: user types their username locally
    char username[USERNAME_SIZE];
    memset(username, 0, USERNAME_SIZE);
    if (fgets(username, USERNAME_SIZE, stdin) == NULL) {
        close(sock_fd);
        return 1;
    }
    // Strip newline
    int ulen = strlen(username);
    if (ulen > 0 && username[ulen-1] == '\n') username[ulen-1] = '\0';

    // Step 3: send username to server
    if (!sendAll(sock_fd, std::string(username) + "\n")) {
        std::cerr << "[ERROR] Failed to send username.\n";
        close(sock_fd);
        return 1;
    }

    // Step 4: receive welcome message from server
    memset(prompt_buf, 0, BUFFER_SIZE);
    bytes = recv(sock_fd, prompt_buf, BUFFER_SIZE - 1, 0);
    if (bytes > 0) std::cout << prompt_buf;

    // ── Chat begins ───────────────────────────────────────────────
    std::cout << "─────────────────────────────────────────\n";
    std::cout << "  Type a message + Enter to send          \n";
    std::cout << "  Type 'exit' to disconnect               \n";
    std::cout << "─────────────────────────────────────────\n";
    std::cout << "You: ";
    std::cout.flush();

    runPhase5Loop(sock_fd);

    close(sock_fd);
    std::cout << "[INFO] Goodbye.\n";
    return 0;
}