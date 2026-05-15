#ifndef CLIENT_H
#define CLIENT_H

#include <iostream>
#include <string>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#include "../common/message.h"

int  connectToServer(const std::string& server_ip, int port);
bool sendAll(int fd, const std::string& message);
void runPhase5Loop(int sock_fd);

#endif // CLIENT_H