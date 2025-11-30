# Multiplayer Maze Runner (Server-Authoritative)

A real-time 2-player maze collection game developed for the **Associate Game Developer Test**. This project demonstrates raw socket programming, server authority, and latency compensation without using third-party networking middleware.

## 🛠️ Technology Stack

* **Server:** C++17 (Raw Winsock, `ws2_32.lib`)
* **Client:** Unity 2022+ (C# `TcpClient`)
* **Architecture:** Server-Authoritative with Client-Side Prediction and Interpolation.

---

## 🚀 Setup & Compilation

### Step 1: Compile the Server
You need a C++ compiler (MinGW/G++ or Visual Studio) to build the server executable. run this where your main.cpp file is stored

**Using G++ (MinGW):**
```bash
g++ main.cpp GameServer.cpp -o server -lws2_32
````

### Step 2: Prepare the Client (Unity)

1.  Open the project in Unity.
2.  Ensure the `GameClient.cs` script is placed in the **Assets** folder and attached to a GameManager object in the scene. 
3.  Add relevant Prefabs and connect them to the GameManager object
4.  Go to **File -\> Build Settings**.
5.  Click **Build** and save the executable as `Game.exe`.

-----

## 🎮 How to Run

1.  **Start the Server:**

      * Double-click `server.exe`.
      * It will open a console and display: *"Waiting for 2 clients..."*

2.  **Start Player 1:**

      * Run `Game.exe`.
      * Click the **"Connect & Start"** button.

3.  **Start Player 2:**

      * Run `Game.exe` again (open a second window).
      * Click **"Connect & Start"**.

4.  **Play:**

      * The game begins automatically once both players connect.

-----

## 🕹️ Controls

The input method depends on which player slot you are assigned (Client 1 or Client 2).

| Action | Player 0 (Blue) | Player 1 (Green) |
| :--- | :--- | :--- |
| **Move Up** | `W` | `Up Arrow` / `Down Arrow`\* |
| **Move Down** | `S` | `Down Arrow` / `Up Arrow`\* |
| **Move Left** | `A` | `Left Arrow` |
| **Move Right** | `D` | `Right Arrow` |

*\*Note: Player 1's camera is rotated 180 degrees to provide a "home" perspective. Depending on preference, Up Arrow moves the player visually "up" the screen (South on the server grid).*

-----

## 🧪 Testing Simultaneous Movement (Single PC)

Since Windows only sends input to the active (focused) window, you cannot control both players simultaneously on one computer using the keyboard. To demonstrate multiplayer synchronization for video recording or testing:

1.  Launch the Server and two Client windows.
2.  Click on **Window B** (Player 1) to focus it.
3.  Press **`P`**.
      * This toggles **Auto-Pilot Mode**. The Green player will begin moving automatically back and forth.
4.  Click on **Window A** (Player 0) to focus it.
5.  Use **WASD** to control the Blue player manually.

*Result: You will see both players moving simultaneously on both screens, proving that network synchronization and interpolation are working correctly.*

*Note: The first game.exe you run will be the player 0 (blue) and the second one will be player 1 (green).*

-----

## 📝 Notes on Implementation

### Coin Spawning Logic

The original requirement suggested spawning coins at fixed time intervals. In this implementation, coins are generated randomly across the map at the **start of the round**.

  * **Reasoning:** This creates an immediate race condition and faster gameplay flow.
  * **Extensibility:** The logic can be easily modified to use a server-side timer to spawn diamonds incrementally if preferred.

### Setup Requirements

  * Ensure that the `GameClient.cs` script in Unity has all Prefabs (Player0, Player1, Wall, Diamond) and UI Panels (Start, HUD, GameOver) linked in the Inspector before building.


### Note
  * For more information about the specs you can also go through description.docx. The assets folder is also in the github directory. It can also directly be copied into you unity project file. Then you just need to coonect the coorect materials, etc to their repective prefabs in hierarchy.
<!-- end list -->