#include "GameServer.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <random>
#include <stack>

// Constants
const float PLAYER_SPEED = 5.0f;
const float PLAYER_SIZE = 0.6f; // Assuming 1x1 tiles
const float DIAMOND_SIZE = 0.5f;

GameServer::GameServer(int port) {
    InitWinsock();

    // Create Socket
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed.\n";
        exit(1);
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed.\n";
        exit(1);
    }

    listen(serverSocket, SOMAXCONN);
    std::cout << "Server listening on port " << port << "...\n";

    // Init State
    clientConnected[0] = false;
    clientConnected[1] = false;
    
    GenerateMaze();
}

GameServer::~GameServer() {
    closesocket(serverSocket);
    WSACleanup();
}

void GameServer::InitWinsock() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed.\n";
        exit(1);
    }
}

// -----------------------------------------------------------------------------
// MAZE GENERATION (Recursive Backtracker)
// -----------------------------------------------------------------------------
void GameServer::GenerateMaze() {
    // Fill with walls
    maze.assign(mazeHeight, std::vector<int>(mazeWidth, 1));

    // Stack for DFS
    std::stack<std::pair<int, int>> stack;
    std::random_device rd;
    std::mt19937 rng(rd());

    int startX = 1, startY = 1;
    maze[startY][startX] = 0;
    stack.push({startX, startY});

    int dirs[4][2] = {{0, -2}, {0, 2}, {-2, 0}, {2, 0}};

    while (!stack.empty()) {
        auto [cx, cy] = stack.top();
        std::vector<int> neighbors;

        // Check neighbors
        for (int i = 0; i < 4; i++) {
            int nx = cx + dirs[i][0];
            int ny = cy + dirs[i][1];
            if (nx > 0 && nx < mazeWidth - 1 && ny > 0 && ny < mazeHeight - 1 && maze[ny][nx] == 1) {
                neighbors.push_back(i);
            }
        }

        if (!neighbors.empty()) {
            std::shuffle(neighbors.begin(), neighbors.end(), rng);
            int dir = neighbors[0];
            int nx = cx + dirs[dir][0];
            int ny = cy + dirs[dir][1];
            
            // Carve wall between
            maze[cy + dirs[dir][1]/2][cx + dirs[dir][0]/2] = 0;
            maze[ny][nx] = 0;
            
            stack.push({nx, ny});
        } else {
            stack.pop();
        }
    }

    // Spawn Diamonds in empty spots
    std::uniform_int_distribution<int> distX(1, mazeWidth - 2);
    std::uniform_int_distribution<int> distY(1, mazeHeight - 2);
    
    for(int i=0; i<15; i++) {
        int rX, rY;
        do {
            rX = distX(rng);
            rY = distY(rng);
        } while(maze[rY][rX] == 1); // Retry if wall

        Diamond d;
        d.id = i;
        d.x = (float)rX + 0.5f; // Center in tile
        d.y = (float)rY + 0.5f;
        d.active = true;
        diamonds.push_back(d);
    }

    // Set spawn points
    players[0] = {0, 1.5f, 1.5f, 0, 0, 0, 0, 0}; // Top Left
    players[1] = {1, (float)mazeWidth - 1.5f, (float)mazeHeight - 1.5f, 0, 0, 0, 0, 0}; // Bottom Right
}

// -----------------------------------------------------------------------------
// MAIN LOOP
// -----------------------------------------------------------------------------
void GameServer::AcceptClients() {
    std::cout << "Waiting for 2 clients to connect...\n";
    int connectedCount = 0;
    
    while (connectedCount < 2) {
        SOCKET client = accept(serverSocket, NULL, NULL);
        if (client != INVALID_SOCKET) {
            // Set Non-Blocking Mode
            u_long mode = 1;
            ioctlsocket(client, FIONBIO, &mode);

            clientSockets[connectedCount] = client;
            clientConnected[connectedCount] = true;
            std::cout << "Client " << connectedCount + 1 << " connected.\n";
            
            // Send Maze Data immediately (Reliable, no lag needed for setup)
            std::stringstream ss;
            ss << "SETUP " << connectedCount << " " << mazeWidth << " " << mazeHeight << " ";

            // 1. Send Maze Wall Data
            for(const auto& row : maze) 
                for(int cell : row) ss << cell;

            // 2. NEW: Send Diamond Data (Count + X + Y for each)
            ss << " " << diamonds.size() << " ";
            for(const auto& d : diamonds) {
                ss << d.x << " " << d.y << " ";
            }

            ss << "\n"; // End of message
            send(client, ss.str().c_str(), ss.str().length(), 0);
            
            connectedCount++;
        }
    }
    std::cout << "Game Starting!\n";
}

