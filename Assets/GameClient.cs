using System;
using System.Collections.Generic;
using System.Linq;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using UnityEngine;
using UnityEngine.UI;

public class GameClient : MonoBehaviour
{
    // Networking
    [Header("Network Settings")]
    public string serverIP = "127.0.0.1";
    public int serverPort = 5000;

    private TcpClient client;
    private NetworkStream stream;
    private Thread receiveThread;
    private bool running = false;

    [Header("Scene References")]
    public Transform player0;       // Blue Cube
    public Transform player1;       // Green Cube
    public GameObject diamondPrefab;
    public GameObject wallPrefab;

    [Header("UI Panels")]
    public GameObject startPanel;
    public GameObject waitingPanel;
    public GameObject hudPanel;
    public GameObject gameOverPanel;

    [Header("UI Elements")]
    public Text scoreText;
    public Text feedbackText; 
    public Text winnerText;
    public Button startButton;
    public Button exitButton;

    // State
    private int myId = -1;
    private List<GameObject> diamondObjects = new List<GameObject>();
    private List<GameObject> wallObjects = new List<GameObject>(); 
    private bool mapGenerated = false;
    private bool autoPilot = false;
    private int lastScoreSum = 0;
    public AudioSource coinSource;

    private Queue<string> messageQueue = new Queue<string>();

    private struct StateSnapshot {
        public float time; 
        public Vector3 p0Pos;
        public Vector3 p1Pos;
    }
    private List<StateSnapshot> snapshots = new List<StateSnapshot>();

    void Start()
    {
        // Setup Buttons
        if(startButton) startButton.onClick.AddListener(OnStartClicked);
        
        // CHANGED: Exit instead of Restart
        if(exitButton) exitButton.onClick.AddListener(OnExitClicked);

        ShowPanel(startPanel);
    }

    void OnStartClicked()
    {
        ConnectToServer();
        ShowPanel(waitingPanel);
    }

    void OnExitClicked()
    {
        SendString("EXIT\n");
    }

    void Update()
    {
        HandleInput();
        ProcessMessages();
        InterpolateEntities();
        UpdateCamera();
        CheckProximityFeedback();
    }

    void CheckProximityFeedback()
    {
        if (player0 != null && player1 != null)
        {
            float dist = Vector3.Distance(player0.position, player1.position);
            if (dist < 0.9f) 
            {
                if(feedbackText) feedbackText.gameObject.SetActive(true);
            }
            else
            {
                if(feedbackText) feedbackText.gameObject.SetActive(false);
            }
        }
    }

    void ConnectToServer()
    {
        try
        {
            client = new TcpClient();
            client.Connect(serverIP, serverPort);
            stream = client.GetStream();
            running = true;

            receiveThread = new Thread(ReceiveLoop);
            receiveThread.IsBackground = true;
            receiveThread.Start();
        }
        catch (Exception e)
        {
            Debug.LogError("Connection Failed: " + e.Message);
        }
    }

    void HandleInput()
    {
        if (myId == -1 || stream == null || !client.Connected) return;

        if (Input.GetKeyDown(KeyCode.P)) {
            autoPilot = !autoPilot;
            Debug.Log("Auto-Pilot: " + autoPilot);
        }

        float x = 0, y = 0;

        if (autoPilot)
        {
            float val = Mathf.Sin(Time.time * 3.0f);
            
            if (val > 0.2f) x = 1;
            else if (val < -0.2f) x = -1;
            else x = 0;
            
            if(Mathf.Cos(Time.time * 1.5f) > 0.8f) y = 1;
            else if (Mathf.Cos(Time.time * 1.5f) < -0.8f) y = -1;
        }
        else
        {
            if (myId == 0) // Player 0 (WASD)
            {
                if (Input.GetKey(KeyCode.W)) y = 1;
                if (Input.GetKey(KeyCode.S)) y = -1;
                if (Input.GetKey(KeyCode.A)) x = -1;
                if (Input.GetKey(KeyCode.D)) x = 1;
            }
            else if (myId == 1) // Player 1 (Arrows)
            {
                if (Input.GetKey(KeyCode.UpArrow)) y = -1;
                if (Input.GetKey(KeyCode.DownArrow)) y = 1;
                if (Input.GetKey(KeyCode.LeftArrow)) x = 1;
                if (Input.GetKey(KeyCode.RightArrow)) x = -1;
            }
        }

        if (x != 0 || y != 0 || Time.frameCount % 10 == 0)
        {
            SendString($"INPUT {x} {y}\n");
        }
    }

