#ifndef SERVER_H
#define SERVER_H

#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>    // select(), fd_set, FD_SET, FD_ISSET, FD_ZERO

// NEW in Phase 3 — we need a data structure to track all connected clients
// vector = dynamic array — grows as clients connect, shrinks when they leave
#include <vector>

#include "../common/message.h"

// Maximum number of simultaneous clients we support
// select() has a system limit (FD_SETSIZE = 1024 on Linux)
// We cap at 10 for this project — plenty for a demo
#define MAX_CLIENTS 10

// ── Function declarations ──────────────────────────────────────────────────

// Same as Phase 1 — creates and returns configured server socket
int  createServerSocket(int port);

// NEW — replaces runPhase1Loop()
// The full select() event loop — handles many clients simultaneously
void runSelectLoop(int server_fd);

// NEW — broadcasts a message to all clients EXCEPT the sender
void broadcastMessage(const std::vector<int>& client_fds,
                      int                     sender_fd,
                      const std::string&      message);

#endif // SERVER_H