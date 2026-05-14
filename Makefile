CXX      = g++
CXXFLAGS = -Wall -Wextra -std=c++17

# Build both server and client
all: chat_server chat_client

# Server binary
chat_server: server/server.cpp server/server.h common/message.h
	$(CXX) $(CXXFLAGS) server/server.cpp -o chat_server

# Client binary
chat_client: client/client.cpp client/client.h common/message.h
	$(CXX) $(CXXFLAGS) client/client.cpp -o chat_client

clean:
	rm -f chat_server chat_client