#pragma once
// Multiplayer networking (ENet, UDP).
//
//  GameServer - runs inside the host's game ("Host Game" in the pause menu). The host's
//               World is the real one: it loads chunks around every player, streams them
//               to clients, checks and applies their block edits and sends out every change.
//  GameClient - used after joining from the Multiplayer screen. Its World is in remote mode
//               and only holds what the server sent.
//
// Neither class touches the window or OpenGL, so the same server code can later run in a
// dedicated (windowless) server.

#include <glm/glm.hpp>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "World.h"

typedef struct _ENetHost ENetHost;
typedef struct _ENetPeer ENetPeer;
typedef struct _ENetPacket ENetPacket;

namespace net {

constexpr uint16_t kDefaultPort      = 30000;
constexpr uint32_t kProtocolVersion  = 3;   // 2: positions carry the sender's time, 3: pings for the player list
constexpr int      kMaxClients       = 64;
constexpr int      kMaxViewDistance  = 10;   // chunks the server will send around a player

// Another player as this game sees them (positions are smoothed between updates).
struct PlayerView {
    std::string name;
    glm::vec3 position{0.0f};
    glm::vec3 target{0.0f};
    float yaw = 0.0f, pitch = 0.0f;
    float targetYaw = 0.0f, targetPitch = 0.0f;
    glm::vec3 velocity{0.0f};     // estimated from the last two updates (blocks/second)
    double lastUpdate = 0.0;      // when the last update arrived (seconds, steady clock)
    float lastSenderTime = 0.0f;  // the sender's clock in that update (orders packets, gives speed)
    // For /netstats: position updates received per second
    int updatesCounted = 0;
    double rateWindowStart = 0.0;
    float updateRate = 0.0f;
    glm::vec3 color{1.0f};
    bool hasPosition = false;
    int ping = -1;                // ms, from the server every couple of seconds (-1 = not known yet)
};
using PlayerMap = std::unordered_map<uint16_t, PlayerView>;

// A line to show in chat, with its colour.
struct ChatLine {
    std::string text;
    glm::vec3 color{1.0f};
};

// Seconds since the program started (used as the time stamp in position updates).
float clockSeconds();

// Shown in the server list (and checked against the server's version).
constexpr const char* kVersionName = "Craft & Ruin 0.1";
// ENet "connect data" meaning "just tell me your status, I'm not joining".
constexpr uint32_t kStatusQuery = 0x53544154;   // "STAT"
constexpr int kMaxMotdLength = 120;

// "host", "host:port" -> host + port (kDefaultPort if none). False if empty/invalid.
bool parseAddress(const std::string& text, std::string& host, uint16_t& port);

// Player names: 1-16 letters, digits, '_' or '-'.
bool validPlayerName(const std::string& name);

} // namespace net

// ===========================================================================
// Server
// ===========================================================================
class GameServer {
public:
    GameServer() = default;
    ~GameServer();
    GameServer(const GameServer&) = delete;
    GameServer& operator=(const GameServer&) = delete;

    // Starts listening on `port`. `world` must stay alive until stop().
    bool start(World& world, uint16_t port, const std::string& gameFolder,
               const std::string& hostName, std::string& error);
    // Kicks everyone with `reason` and closes the port.
    void stop(const std::string& reason = "The host closed the server");

    bool running() const { return m_host != nullptr; }
    uint16_t port() const { return m_port; }
    int playerCount() const;   // including the host

    // Call every frame while hosting (also while paused or loading).
    void update(const glm::vec3& hostPos, float hostYaw, float hostPitch, float dt);

    // Other players, for drawing (the host itself isn't in here).
    const net::PlayerMap& players() const { return m_views; }

    // Lines for /netstats: each player's ping and position updates per second.
    std::vector<std::string> statsLines() const;

    // What the server list shows (the owner sets these in Settings > Server).
    // Can be changed while running; maxPlayers counts the host too.
    void setInfo(const std::string& motd, int maxPlayers);
    const std::string& motd() const { return m_motd; }
    int maxPlayers() const { return m_maxPlayers; }

    // Chat from the host to everyone.
    void sendChat(const std::string& text);
    // Lines the host should see in chat (joins, leaves, other players' messages).
    std::vector<net::ChatLine> takeChat();

private:
    struct Client {
        ENetPeer* peer = nullptr;
        uint16_t id = 0;
        bool joined = false;
        std::string name;
        int viewDistance = 8;
        glm::vec3 position{0.0f};
        float yaw = 0.0f, pitch = 0.0f;
        float sentTime = 0.0f;   // client's clock in its last position update
        bool hasPosition = false;
        std::unordered_set<World::ChunkCoord, PairHash> sentChunks;
    };

