#ifndef SERVER_H
#define SERVER_H

// ── What each header gives us ──────────────────────────────────────────────
// iostream      : cout, cerr  (printing to terminal)
// string        : std::string
// cstring       : memset()    (zero out memory buffers)
// unistd.h      : close()     (close file descriptors)
// sys/socket.h  : socket(), bind(), listen(), accept(), send(), recv()
// netinet/in.h  : sockaddr_in (the struct that holds IP + port)
// arpa/inet.h   : htons(), inet_addr() (convert IP/port to network format)

#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../common/message.h"

// ── Function declarations ──────────────────────────────────────────────────

// Creates and returns a configured server socket (fd)
// Returns -1 on failure
int  createServerSocket(int port);

// Waits for one client to connect, handles echo, then closes
void runPhase1Loop(int server_fd);

#endif // SERVER_H