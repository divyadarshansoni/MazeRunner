#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <string>
#include <deque>
#include <chrono>

#pragma comment (lib, "ws2_32.lib")

// Data Structures
struct Player {
    int id;
    float x, y;
    float vx, vy;
    int score;
    // Input state
    float inputX, inputY; 
};

struct Diamond {
    int id;
    float x, y;
    bool active;
};

struct DelayedMessage {
    std::chrono::steady_clock::time_point deliveryTime;
    std::string data;
    int playerId; // For inbound inputs, which player sent it?
};

class GameServer {
public:
    GameServer(int port);
    ~GameServer();
    void Run();

private:
    // Networking
    void InitWinsock();
    void AcceptClients(); // Waits for 2 players
    void ReadNetworkInput();
    void ProcessDelayedInputs();
    void BroadcastState();
    void SendRawToClient(int clientId, std::string message);

    // Simulation
    void UpdatePhysics(float dt);
    void CheckCollisions();
    void GenerateMaze();
    bool IsWall(float x, float y);
    void ResetGame();
    
    // State
    SOCKET serverSocket;
    SOCKET clientSockets[2];
    bool clientConnected[2];
    
    Player players[2];
    std::vector<Diamond> diamonds;
    
    // Maze Data (1 = Wall, 0 = Path)
    int mazeWidth = 21;
    int mazeHeight = 21;
    std::vector<std::vector<int>> maze;

    // Latency Queues
    std::deque<DelayedMessage> inboundQueue;  // Inputs waiting to be processed
    std::deque<DelayedMessage> outboundQueue; // State updates waiting to be sent

    const int LATENCY_MS = 200;
    float gameTimer = 60.0f;
    bool gameRunning = true;
};