#ifndef CLIENT_H
#define CLIENT_H

// Same headers as server — sockets are symmetric at the API level
// The difference is in WHICH functions we call, not which headers we include
#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../common/message.h"

// Creates and returns a connected socket fd to the server
// Returns -1 on failure
int connectToServer(const std::string& server_ip, int port);

// Main loop: read user input → send → receive echo → print
void runPhase2Loop(int sock_fd);

#endif // CLIENT_H