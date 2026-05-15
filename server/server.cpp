#include "server.h"

// ═══════════════════════════════════════════════════════════════════
//  createServerSocket()
//  Identical to Phase 1 — nothing changes here
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
//  broadcastMessage()
//  Send a message to every connected client EXCEPT the sender
//
//  Why exclude the sender?
//  The sender already sees their own message on their own screen.
//  Sending it back to them would print it twice — confusing.
// ═══════════════════════════════════════════════════════════════════
void broadcastMessage(const std::vector<int>& client_fds,
                      int                     sender_fd,
                      const std::string&      message)
{
    for (int fd : client_fds)
    {
        // Skip the sender — they don't need their own message echoed back
        if (fd == sender_fd) continue;

        int bytes_sent = send(fd, message.c_str(), message.size(), 0);

        if (bytes_sent == -1) {
            // This client's connection may have broken
            // We log it but don't crash — the select() loop will
            // detect the broken fd on the next iteration
            std::cerr << "[WARN] send() failed for fd " << fd << "\n";
        }
    }
}


// ═══════════════════════════════════════════════════════════════════
//  runSelectLoop()
//  The core of Phase 3 — one loop, many clients, no threads
//
//  Key data structures:
//
//  client_fds  → std::vector<int>
//                tracks all currently connected client file descriptors
//                grows when clients connect, shrinks when they leave
//
//  read_fds    → fd_set
//                a SET of file descriptors select() watches for READ activity
//                "read activity" means: data arrived OR new connection arrived
//
//  max_fd      → the highest fd number in our watch set
//                select() needs this to know how far to scan
// ═══════════════════════════════════════════════════════════════════
void runSelectLoop(int server_fd)
{
    // Our dynamic list of connected client fds
    // Starts empty — clients join as they connect
    std::vector<int> client_fds;

    char buffer[BUFFER_SIZE];

    std::cout << "[INFO] Waiting for clients...\n";
    std::cout << "─────────────────────────────────────────\n";

    // ── THE MAIN EVENT LOOP ───────────────────────────────────────
    // This loop runs FOREVER — every iteration is one "event"
    // An event is: new client connects OR existing client sends data
    while (true)
    {
        // ── BUILD THE WATCH SET ───────────────────────────────────
        // fd_set is a fixed-size bit array (1024 bits on Linux)
        // Each bit represents one fd — 1 = watch it, 0 = ignore it
        //
        // We MUST rebuild this every iteration because select()
        // MODIFIES the fd_set to show which fds are ready.
        // If we don't rebuild, we lose track of fds that weren't ready.
        fd_set read_fds;
        FD_ZERO(&read_fds);              // zero out all bits — start clean

        // Always watch the server fd — new clients might connect
        FD_SET(server_fd, &read_fds);    // set server_fd's bit to 1
        int max_fd = server_fd;          // track the highest fd number

        // Watch every connected client fd too
        for (int fd : client_fds)
        {
            FD_SET(fd, &read_fds);
            if (fd > max_fd) max_fd = fd;   // update max if this fd is bigger
        }

        // ── CALL select() ─────────────────────────────────────────
        // select(max_fd + 1, &read_fds, NULL, NULL, NULL)
        //
        // Argument breakdown:
        //   max_fd + 1   → scan fds from 0 up to max_fd (inclusive)
        //                  +1 because it's an exclusive upper bound
        //   &read_fds    → watch these fds for READ activity
        //   NULL         → not watching for WRITE activity
        //   NULL         → not watching for ERRORS (separately)
        //   NULL         → no timeout — block FOREVER until activity
        //
        // select() MODIFIES read_fds:
        //   BEFORE: read_fds = {server_fd, A_fd, B_fd}   (all we watch)
        //   AFTER:  read_fds = {A_fd}                    (only A has data)
        //
        // Returns: number of fds that are ready (-1 = error)
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        if (activity == -1) {
            std::cerr << "[ERROR] select() failed\n";
            break;
        }

        // ── CHECK: IS server_fd READY? ────────────────────────────
        // If server_fd is in the ready set, a NEW client is connecting
        // FD_ISSET checks if a specific fd's bit is set in read_fds
        if (FD_ISSET(server_fd, &read_fds))
        {
            // A new client knocked → accept() them
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int new_fd = accept(server_fd,
                               (struct sockaddr*)&client_addr,
                               &client_len);

            if (new_fd == -1) {
                std::cerr << "[ERROR] accept() failed\n";
                continue; // don't crash, try again next iteration
            }

            // Enforce client limit
            if ((int)client_fds.size() >= MAX_CLIENTS) {
                std::string msg = "[SERVER] Chat is full. Try again later.\n";
                send(new_fd, msg.c_str(), msg.size(), 0);
                close(new_fd);
                std::cout << "[INFO] Rejected client — server full\n";
                continue;
            }

            // Add new client to our tracking list
            client_fds.push_back(new_fd);

            std::cout << "[CONNECT] New client → IP: "
                      << inet_ntoa(client_addr.sin_addr)
                      << "  Port: " << ntohs(client_addr.sin_port)
                      << "  fd: " << new_fd
                      << "  (Total clients: " << client_fds.size() << ")\n";

            // Welcome message to the new client
            std::string welcome = "[SERVER] Welcome! "
                                + std::to_string(client_fds.size())
                                + " client(s) connected.\n";
            send(new_fd, welcome.c_str(), welcome.size(), 0);

            // Announce to everyone else that someone joined
            std::string announce = "[SERVER] A new client joined. (fd "
                                 + std::to_string(new_fd) + ")\n";
            broadcastMessage(client_fds, new_fd, announce);
        }

        // ── CHECK: WHICH CLIENT FDs ARE READY? ───────────────────
        // We can't use range-based for loop here because we might
        // need to REMOVE elements (disconnected clients) mid-loop.
        // Use index-based loop with careful erase() handling.
        int i = 0;
        while (i < (int)client_fds.size())
        {
            int fd = client_fds[i];

            if (FD_ISSET(fd, &read_fds))
            {
                // This client has data waiting — read it
                memset(buffer, 0, BUFFER_SIZE);
                int bytes_received = recv(fd, buffer, BUFFER_SIZE - 1, 0);

                if (bytes_received == 0)
                {
                    // ── Client disconnected gracefully ────────────
                    // recv() returning 0 = client sent TCP FIN
                    std::cout << "[DISCONNECT] fd " << fd
                              << " disconnected. "
                              << "(Remaining: " << client_fds.size() - 1
                              << " client(s))\n";

                    // Announce departure to remaining clients
                    std::string bye = "[SERVER] A client left. (fd "
                                    + std::to_string(fd) + ")\n";
                    broadcastMessage(client_fds, fd, bye);

                    // Close the fd and remove from our list
                    close(fd);
                    // erase() removes the element and shifts others left
                    // We do NOT increment i — the next element slides
                    // into position i, so we check i again
                    client_fds.erase(client_fds.begin() + i);
                    continue; // skip the i++ at the bottom
                }

                if (bytes_received == -1)
                {
                    // ── Connection error ──────────────────────────
                    std::cerr << "[ERROR] recv() failed on fd " << fd << "\n";
                    close(fd);
                    client_fds.erase(client_fds.begin() + i);
                    continue;
                }

                // ── Valid message received — broadcast it ─────────
                std::string message = "[fd " + std::to_string(fd)
                                    + "]: " + std::string(buffer);

                std::cout << "[MSG] " << message;

                // Send to all OTHER connected clients
                broadcastMessage(client_fds, fd, message);
            }

            i++; // move to next client
        }

    } // end while(true)

    // Clean up — close all remaining client connections
    for (int fd : client_fds) {
        close(fd);
    }
}


// ═══════════════════════════════════════════════════════════════════
//  main()
// ═══════════════════════════════════════════════════════════════════
int main(int argc, char* argv[])
{
    int port = (argc >= 2) ? std::stoi(argv[1]) : DEFAULT_PORT;

    std::cout << "╔══════════════════════════════╗\n";
    std::cout << "║   TCP Chat Server — Phase 3  ║\n";
    std::cout << "╚══════════════════════════════╝\n";

    int server_fd = createServerSocket(port);
    if (server_fd == -1) return 1;

    runSelectLoop(server_fd);

    close(server_fd);
    std::cout << "[INFO] Server shut down.\n";
    return 0;
}