    void SendString(string msg)
    {
        if (stream == null || !stream.CanWrite) return;
        try
        {
            byte[] data = Encoding.ASCII.GetBytes(msg);
            stream.Write(data, 0, data.Length);
        }
        catch { }
    }

    void ReceiveLoop()
    {
        byte[] buffer = new byte[4096];
        while (running)
        {
            try
            {
                if (stream.DataAvailable)
                {
                    int bytes = stream.Read(buffer, 0, buffer.Length);
                    string msg = Encoding.ASCII.GetString(buffer, 0, bytes);
                    lock (messageQueue) { messageQueue.Enqueue(msg); }
                }
                else
                {
                    Thread.Sleep(1);
                }
            }
            catch { running = false; break; }
        }
    }

    void ProcessMessages()
    {
        lock (messageQueue)
        {
            while (messageQueue.Count > 0)
            {
                string batch = messageQueue.Dequeue();
                string[] lines = batch.Split(new[] { '\n' }, StringSplitOptions.RemoveEmptyEntries);
                foreach (string line in lines) ParseMessage(line);
            }
        }
    }

    void ParseMessage(string line)
    {
        string[] parts = line.Split(' ');
        string cmd = parts[0];

        if (cmd == "SETUP")
        {
            if(myId == -1) myId = int.Parse(parts[1]); 
            
            int w = int.Parse(parts[2]);
            int h = int.Parse(parts[3]);
            string data = parts[4];
            
            int diamondCount = int.Parse(parts[5]);
            List<Vector2> diamondPositions = new List<Vector2>();
            int currentIndex = 6;
            
            for(int i = 0; i < diamondCount; i++)
            {
                float dx = float.Parse(parts[currentIndex]);
                float dy = float.Parse(parts[currentIndex + 1]);
                diamondPositions.Add(new Vector2(dx, dy));
                currentIndex += 2;
            }
        
            BuildMaze(w, h, data, diamondPositions);
            ShowPanel(hudPanel);
        }
        else if (cmd == "STATE")
        {
            // STATE <time> <p0x> <p0y> <p0s> <p1x> <p1y> <p1s> <diamonds...>
            Vector3 p0Pos = new Vector3(float.Parse(parts[2]), 0.5f, float.Parse(parts[3]));
            int p0Score = int.Parse(parts[4]);
            
            Vector3 p1Pos = new Vector3(float.Parse(parts[5]), 0.5f, float.Parse(parts[6]));
            int p1Score = int.Parse(parts[7]);

            string dBits = parts[8];
            UpdateDiamonds(dBits);

            if(scoreText) scoreText.text = $"BLUE: {p0Score}  |  GREEN: {p1Score}";

            StateSnapshot snap = new StateSnapshot();
            snap.time = Time.time; 
            snap.p0Pos = p0Pos;
            snap.p1Pos = p1Pos;
            snapshots.Add(snap);

            if ((p0Score + p1Score) > lastScoreSum)
            {
                if(coinSource) coinSource.Play();
                lastScoreSum = p0Score + p1Score;
            }

            if (snapshots.Count > 20) snapshots.RemoveAt(0);
        }
        else if (cmd == "GAMEOVER")
        {
            // GAMEOVER <WinnerID> <Score0> <Score1>
            int winner = int.Parse(parts[1]);
            int s0 = int.Parse(parts[2]);
            int s1 = int.Parse(parts[3]);

            string msg;
            if (winner == -1) msg = "IT'S A DRAW!";
            else if (winner == 0) msg = "BLUE WINS!";
            else msg = "GREEN WINS!";

            if(winnerText) winnerText.text = $"{msg}\n\nFinal Score:\nBlue: {s0} - Green: {s1}";
            
            ShowPanel(gameOverPanel);

            running = false;
        }
        else if (cmd == "SHUTDOWN")
        {
            Debug.Log("Server commanded shutdown.");
            Application.Quit();

            #if UNITY_EDITOR
            UnityEditor.EditorApplication.isPlaying = false;
            #endif
        }
    }

