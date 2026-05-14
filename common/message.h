#ifndef MESSAGE_H
#define MESSAGE_H

// Maximum size of any single message (in bytes)
// 1024 bytes = 1 KB — plenty for a chat message
#define BUFFER_SIZE 1024

// The port our server will listen on
// Ports 1024-65535 are available to regular users (no root needed)
#define DEFAULT_PORT 8080

#endif // MESSAGE_H