void GameServer::Run() {
    AcceptClients();

    auto lastTime = std::chrono::high_resolution_clock::now();

    while (gameRunning) {
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<float> diff = now - lastTime;
        float dt = diff.count();
        lastTime = now;

        // 1. Read Inputs -> Add to Latency Queue
        ReadNetworkInput();

        // 2. Process Inputs that have "arrived" after lag
        ProcessDelayedInputs();

        // 3. Physics & Game Logic
        UpdatePhysics(dt);
        CheckCollisions();

        // 4. Timer
        gameTimer -= dt;
        if (gameTimer <= 0) gameTimer = 0;

        // 5. Broadcast State (via Latency Queue)
        BroadcastState();

        // Sleep to cap at ~60hz
        Sleep(16);
    }
}

// -----------------------------------------------------------------------------
// NETWORKING & LAG SIMULATION
// -----------------------------------------------------------------------------
void GameServer::ReadNetworkInput() {
    char buffer[1024];
    for (int i = 0; i < 2; i++) {
        if (!clientConnected[i]) continue;

        int bytesReceived = recv(clientSockets[i], buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0';
            // Simulate Network Delay: Push to Queue
            DelayedMessage msg;
            msg.data = std::string(buffer);
            msg.playerId = i;
            msg.deliveryTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(LATENCY_MS);
            inboundQueue.push_back(msg);
        }
    }
}

void GameServer::ProcessDelayedInputs() {
    auto now = std::chrono::steady_clock::now();
    
    while (!inboundQueue.empty()) {
        DelayedMessage& msg = inboundQueue.front();
        if (now >= msg.deliveryTime) {
            // Message has "arrived"
            std::stringstream ss(msg.data);
            std::string cmd;
            ss >> cmd;
            if (cmd == "INPUT") {
                float inX, inY;
                ss >> inX >> inY;
                players[msg.playerId].inputX = inX;
                players[msg.playerId].inputY = inY;
            } else if (cmd == "EXIT") {
                std::cout << "EXIT requested. Shutting down...\n";
                std::string shutdownMsg = "SHUTDOWN\n";
                
                // Tell BOTH players to quit
                for(int i=0; i<2; i++) {
                    SendRawToClient(i, shutdownMsg);
                }
                gameRunning = false; // Stop the server loop
            }
            inboundQueue.pop_front();
        } else {
            break; // Queue is sorted by time, so we can stop
        }
    }
}

void GameServer::BroadcastState() {
    // Format: STATE <time> <p1x> <p1y> <p1s> <p2x> <p2y> <p2s> <d1_active>...<dn_active>
    std::stringstream ss;
    ss << "STATE " << gameTimer << " ";
    for (int i = 0; i < 2; i++) {
        ss << players[i].x << " " << players[i].y << " " << players[i].score << " ";
    }
    for (const auto& d : diamonds) {
        ss << (d.active ? 1 : 0);
    }
    ss << "\n"; // Newline delimiter

    // Add to outbound queue for latency
    DelayedMessage msg;
    msg.data = ss.str();
    msg.deliveryTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(LATENCY_MS);
    outboundQueue.push_back(msg);

    // Process Outbound Queue
    auto now = std::chrono::steady_clock::now();
    while (!outboundQueue.empty()) {
        DelayedMessage& outMsg = outboundQueue.front();
        if (now >= outMsg.deliveryTime) {
            for (int i = 0; i < 2; i++) {
                SendRawToClient(i, outMsg.data);
            }
            outboundQueue.pop_front();
        } else {
            break;
        }
    }
}

