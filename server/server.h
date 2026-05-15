#ifndef SERVER_H
#define SERVER_H

#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <vector>
#include <map>       // NEW: maps fd → username  e.g. {4:"Alice", 5:"Bob"}
#include <csignal>   // NEW: signal(), SIGINT — for Ctrl+C handling

#include "../common/message.h"

// ── Global state ───────────────────────────────────────────────────
// volatile: tells compiler "this can change outside normal code flow"
//           (signal handlers run asynchronously — compiler must not
//            cache this variable in a register)
// sig_atomic_t: a type guaranteed to be read/written atomically
//               safe to use in signal handlers
extern volatile sig_atomic_t g_running;

// ── Function declarations ──────────────────────────────────────────
int  createServerSocket(int port);

// NEW: send ALL bytes — retries if send() delivers less than requested
// Returns true if all bytes sent, false on error
bool sendAll(int fd, const std::string& message);

// UPGRADED: now uses username map instead of fd numbers
void broadcastMessage(const std::vector<int>&        client_fds,
                      const std::map<int,std::string>& usernames,
                      int                              sender_fd,
                      const std::string&               message);

// NEW: signal handler — called when user presses Ctrl+C
void signalHandler(int signal);

// UPGRADED: full Phase 5 loop with username handshake + robust send
void runSelectLoop(int server_fd);

#endif // SERVER_H