#include "GameServer.h"
#include <iostream>

int main() {
    std::cout << "=== Multiplayer Maze Server ===\n";
    std::cout << "Port: 5000\n";
    std::cout << "Latency: 200ms (Simulated)\n";
    
    // Create and run server on port 5000
    GameServer server(5000);
    server.Run();

    return 0;
}