    void handlePacket(Client& c, const uint8_t* data, size_t size);
    void sendStatus(ENetPeer* peer);
    void handleHello(Client& c, const uint8_t* data, size_t size);
    void handleBlocks(Client& c, const uint8_t* data, size_t size);
    void kick(Client& c, const std::string& reason);
    void dropClient(uint16_t id);

    void streamChunks(Client& c);
    void sendBlockChanges(const std::vector<World::BlockChange>& changes);
    void sendPositions();
    void broadcastChat(const std::string& text, const glm::vec3& color);

    Client* findByPeer(ENetPeer* peer);

    ENetHost* m_host = nullptr;
    World* m_world = nullptr;
    uint16_t m_port = 0;
    std::string m_game;
    std::string m_hostName;
    std::string m_motd = "A Craft & Ruin server";
    int m_maxPlayers = 20;
    uint16_t m_nextId = 1;           // 0 is the host
    std::unordered_map<uint16_t, Client> m_clients;
    net::PlayerMap m_views;
    std::vector<net::ChatLine> m_chat;

    glm::vec3 m_hostPos{0.0f};
    float m_hostYaw = 0.0f, m_hostPitch = 0.0f;
    float m_posTimer = 0.0f;
    float m_pingTimer = 0.0f;
};

// ===========================================================================
// Client
// ===========================================================================
class GameClient {
public:
    enum class State { Idle, Connecting, Joined };

    struct Welcome {
        uint16_t playerId = 0;
        uint32_t seed = 0;
        glm::vec3 spawn{0.0f};
        std::string hostName;
    };

    GameClient() = default;
    ~GameClient();
    GameClient(const GameClient&) = delete;
    GameClient& operator=(const GameClient&) = delete;

    // Starts connecting (returns straight away; watch takeWelcome / takeDisconnect).
    // When the server accepts, `world` is reset and switched to remote mode.
    bool connect(World& world, const std::string& address, const std::string& playerName,
                 const std::string& gameFolder, int viewDistance, std::string& error);
    // Leaves the server (no-op when not connected).
    void disconnect();

    State state() const { return m_state; }
    bool active() const { return m_state != State::Idle; }
    const std::string& address() const { return m_addressText; }

    // Call every frame while connecting or joined.
    void update(const glm::vec3& pos, float yaw, float pitch, float dt);

    // True once, when the server has accepted us.
    bool takeWelcome(Welcome& out);
    // True once, when the connection failed or ended (reason says why).
    bool takeDisconnect(std::string& reason);

    const net::PlayerMap& players() const { return m_views; }

    // Your ping to the server in ms (-1 when not connected), and your player id there.
    int ping() const;
    uint16_t playerId() const { return m_welcome.playerId; }

    // Lines for /netstats: ping to the server and updates per second from each player.
    std::vector<std::string> statsLines() const;

    void sendChat(const std::string& text);
    std::vector<net::ChatLine> takeChat();

private:
    void handlePacket(const uint8_t* data, size_t size);
    void finish(const std::string& reason);
    void sendEdits();

    ENetHost* m_host = nullptr;
    ENetPeer* m_peer = nullptr;
    World* m_world = nullptr;
    State m_state = State::Idle;
    std::string m_addressText;
    std::string m_name;
    std::string m_game;
    int m_viewDistance = 8;
    double m_connectStarted = 0.0;

    uint8_t m_fromServer[256] = {};   // server block id -> our id
    uint8_t m_toServer[256] = {};     // our block id -> server block id
    bool m_haveWelcome = false;
    Welcome m_welcome;
    bool m_ended = false;
    std::string m_endReason;
    std::string m_kickReason;

    net::PlayerMap m_views;
    std::vector<net::ChatLine> m_chat;
    float m_posTimer = 0.0f;
};

// ===========================================================================
// Server list status ("is it online, what's it about, who's on")
// ===========================================================================
struct ServerStatus {
    enum class State { Pinging, Online, Offline } state = State::Pinging;
    std::string motd;
    std::string version;         // the server's kVersionName
    std::string game;            // game folder the server is playing
    int players = 0, maxPlayers = 0;
    std::vector<std::string> names;   // up to 20 player names (hover in the list)
    int ping = -1;               // ms
    bool compatible = true;      // same protocol version
    std::string error;           // why it's offline
};

class ServerPinger {
public:
    ~ServerPinger();
    // Starts (or restarts) checking `address`. Results appear in status(address).
    void ping(const std::string& address);
    // Call every frame while the server list is open.
    void update();
    // nullptr if this address was never pinged.
    const ServerStatus* status(const std::string& address) const;
    void clear();

private:
    struct Query {
        ENetHost* host = nullptr;
        ENetPeer* peer = nullptr;
        double started = 0.0;
        double connectedAt = 0.0;
        ServerStatus result;
    };
    void finish(Query& q);
    std::unordered_map<std::string, Query> m_queries;
};
