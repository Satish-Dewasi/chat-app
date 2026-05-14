CXX      = g++
CXXFLAGS = -Wall -Wextra -std=c++17

all: chat_server

chat_server: server/server.cpp server/server.h common/message.h
	$(CXX) $(CXXFLAGS) server/server.cpp -o chat_server

clean:
	rm -f chat_server chat_client