    void GenerateScenery()
    {
        for (int i = 0; i < 100; i++)
        {
            float rx = UnityEngine.Random.Range(-20f, 40f);
            float ry = UnityEngine.Random.Range(-20f, 40f);

            if (rx > -2f && rx < 23f && ry > -2f && ry < 23f) continue;

            float height = UnityEngine.Random.Range(1f, 6f);
            
            Vector3 pos = new Vector3(rx, height / 2f, ry);

            GameObject scenery = Instantiate(wallPrefab, pos, Quaternion.identity);
            
            // Apply scale
            scenery.transform.localScale = new Vector3(1f, height, 1f);
            
            wallObjects.Add(scenery);
        }
    }

    void BuildMaze(int w, int h, string data, List<Vector2> diamondPositions)
    {
        if (mapGenerated) return;

        foreach (var d in diamondObjects) Destroy(d);
        foreach (var wObj in wallObjects) Destroy(wObj);
        diamondObjects.Clear();
        wallObjects.Clear();

        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                int index = y * w + x;
                if (index < data.Length && data[index] == '1')
                {
                    GameObject wall = Instantiate(wallPrefab, new Vector3(x + 0.5f, 0.5f, y + 0.5f), Quaternion.identity);
                    wallObjects.Add(wall);
                }
            }
        }

        for(int i = 0; i < diamondPositions.Count; i++)
        {
            Vector3 pos = new Vector3(diamondPositions[i].x, 0.5f, diamondPositions[i].y);
            
            GameObject obj = Instantiate(diamondPrefab, pos, Quaternion.identity);
            obj.SetActive(true); 
            diamondObjects.Add(obj);
        }

        GenerateScenery();

        mapGenerated = true;
    }

    void UpdateDiamonds(string activeBits)
    {
        for(int i = 0; i < activeBits.Length && i < diamondObjects.Count; i++)
        {
            bool isActive = (activeBits[i] == '1');
            diamondObjects[i].SetActive(isActive);
        }
    }

    void InterpolateEntities()
    {
        if (snapshots.Count < 2) return;
        float renderDelay = 0.1f; 
        float renderTime = Time.time - renderDelay;
        StateSnapshot from = snapshots[0];
        StateSnapshot to = snapshots.LastOrDefault(); 

        for (int i = 0; i < snapshots.Count - 1; i++)
        {
            if (snapshots[i].time <= renderTime && snapshots[i + 1].time >= renderTime)
            {
                from = snapshots[i];
                to = snapshots[i + 1];
                break;
            }
        }
        float total = to.time - from.time;
        float t = 0;
        if (total > 0.0001f) t = (renderTime - from.time) / total;

        if(player0) player0.position = Vector3.Lerp(from.p0Pos, to.p0Pos, t);
        if(player1) player1.position = Vector3.Lerp(from.p1Pos, to.p1Pos, t);
    }

    void UpdateCamera()
    {
        Camera.main.orthographic = false;
        Camera.main.fieldOfView = 60f;
        Vector3 mazeCenter = new Vector3(10.5f, 0f, 10.5f);
        float camHeight = 25f; 
        float camDistance = 2f;

        if (myId == 0)
        {
            Camera.main.transform.position = new Vector3(10.5f, camHeight, 10.5f - camDistance);
            Camera.main.transform.LookAt(mazeCenter);
        }
        else if (myId == 1)
        {
            Camera.main.transform.position = new Vector3(10.5f, camHeight, 10.5f + camDistance);
            Camera.main.transform.LookAt(mazeCenter);
        }
        else
        {
            Camera.main.transform.position = new Vector3(10.5f, 25f, 8.5f);
            Camera.main.transform.LookAt(mazeCenter);
        }
    }
    
    void ShowPanel(GameObject panelToShow)
    {
        if(startPanel) startPanel.SetActive(false);
        if(waitingPanel) waitingPanel.SetActive(false);
        if(hudPanel) hudPanel.SetActive(false);
        if(gameOverPanel) gameOverPanel.SetActive(false);

        if(panelToShow != null) panelToShow.SetActive(true);
    }

    private void OnApplicationQuit()
    {
        running = false;
        if (receiveThread != null && receiveThread.IsAlive) receiveThread.Abort();
        if (stream != null) stream.Close();
        if (client != null) client.Close();
    }
}