void GameServer::SendRawToClient(int clientId, std::string message) {
    if (clientConnected[clientId]) {
        send(clientSockets[clientId], message.c_str(), message.length(), 0);
    }
}

// -----------------------------------------------------------------------------
// PHYSICS
// -----------------------------------------------------------------------------
bool GameServer::IsWall(float x, float y) {
    int gx = (int)x;
    int gy = (int)y;
    if (gx < 0 || gx >= mazeWidth || gy < 0 || gy >= mazeHeight) return true;
    return maze[gy][gx] == 1;
}

bool CheckPlayerCollision(float x, float y, int myId, Player* allPlayers) {
    float safeDist = 0.8f; // Slightly larger than player size (0.6) to keep a gap
    for(int i=0; i<2; i++) {
        if(i == myId) continue; // Don't check against self
        
        float dx = x - allPlayers[i].x;
        float dy = y - allPlayers[i].y;
        float dist = sqrt(dx*dx + dy*dy);
        
        if(dist < safeDist) return true; // Collision detected!
    }
    return false;
}

void GameServer::UpdatePhysics(float dt) {
    float halfSize = PLAYER_SIZE / 2.0f; 

    for (int i = 0; i < 2; i++) {
        Player& p = players[i];
        
        float dx = p.inputX * PLAYER_SPEED * dt;
        float dy = p.inputY * PLAYER_SPEED * dt;

        // Try moving X
        float nextX = p.x + dx;
        bool wallHitX = IsWall(nextX - halfSize, p.y) || IsWall(nextX + halfSize, p.y);
        bool playerHitX = CheckPlayerCollision(nextX, p.y, i, players);

        if (!wallHitX && !playerHitX) {
            p.x += dx;
        }

        // Try moving Y
        float nextY = p.y + dy;
        bool wallHitY = IsWall(p.x, nextY - halfSize) || IsWall(p.x, nextY + halfSize);
        bool playerHitY = CheckPlayerCollision(p.x, nextY, i, players);

        if (!wallHitY && !playerHitY) {
            p.y += dy;
        }
    }
}

void GameServer::CheckCollisions() {
    int activeDiamonds = 0;

    for (int i = 0; i < 2; i++) {
        for (auto& d : diamonds) {
            if (!d.active) continue;
            
            // Count active diamonds
            activeDiamonds++;

            float dist = sqrt(pow(players[i].x - d.x, 2) + pow(players[i].y - d.y, 2));
            if (dist < (PLAYER_SIZE/2 + DIAMOND_SIZE/2)) {
                d.active = false;
                players[i].score++;
                activeDiamonds--; // Decrement since we just picked one up
            }
        }
    }

    // GAME OVER CHECK
    if (activeDiamonds <= 0) {
        // Format: GAMEOVER <WinnerID> <Score0> <Score1>
        int winner = (players[0].score > players[1].score) ? 0 : 1;
        if (players[0].score == players[1].score) winner = -1; // Draw

        std::stringstream ss;
        ss << "GAMEOVER " << winner << " " << players[0].score << " " << players[1].score << "\n";
        
        // Send to both immediately
        for(int i=0; i<2; i++) SendRawToClient(i, ss.str());
        
        // Don't loop this message
        gameRunning = false; 
    }
}

void GameServer::ResetGame() {
    // Reset Scores
    players[0].score = 0;
    players[1].score = 0;
    
    // Regenerate Maze and Diamonds
    GenerateMaze(); // This resets positions too
    
    // Restart Loop
    gameRunning = true;

    // Send NEW SETUP to both clients
    std::stringstream ss;
    ss << "SETUP " << 0 << " " << mazeWidth << " " << mazeHeight << " "; // Hack: ID doesn't matter here
    for(const auto& row : maze) for(int cell : row) ss << cell;
    ss << " " << diamonds.size() << " ";
    for(const auto& d : diamonds) ss << d.x << " " << d.y << " ";
    ss << "\n";

    // Re-broadcast setup (We need to handle ID re-assignment carefully, 
    // but for this simple test, assume clients keep their IDs).
    SendRawToClient(0, ss.str());
    SendRawToClient(1, ss.str());
}