// enet includes <windows.h> on Windows; keep it from defining min/max/near/far macros.
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#endif
#include <enet/enet.h>

#include "Net.h"
#include "ChunkData.h"
#include "Block.h"
#include "Types.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>

// ===========================================================================
// Protocol
//
// Every packet starts with a u8 message type. Integers are little-endian, floats are
// IEEE-754 32-bit, strings are u16 length + bytes. Blocks travel by NAME (not id), so
// the server and clients don't need identical block ids - only the same mods.
//
// Channel 0 (reliable, in order): everything except positions.
// Channel 1 (unreliable, sequenced): player positions.
// ===========================================================================
namespace {

enum Msg : uint8_t {
    // client -> server
    C_HELLO   = 1,    // u32 protocol, str name, str game, u8 viewDistance
    C_POS     = 2,    // f32 x y z yaw pitch time
    C_BLOCKS  = 3,    // u16 count, { i32 x y z, str block, u8 meta }
    C_CHAT    = 4,    // str text

    // server -> client
    S_WELCOME = 64,   // u16 yourId, u32 seed, f32 spawn x y z, str hostName
    S_KICK    = 65,   // str reason
    S_CHUNK   = 66,   // i32 cx cz, rest = chunk bytes (World::encodeChunk)
    S_UNLOAD  = 67,   // i32 cx cz
    S_BLOCKS  = 68,   // u16 count, { i32 x y z, str block, u8 meta }
    S_ADD     = 69,   // u16 id, str name
    S_REMOVE  = 70,   // u16 id
    S_PLAYERS = 71,   // u16 count, { u16 id, f32 x y z yaw pitch time }
    S_CHAT    = 72,   // str text, f32 r g b
    S_PINGS   = 73,   // u16 count, { u16 id, u16 ping ms }   (for the player list)
    S_STATUS  = 74,   // u32 protocol, str version, str game, str motd, u16 online, u16 max,
                      // u16 nameCount, { str name }    (server list; sent instead of joining)
};

constexpr int   kChunksPerUpdate     = 4;            // per client per frame
constexpr size_t kMaxChunkBytesInFlight = 512 * 1024; // don't queue more than this per client
constexpr float kPositionInterval    = 1.0f / 30.0f; // 30 position updates a second
constexpr float kMaxReach            = 10.0f;        // blocks a player may edit from
constexpr double kConnectTimeoutSec  = 10.0;

const glm::vec3 kInfoColor(1.0f, 1.0f, 0.4f);
const glm::vec3 kChatColor(1.0f, 1.0f, 1.0f);

using Clock = std::chrono::steady_clock;
double nowSeconds() {
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

inline int floorDiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}
inline int positiveMod(int a, int b) { int m = a % b; return m < 0 ? m + b : m; }

World::ChunkCoord chunkOf(const glm::vec3& p) {
    return {floorDiv(static_cast<int>(std::floor(p.x)), CHUNK_WIDTH),
            floorDiv(static_cast<int>(std::floor(p.z)), CHUNK_WIDTH)};
}

// --- Writing / reading packets ---------------------------------------------
struct Writer {
    std::vector<uint8_t> d;
    explicit Writer(uint8_t type) { d.push_back(type); }
    void u8(uint8_t v) { d.push_back(v); }
    void u16(uint16_t v) { d.push_back(v & 0xFF); d.push_back(v >> 8); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) d.push_back((v >> (8 * i)) & 0xFF); }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v) { uint32_t u; std::memcpy(&u, &v, 4); u32(u); }
    void str(const std::string& s) {
        const size_t n = std::min<size_t>(s.size(), 0xFFFF);
        u16(static_cast<uint16_t>(n));
        d.insert(d.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
    }
    void bytes(const std::vector<uint8_t>& b) { d.insert(d.end(), b.begin(), b.end()); }
};

struct Reader {
    const uint8_t* d; size_t n; size_t p = 1; bool ok = true;   // p = 1 skips the type byte
    Reader(const uint8_t* data, size_t size) : d(data), n(size) {}
    bool need(size_t k) { if (!ok || p + k > n) { ok = false; return false; } return true; }
    uint8_t u8() { if (!need(1)) return 0; return d[p++]; }
    uint16_t u16() { if (!need(2)) return 0; uint16_t v = d[p] | (d[p + 1] << 8); p += 2; return v; }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(d[p + i]) << (8 * i);
        p += 4;
        return v;
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    float f32() { uint32_t u = u32(); float v; std::memcpy(&v, &u, 4); return std::isfinite(v) ? v : 0.0f; }
    std::string str() {
        const uint16_t len = u16();
        if (!need(len)) return {};
        std::string s(reinterpret_cast<const char*>(d + p), len);
        p += len;
        return s;
    }
    std::vector<uint8_t> rest() { std::vector<uint8_t> v(d + p, d + n); p = n; return v; }
    // Read in order. (Never write glm::vec3(r.f32(), r.f32(), r.f32()) - C++ doesn't say which
    // argument is evaluated first, and GCC goes right-to-left, which would swap x and z.)
    glm::vec3 vec3f() { const float x = f32(); const float y = f32(); const float z = f32(); return {x, y, z}; }
    glm::ivec3 ivec3() { const int x = i32(); const int y = i32(); const int z = i32(); return {x, y, z}; }
};

void sendTo(ENetPeer* peer, const Writer& w, bool reliable = true) {
    if (!peer) return;
    ENetPacket* pkt = enet_packet_create(w.d.data(), w.d.size(),
                                         reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    enet_peer_send(peer, reliable ? 0 : 1, pkt);
}

std::string blockName(uint8_t id) {
    return id == 0 ? std::string() : BlockRegistry::getInstance().getBlock(id).name;
}

// "" = air. Returns false for a block this game doesn't have.
bool blockIdFromName(const std::string& name, uint8_t& id) {
    if (name.empty()) { id = 0; return true; }
    id = BlockRegistry::getInstance().getBlockId(name);
    return id != 0;
}

// Same colour for the same name everywhere.
glm::vec3 colorForName(const std::string& name) {
    uint32_t h = 2166136261u;
    for (unsigned char ch : name) { h ^= ch; h *= 16777619u; }
    const float hue = static_cast<float>(h % 360) / 60.0f;
    const float x = 1.0f - std::fabs(std::fmod(hue, 2.0f) - 1.0f);
    glm::vec3 c;
    if (hue < 1) c = {1, x, 0}; else if (hue < 2) c = {x, 1, 0}; else if (hue < 3) c = {0, 1, x};
    else if (hue < 4) c = {0, x, 1}; else if (hue < 5) c = {x, 0, 1}; else c = {1, 0, x};
    return c * 0.65f + glm::vec3(0.2f);
}

// Moves smoothed player views toward their latest known position.
// Where another player probably is now: their last known position, moved on by their
// recent speed for a short while (so they don't trail behind between updates), then
// eased toward smoothly.
void smoothPlayers(net::PlayerMap& views, float dt) {
    const double now = nowSeconds();
    const float t = 1.0f - std::exp(-dt * 20.0f);
    for (auto& [id, v] : views) {
        if (!v.hasPosition) continue;
        const float age = static_cast<float>(now - v.lastUpdate);
        const glm::vec3 predicted = v.target + v.velocity * std::min(age, 0.12f);
        if (glm::distance(v.position, predicted) > 8.0f) v.position = predicted;   // teleported
        v.position += (predicted - v.position) * t;
        const float dy = std::fmod(v.targetYaw - v.yaw + 540.0f, 360.0f) - 180.0f;   // shortest turn
        v.yaw += dy * t;
        v.pitch += (v.targetPitch - v.pitch) * t;
    }
}

// A new position for another player. `senderTime` is the sender's own clock when it was
// recorded, so speed is worked out from when they really moved, not from when the packets
// happened to arrive (several can arrive in one frame on a slow or busy PC).
void setTarget(net::PlayerView& v, const glm::vec3& p, float yaw, float pitch, float senderTime) {
    const double now = nowSeconds();
    if (v.hasPosition) {
        const float gap = senderTime - v.lastSenderTime;
        if (gap <= 0.0f && gap > -5.0f) return;   // older than what we have (arrived out of order)
        if (gap > 0.0f && gap < 0.5f) {
            const glm::vec3 vel = (p - v.target) / gap;
            // Blend with the previous estimate so one odd packet doesn't cause a jump
            v.velocity = glm::length(vel) > 40.0f ? glm::vec3(0.0f) : glm::mix(v.velocity, vel, 0.6f);
        } else {
            v.velocity = glm::vec3(0.0f);   // long gap, or the sender restarted
        }
    } else {
        v.position = p;
        v.yaw = yaw;
        v.pitch = pitch;
        v.hasPosition = true;
    }
    v.target = p;
    v.targetYaw = yaw;
    v.targetPitch = pitch;
    v.lastUpdate = now;
    v.lastSenderTime = senderTime;

    // Updates per second, for /netstats
    ++v.updatesCounted;
    if (now - v.rateWindowStart >= 1.0) {
        v.updateRate = static_cast<float>(v.updatesCounted / (now - v.rateWindowStart));
        v.updatesCounted = 0;
        v.rateWindowStart = now;
    }
}

// ENet slows down ("throttles") unreliable packets when the connection looks busy -
// e.g. while chunks are streaming - and silently drops positions, which makes other
// players stutter. Positions are tiny, so turn that off: always send them all.
void disableThrottle(ENetPeer* peer) {
    enet_peer_throttle_configure(peer, ENET_PEER_PACKET_THROTTLE_INTERVAL, 0, 0);
}

// enet_initialize once for the whole program.
bool ensureEnet() {
    static bool ok = false, tried = false;
    if (!tried) {
        tried = true;
        ok = enet_initialize() == 0;
        if (ok) std::atexit(enet_deinitialize);
        else std::cerr << "[Net] Could not start networking (enet_initialize failed)" << std::endl;
    }
    return ok;
}

// Chunk offsets sorted nearest-first, so chunk streaming sends close chunks first.
const std::vector<std::pair<int, int>>& ringOffsets() {
    static std::vector<std::pair<int, int>> offsets = [] {
        std::vector<std::pair<int, int>> v;
        const int r = net::kMaxViewDistance;
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz) v.push_back({dx, dz});
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
            return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second;
        });
        return v;
    }();
    return offsets;
}

} // namespace

// ===========================================================================
// Helpers
// ===========================================================================
float net::clockSeconds() {
    static const double start = nowSeconds();
    return static_cast<float>(nowSeconds() - start);
}

bool net::parseAddress(const std::string& text, std::string& host, uint16_t& port) {
    std::string s = text;
    s.erase(0, s.find_first_not_of(" \t"));
    s.erase(s.find_last_not_of(" \t") + 1);
    if (s.empty()) return false;

    port = kDefaultPort;
    const size_t colon = s.rfind(':');
    if (colon != std::string::npos) {
        const std::string portText = s.substr(colon + 1);
        s = s.substr(0, colon);
        if (portText.empty() || portText.size() > 5 ||
            portText.find_first_not_of("0123456789") != std::string::npos) return false;
        const int p = std::stoi(portText);
        if (p <= 0 || p > 65535) return false;
        port = static_cast<uint16_t>(p);
    }
    host = s;
    return !host.empty();
}

bool net::validPlayerName(const std::string& name) {
    if (name.empty() || name.size() > 16) return false;
    for (char ch : name) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-')) return false;
    }
    return true;
}

// ===========================================================================
// GameServer
// ===========================================================================
// At program exit the World may already be gone, so only close the socket here
// (call stop() first for a clean shutdown).
GameServer::~GameServer() {
    if (m_host) enet_host_destroy(m_host);
}

bool GameServer::start(World& world, uint16_t port, const std::string& gameFolder,
                       const std::string& hostName, std::string& error) {
    if (running()) { error = "Already hosting"; return false; }
    if (!ensureEnet()) { error = "Networking could not be started"; return false; }

    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = port;
    // Room for max players plus a few extra connections (status checks, people being
    // told the server is full). ENet allows at most 4095.
    const size_t peers = static_cast<size_t>(std::clamp(m_maxPlayers + 16, 32, 4095));
    m_host = enet_host_create(&addr, peers, 2, 0, 0);
    if (!m_host) {
        error = "Port " + std::to_string(port) + " is already in use";
        return false;
    }

    m_world = &world;
    m_port = port;
    m_game = gameFolder;
    m_hostName = hostName;
    m_nextId = 1;
    m_clients.clear();
    m_views.clear();
    m_chat.clear();
    m_world->setRecordChanges(true);
    std::cout << "[Server] Hosting on port " << port << std::endl;
    return true;
}

void GameServer::stop(const std::string& reason) {
    if (!m_host) return;
    for (auto& [id, c] : m_clients) {
        Writer w(S_KICK);
        w.str(reason);
        sendTo(c.peer, w);
        enet_peer_disconnect_later(c.peer, 0);
    }
    // Give the goodbye packets a moment to leave
    enet_host_flush(m_host);
    ENetEvent ev;
    const double until = nowSeconds() + 0.3;
    while (!m_clients.empty() && nowSeconds() < until && enet_host_service(m_host, &ev, 20) >= 0) {
        if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
        if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
            if (Client* c = findByPeer(ev.peer)) m_clients.erase(c->id);
        }
    }
    enet_host_destroy(m_host);
    m_host = nullptr;
    m_clients.clear();
    m_views.clear();
    if (m_world) {
        m_world->setRecordChanges(false);
        m_world->setExtraLoadCenters({});
    }
    m_world = nullptr;
    std::cout << "[Server] Stopped" << std::endl;
}

int GameServer::playerCount() const {
    int n = 1;
    for (const auto& [id, c] : m_clients) if (c.joined) ++n;
    return n;
}

GameServer::Client* GameServer::findByPeer(ENetPeer* peer) {
    const uint16_t id = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(peer->data));
    auto it = m_clients.find(id);
    return (it != m_clients.end() && it->second.peer == peer) ? &it->second : nullptr;
}

void GameServer::update(const glm::vec3& hostPos, float hostYaw, float hostPitch, float dt) {
    if (!m_host) return;
    m_hostPos = hostPos;
    m_hostYaw = hostYaw;
    m_hostPitch = hostPitch;

    // 1. Network events
    ENetEvent ev;
    while (m_host && enet_host_service(m_host, &ev, 0) > 0) {
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            if (ev.data == net::kStatusQuery) {
                // Server list check: answer and hang up (not a player)
                ev.peer->data = nullptr;
                sendStatus(ev.peer);
                enet_peer_disconnect_later(ev.peer, 0);
                break;
            }
            const uint16_t id = m_nextId++;
            if (m_nextId == 0) m_nextId = 1;
            Client c;
            c.peer = ev.peer;
            c.id = id;
            ev.peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(id));
            disableThrottle(ev.peer);
            m_clients[id] = std::move(c);
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE:
            if (Client* c = findByPeer(ev.peer)) handlePacket(*c, ev.packet->data, ev.packet->dataLength);
            enet_packet_destroy(ev.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            if (Client* c = findByPeer(ev.peer)) dropClient(c->id);
            break;
        default:
            break;
        }
    }

    // 2. Keep chunks loaded around every joined player
    std::vector<World::LoadCenter> centers;
    for (const auto& [id, c] : m_clients) {
        if (c.joined && c.hasPosition) centers.push_back({c.position, c.viewDistance});
    }
    m_world->setExtraLoadCenters(centers);

    // 3. Block changes (player edits, water, mods) -> everyone who has that chunk
    sendBlockChanges(m_world->takeBlockChanges());

    // 4. Chunks
    for (auto& [id, c] : m_clients) {
        if (c.joined && c.hasPosition) streamChunks(c);
    }

    // 5. Positions, 20 times a second
    m_posTimer += dt;
    if (m_posTimer >= kPositionInterval) {
        m_posTimer = std::fmod(m_posTimer, kPositionInterval);
        sendPositions();
    }

    // Pings for the player list (Tab), every 2 seconds
    m_pingTimer += dt;
    if (m_pingTimer >= 2.0f) {
        m_pingTimer = 0.0f;
        Writer w(S_PINGS);
        uint16_t n = 0;
        w.u16(0);
        w.u16(0); w.u16(0); ++n;   // the host
        for (const auto& [id, c] : m_clients) {
            if (!c.joined) continue;
            const int ping = static_cast<int>(std::min<enet_uint32>(c.peer->roundTripTime, 65535));
            w.u16(id); w.u16(static_cast<uint16_t>(ping)); ++n;
            auto it = m_views.find(id);
            if (it != m_views.end()) it->second.ping = ping;
        }
        w.d[1] = n & 0xFF;
        w.d[2] = n >> 8;
        for (auto& [id, c] : m_clients) {
            if (c.joined) sendTo(c.peer, w);
        }
    }

    smoothPlayers(m_views, dt);
    enet_host_flush(m_host);
}

void GameServer::handlePacket(Client& c, const uint8_t* data, size_t size) {
    if (size == 0) return;
    const uint8_t type = data[0];

    if (!c.joined) {
        if (type == C_HELLO) handleHello(c, data, size);
        return;
    }

    switch (type) {
    case C_POS: {
        Reader r(data, size);
        glm::vec3 p = r.vec3f();
        const float yaw = r.f32();
        const float pitch = r.f32();
        const float time = r.f32();
        if (!r.ok) return;
        p.y = std::clamp(p.y, -64.0f, static_cast<float>(CHUNK_HEIGHT) + 64.0f);
        c.position = p;
        c.yaw = yaw;
        c.pitch = pitch;
        c.sentTime = time;
        c.hasPosition = true;
        setTarget(m_views[c.id], p, yaw, pitch, time);
        break;
    }
    case C_BLOCKS:
        handleBlocks(c, data, size);
        break;
    case C_CHAT: {
        Reader r(data, size);
        std::string text = r.str();
        if (!r.ok || text.empty()) return;
        if (text.size() > 256) text.resize(256);
        broadcastChat("<" + c.name + "> " + text, kChatColor);
        break;
    }
    default:
        break;
    }
}

void GameServer::handleHello(Client& c, const uint8_t* data, size_t size) {
    Reader r(data, size);
    const uint32_t protocol = r.u32();
    const std::string name = r.str();
    const std::string game = r.str();
    const int view = r.u8();
    if (!r.ok) { kick(c, "Bad hello packet"); return; }

    if (protocol != net::kProtocolVersion) {
        kick(c, protocol < net::kProtocolVersion ? "Your game is older than the server - please update"
                                                 : "The server is running an older version");
        return;
    }
    if (!net::validPlayerName(name)) { kick(c, "Invalid name (use 1-16 letters, numbers, _ or -)"); return; }
    if (game != m_game) { kick(c, "This server is playing '" + m_game + "' - select that game first"); return; }
    if (name == m_hostName) { kick(c, "Someone called '" + name + "' is already playing"); return; }
    if (playerCount() >= m_maxPlayers) {
        kick(c, "The server is full (" + std::to_string(m_maxPlayers) + " players)");
        return;
    }
    for (const auto& [id, other] : m_clients) {
        if (other.joined && other.name == name) { kick(c, "Someone called '" + name + "' is already playing"); return; }
    }

    c.name = name;
    c.viewDistance = std::clamp(view, 2, net::kMaxViewDistance);
    c.joined = true;

    // Spawn: on the ground at the world spawn if it's loaded, else next to the host
    glm::vec3 spawn = m_hostPos + glm::vec3(1.0f, 0.0f, 1.0f);
    const int ground = m_world->groundHeightAt(8, 8);
    if (ground >= 0) spawn = glm::vec3(8.5f, static_cast<float>(ground), 8.5f);
    c.position = spawn;
    c.hasPosition = true;

    Writer w(S_WELCOME);
    w.u16(c.id);
    w.u32(m_world->seed());
    w.f32(spawn.x); w.f32(spawn.y); w.f32(spawn.z);
    w.str(m_hostName);
    sendTo(c.peer, w);

    // Tell the newcomer who's here, and everyone else about the newcomer
    {
        Writer host(S_ADD);
        host.u16(0);
        host.str(m_hostName);
        sendTo(c.peer, host);
    }
    for (const auto& [id, other] : m_clients) {
        if (!other.joined || id == c.id) continue;
        Writer a(S_ADD); a.u16(id); a.str(other.name); sendTo(c.peer, a);
        Writer b(S_ADD); b.u16(c.id); b.str(c.name); sendTo(other.peer, b);
    }

    net::PlayerView& v = m_views[c.id];
    v.name = name;
    v.color = colorForName(name);
    setTarget(v, spawn, 0.0f, 0.0f, 0.0f);

    std::cout << "[Server] " << name << " joined" << std::endl;
    broadcastChat(name + " joined the game", kInfoColor);
}

void GameServer::handleBlocks(Client& c, const uint8_t* data, size_t size) {
    Reader r(data, size);
    const uint16_t count = r.u16();
    std::vector<World::BlockChange> rejected;

    for (uint16_t i = 0; i < count && r.ok; ++i) {
        const glm::ivec3 pos = r.ivec3();
        const std::string name = r.str();
        const uint8_t meta = r.u8();
        if (!r.ok) break;

        uint8_t id = 0;
        const glm::vec3 eye = c.position + glm::vec3(0.0f, 1.62f, 0.0f);
        const bool inReach = glm::distance(eye, glm::vec3(pos) + glm::vec3(0.5f)) <= kMaxReach;
        const bool loaded = m_world->chunkData(chunkOf(glm::vec3(pos))) != nullptr &&
                            pos.y >= 0 && pos.y < CHUNK_HEIGHT;

        if (blockIdFromName(name, id) && inReach && loaded) {
            m_world->setBlockAt(pos, id);
            if (m_world->getMetadataAt(pos) != meta) m_world->setMetadataAt(pos, meta);
        } else if (loaded) {
            // Undo the player's prediction: send them what's really there
            rejected.push_back({pos, m_world->getBlockAt(pos), m_world->getMetadataAt(pos)});
        }
    }

    if (!rejected.empty()) {
        Writer w(S_BLOCKS);
        w.u16(static_cast<uint16_t>(rejected.size()));
        for (const auto& ch : rejected) {
            w.i32(ch.pos.x); w.i32(ch.pos.y); w.i32(ch.pos.z);
            w.str(blockName(ch.id));
            w.u8(ch.meta);
        }
        sendTo(c.peer, w);
    }
}

void GameServer::kick(Client& c, const std::string& reason) {
    Writer w(S_KICK);
    w.str(reason);
    sendTo(c.peer, w);
    enet_peer_disconnect_later(c.peer, 0);
    std::cout << "[Server] Kicked " << (c.name.empty() ? "a player" : c.name) << ": " << reason << std::endl;
}

void GameServer::dropClient(uint16_t id) {
    auto it = m_clients.find(id);
    if (it == m_clients.end()) return;
    const bool wasJoined = it->second.joined;
    const std::string name = it->second.name;
    m_clients.erase(it);
    m_views.erase(id);

    if (wasJoined) {
        for (auto& [oid, other] : m_clients) {
            if (!other.joined) continue;
            Writer w(S_REMOVE);
            w.u16(id);
            sendTo(other.peer, w);
        }
        std::cout << "[Server] " << name << " left" << std::endl;
        broadcastChat(name + " left the game", kInfoColor);
    }
}

void GameServer::streamChunks(Client& c) {
    const World::ChunkCoord center = chunkOf(c.position);
    const int view = c.viewDistance;

    // Drop chunks that are now too far away
    for (auto it = c.sentChunks.begin(); it != c.sentChunks.end();) {
        const int dx = it->first - center.first, dz = it->second - center.second;
        if (std::max(std::abs(dx), std::abs(dz)) > view + 1) {
            Writer w(S_UNLOAD);
            w.i32(it->first); w.i32(it->second);
            sendTo(c.peer, w);
            it = c.sentChunks.erase(it);
        } else {
            ++it;
        }
    }

    // Send the nearest loaded chunks they don't have yet
    int sent = 0;
    for (const auto& [dx, dz] : ringOffsets()) {
        if (sent >= kChunksPerUpdate) break;
        if (c.peer->reliableDataInTransit > kMaxChunkBytesInFlight) break;
        if (std::max(std::abs(dx), std::abs(dz)) > view) continue;

        const World::ChunkCoord cc{center.first + dx, center.second + dz};
        if (c.sentChunks.count(cc)) continue;
        const ChunkData* data = m_world->chunkData(cc);
        if (!data) continue;   // not generated yet - next time

        Writer w(S_CHUNK);
        w.i32(cc.first); w.i32(cc.second);
        w.bytes(World::encodeChunk(*data));
        sendTo(c.peer, w);
        c.sentChunks.insert(cc);
        ++sent;
    }
}

void GameServer::sendBlockChanges(const std::vector<World::BlockChange>& changes) {
    if (changes.empty()) return;
    for (auto& [id, c] : m_clients) {
        if (!c.joined) continue;
        std::vector<const World::BlockChange*> mine;
        for (const auto& ch : changes) {
            if (c.sentChunks.count(chunkOf(glm::vec3(ch.pos)))) mine.push_back(&ch);
        }
        // Several packets if a lot changed at once
        for (size_t start = 0; start < mine.size(); start += 4096) {
            const size_t end = std::min(mine.size(), start + 4096);
            Writer w(S_BLOCKS);
            w.u16(static_cast<uint16_t>(end - start));
            for (size_t i = start; i < end; ++i) {
                w.i32(mine[i]->pos.x); w.i32(mine[i]->pos.y); w.i32(mine[i]->pos.z);
                w.str(blockName(mine[i]->id));
                w.u8(mine[i]->meta);
            }
            sendTo(c.peer, w);
        }
    }
}

void GameServer::sendPositions() {
    struct Entry { uint16_t id; glm::vec3 p; float yaw, pitch, time; };
    std::vector<Entry> all;
    all.push_back({0, m_hostPos, m_hostYaw, m_hostPitch, net::clockSeconds()});
    for (const auto& [id, c] : m_clients) {
        if (c.joined && c.hasPosition) all.push_back({id, c.position, c.yaw, c.pitch, c.sentTime});
    }

    for (auto& [id, c] : m_clients) {
        if (!c.joined) continue;
        const float range = (c.viewDistance + 1) * static_cast<float>(CHUNK_WIDTH);
        Writer w(S_PLAYERS);
        w.u16(0);   // count, filled in below
        uint16_t n = 0;
        for (const Entry& e : all) {
            if (e.id == id) continue;
            const glm::vec2 d(e.p.x - c.position.x, e.p.z - c.position.z);
            if (glm::length(d) > range) continue;   // too far to see
            w.u16(e.id);
            w.f32(e.p.x); w.f32(e.p.y); w.f32(e.p.z);
            w.f32(e.yaw); w.f32(e.pitch);
            w.f32(e.time);
            ++n;
        }
        if (n == 0) continue;
        w.d[1] = n & 0xFF;
        w.d[2] = n >> 8;
        sendTo(c.peer, w, false);
    }
}

void GameServer::broadcastChat(const std::string& text, const glm::vec3& color) {
    m_chat.push_back({text, color});
    for (auto& [id, c] : m_clients) {
        if (!c.joined) continue;
        Writer w(S_CHAT);
        w.str(text);
        w.f32(color.r); w.f32(color.g); w.f32(color.b);
        sendTo(c.peer, w);
    }
}

void GameServer::setInfo(const std::string& motd, int maxPlayers) {
    m_motd = motd.substr(0, net::kMaxMotdLength);
    m_maxPlayers = std::clamp(maxPlayers, 2, 4000);
}

void GameServer::sendStatus(ENetPeer* peer) {
    Writer w(S_STATUS);
    w.u32(net::kProtocolVersion);
    w.str(net::kVersionName);
    w.str(m_game);
    w.str(m_motd);
    w.u16(static_cast<uint16_t>(playerCount()));
    w.u16(static_cast<uint16_t>(m_maxPlayers));
    std::vector<std::string> names{m_hostName};
    for (const auto& [id, c] : m_clients) {
        if (c.joined && names.size() < 20) names.push_back(c.name);
    }
    w.u16(static_cast<uint16_t>(names.size()));
    for (const auto& n : names) w.str(n);
    sendTo(peer, w);
}

void GameServer::sendChat(const std::string& text) {
    if (!running() || text.empty()) return;
    broadcastChat("<" + m_hostName + "> " + text, kChatColor);
}

std::vector<net::ChatLine> GameServer::takeChat() {
    std::vector<net::ChatLine> out;
    out.swap(m_chat);
    return out;
}

// ===========================================================================
// GameClient
// ===========================================================================
GameClient::~GameClient() {
    if (m_host) enet_host_destroy(m_host);
}

bool GameClient::connect(World& world, const std::string& address, const std::string& playerName,
                         const std::string& gameFolder, int viewDistance, std::string& error) {
    disconnect();
    m_ended = false;
    m_endReason.clear();
    m_kickReason.clear();
    m_haveWelcome = false;

    if (!net::validPlayerName(playerName)) {
        error = "Pick a name first (1-16 letters, numbers, _ or -)";
        return false;
    }
    std::string hostText;
    uint16_t port = 0;
    if (!net::parseAddress(address, hostText, port)) {
        error = "That isn't a valid address (example: 192.168.1.20 or myserver.net:30000)";
        return false;
    }
    if (!ensureEnet()) { error = "Networking could not be started"; return false; }

    ENetAddress addr;
    if (enet_address_set_host(&addr, hostText.c_str()) != 0) {
        error = "Could not find '" + hostText + "'";
        return false;
    }
    addr.port = port;

    m_host = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!m_host) { error = "Could not open a network socket"; return false; }
    m_peer = enet_host_connect(m_host, &addr, 2, 0);
    if (!m_peer) {
        enet_host_destroy(m_host);
        m_host = nullptr;
        error = "Could not start connecting";
        return false;
    }

    m_world = &world;
    m_addressText = hostText + ":" + std::to_string(port);
    m_name = playerName;
    m_game = gameFolder;
    m_viewDistance = std::clamp(viewDistance, 2, net::kMaxViewDistance);
    m_connectStarted = nowSeconds();
    m_state = State::Connecting;
    m_views.clear();
    m_chat.clear();
    std::cout << "[Client] Connecting to " << m_addressText << " as " << m_name << std::endl;
    return true;
}

void GameClient::disconnect() {
    if (!m_host) { m_state = State::Idle; return; }
    if (m_peer) {
        enet_peer_disconnect(m_peer, 0);
        ENetEvent ev;
        const double until = nowSeconds() + 0.3;
        bool done = false;
        while (!done && nowSeconds() < until && enet_host_service(m_host, &ev, 20) >= 0) {
            if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
            if (ev.type == ENET_EVENT_TYPE_DISCONNECT) done = true;
        }
        if (!done) enet_peer_reset(m_peer);
    }
    enet_host_destroy(m_host);
    m_host = nullptr;
    m_peer = nullptr;
    m_state = State::Idle;
    m_views.clear();
    std::cout << "[Client] Disconnected" << std::endl;
}

void GameClient::finish(const std::string& reason) {
    const bool wasJoined = m_state == State::Joined;
    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
        m_peer = nullptr;
    }
    m_state = State::Idle;
    m_views.clear();
    m_ended = true;
    if (!m_kickReason.empty()) m_endReason = m_kickReason;
    else m_endReason = wasJoined ? reason : "Could not connect to " + m_addressText +
                                            " (is the server running and the port open?)";
    std::cout << "[Client] " << m_endReason << std::endl;
}

void GameClient::update(const glm::vec3& pos, float yaw, float pitch, float dt) {
    if (!m_host) return;

    ENetEvent ev;
    while (m_host && enet_host_service(m_host, &ev, 0) > 0) {
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            disableThrottle(m_peer);
            Writer w(C_HELLO);
            w.u32(net::kProtocolVersion);
            w.str(m_name);
            w.str(m_game);
            w.u8(static_cast<uint8_t>(m_viewDistance));
            sendTo(m_peer, w);
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE:
            handlePacket(ev.packet->data, ev.packet->dataLength);
            enet_packet_destroy(ev.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            finish("Lost connection to the server");
            return;
        default:
            break;
        }
    }
    if (!m_host) return;

    if (m_state == State::Connecting && nowSeconds() - m_connectStarted > kConnectTimeoutSec) {
        finish("Timed out");
        return;
    }

    if (m_state == State::Joined) {
        sendEdits();
        m_posTimer += dt;
        if (m_posTimer >= kPositionInterval) {
            m_posTimer = std::fmod(m_posTimer, kPositionInterval);
            Writer w(C_POS);
            w.f32(pos.x); w.f32(pos.y); w.f32(pos.z);
            w.f32(yaw); w.f32(pitch);
            w.f32(net::clockSeconds());
            sendTo(m_peer, w, false);
        }
        smoothPlayers(m_views, dt);
    }
    if (m_host) enet_host_flush(m_host);
}

void GameClient::sendEdits() {
    const auto changes = m_world->takeBlockChanges();
    if (changes.empty()) return;
    for (size_t start = 0; start < changes.size(); start += 4096) {
        const size_t end = std::min(changes.size(), start + 4096);
        Writer w(C_BLOCKS);
        w.u16(static_cast<uint16_t>(end - start));
        for (size_t i = start; i < end; ++i) {
            const auto& ch = changes[i];
            w.i32(ch.pos.x); w.i32(ch.pos.y); w.i32(ch.pos.z);
            w.str(blockName(ch.id));
            w.u8(ch.meta);
        }
        sendTo(m_peer, w);
    }
}

void GameClient::handlePacket(const uint8_t* data, size_t size) {
    if (size == 0) return;
    Reader r(data, size);

    switch (data[0]) {
    case S_WELCOME: {
        Welcome wel;
        wel.playerId = r.u16();
        wel.seed = r.u32();
        wel.spawn = r.vec3f();
        wel.hostName = r.str();
        if (!r.ok) return;
        // Fresh world that only holds what the server sends
        m_world->reset();
        m_world->setSaveDirectory("");
        m_world->setSeed(wel.seed);
        m_world->setRemote(true);
        m_welcome = wel;
        m_haveWelcome = true;
        m_state = State::Joined;
        std::cout << "[Client] Joined " << m_addressText << std::endl;
        break;
    }
    case S_KICK:
        // The server is dropping us anyway - leave now with its reason
        m_kickReason = r.str();
        if (m_kickReason.empty()) m_kickReason = "Disconnected by the server";
        finish(m_kickReason);
        break;
    case S_CHUNK: {
        if (m_state != State::Joined) return;
        const int cx = r.i32(), cz = r.i32();
        if (!r.ok) return;
        auto chunk = std::make_shared<ChunkData>();
        if (World::decodeChunk(r.rest(), *chunk)) m_world->insertChunk(cx, cz, std::move(chunk));
        break;
    }
    case S_UNLOAD: {
        const int cx = r.i32(), cz = r.i32();
        if (r.ok && m_state == State::Joined) m_world->removeChunk(cx, cz);
        break;
    }
    case S_BLOCKS: {
        if (m_state != State::Joined) return;
        const uint16_t count = r.u16();
        for (uint16_t i = 0; i < count && r.ok; ++i) {
            const glm::ivec3 pos = r.ivec3();
            const std::string name = r.str();
            const uint8_t meta = r.u8();
            if (!r.ok) break;
            uint8_t id = 0;
            if (!blockIdFromName(name, id)) id = 0;   // block we don't have -> air
            m_world->applyNetworkBlock(pos, id, meta);
        }
        break;
    }
    case S_ADD: {
        const uint16_t id = r.u16();
        const std::string name = r.str();
        if (!r.ok) return;
        auto& v = m_views[id];
        v.name = name;
        v.color = colorForName(name);
        break;
    }
    case S_REMOVE: {
        const uint16_t id = r.u16();
        if (r.ok) m_views.erase(id);
        break;
    }
    case S_PLAYERS: {
        const uint16_t count = r.u16();
        for (uint16_t i = 0; i < count && r.ok; ++i) {
            const uint16_t id = r.u16();
            const glm::vec3 p = r.vec3f();
            const float yaw = r.f32();
            const float pitch = r.f32();
            const float time = r.f32();
            if (!r.ok) break;
            auto it = m_views.find(id);
            if (it != m_views.end()) setTarget(it->second, p, yaw, pitch, time);
        }
        break;
    }
    case S_PINGS: {
        const uint16_t count = r.u16();
        for (uint16_t i = 0; i < count && r.ok; ++i) {
            const uint16_t id = r.u16();
            const uint16_t ping = r.u16();
            if (!r.ok) break;
            auto it = m_views.find(id);
            if (it != m_views.end()) it->second.ping = ping;
        }
        break;
    }
    case S_CHAT: {
        net::ChatLine line;
        line.text = r.str();
        line.color = r.vec3f();
        if (r.ok) m_chat.push_back(line);
        break;
    }
    default:
        break;
    }
}

bool GameClient::takeWelcome(Welcome& out) {
    if (!m_haveWelcome) return false;
    m_haveWelcome = false;
    out = m_welcome;
    return true;
}

bool GameClient::takeDisconnect(std::string& reason) {
    if (!m_ended) return false;
    m_ended = false;
    reason = m_endReason;
    return true;
}

void GameClient::sendChat(const std::string& text) {
    if (m_state != State::Joined || text.empty()) return;
    Writer w(C_CHAT);
    w.str(text);
    sendTo(m_peer, w);
}

std::vector<net::ChatLine> GameClient::takeChat() {
    std::vector<net::ChatLine> out;
    out.swap(m_chat);
    return out;
}

// ===========================================================================
// /netstats
// ===========================================================================
namespace {
std::string rateText(const net::PlayerView& v) {
    // No update for over a second = the rate shown would be stale
    const double age = nowSeconds() - v.lastUpdate;
    const float rate = age > 1.5 ? 0.0f : v.updateRate;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f updates/s", rate);
    return buf;
}
}

std::vector<std::string> GameServer::statsLines() const {
    std::vector<std::string> out;
    out.push_back("Hosting on port " + std::to_string(m_port) + ", " + std::to_string(playerCount()) + " players");
    for (const auto& [id, c] : m_clients) {
        if (!c.joined) continue;
        std::string line = c.name + ": ping " + std::to_string(c.peer->roundTripTime) + " ms";
        auto it = m_views.find(id);
        if (it != m_views.end()) line += ", " + rateText(it->second) + " (expect ~30)";
        out.push_back(line);
    }
    return out;
}

int GameClient::ping() const {
    if (m_state != State::Joined || !m_peer) return -1;
    return static_cast<int>(m_peer->roundTripTime);
}

std::vector<std::string> GameClient::statsLines() const {
    std::vector<std::string> out;
    if (m_state != State::Joined || !m_peer) {
        out.push_back("Not connected to a server");
        return out;
    }
    out.push_back("Server " + m_addressText + ": ping " + std::to_string(m_peer->roundTripTime) + " ms");
    for (const auto& [id, v] : m_views) {
        out.push_back(v.name + ": " + rateText(v) + " (expect ~30)");
    }
    return out;
}

// ===========================================================================
// ServerPinger
// ===========================================================================
namespace {
constexpr double kStatusTimeoutSec = 5.0;
}

ServerPinger::~ServerPinger() { clear(); }

void ServerPinger::clear() {
    for (auto& [addr, q] : m_queries) {
        if (q.host) enet_host_destroy(q.host);
    }
    m_queries.clear();
}

void ServerPinger::finish(Query& q) {
    if (q.peer) enet_peer_reset(q.peer);
    if (q.host) enet_host_destroy(q.host);
    q.host = nullptr;
    q.peer = nullptr;
}

void ServerPinger::ping(const std::string& address) {
    Query& q = m_queries[address];
    finish(q);
    q.result = ServerStatus{};

    std::string hostText;
    uint16_t port = 0;
    ENetAddress addr;
    if (!net::parseAddress(address, hostText, port)) {
        q.result.state = ServerStatus::State::Offline;
        q.result.error = "Invalid address";
        return;
    }
    if (!ensureEnet() || enet_address_set_host(&addr, hostText.c_str()) != 0) {
        q.result.state = ServerStatus::State::Offline;
        q.result.error = "Unknown host";
        return;
    }
    addr.port = port;
    q.host = enet_host_create(nullptr, 1, 2, 0, 0);
    if (q.host) q.peer = enet_host_connect(q.host, &addr, 2, net::kStatusQuery);
    if (!q.host || !q.peer) {
        finish(q);
        q.result.state = ServerStatus::State::Offline;
        q.result.error = "Can't connect to server";
        return;
    }
    q.started = nowSeconds();
}

void ServerPinger::update() {
    for (auto& [address, q] : m_queries) {
        if (!q.host) continue;
        ENetEvent ev;
        while (q.host && enet_host_service(q.host, &ev, 0) > 0) {
            if (ev.type == ENET_EVENT_TYPE_CONNECT) {
                q.connectedAt = nowSeconds();
            } else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                Reader r(ev.packet->data, ev.packet->dataLength);
                if (ev.packet->dataLength > 0 && ev.packet->data[0] == S_STATUS) {
                    ServerStatus st;
                    const uint32_t protocol = r.u32();
                    st.version = r.str();
                    st.game = r.str();
                    st.motd = r.str();
                    st.players = r.u16();
                    st.maxPlayers = r.u16();
                    const uint16_t n = r.u16();
                    for (uint16_t i = 0; i < n && r.ok; ++i) st.names.push_back(r.str());
                    if (r.ok) {
                        st.state = ServerStatus::State::Online;
                        st.compatible = protocol == net::kProtocolVersion;
                        // The server answers the moment the handshake finishes: one round trip
                        st.ping = static_cast<int>((nowSeconds() - (q.connectedAt > 0 ? q.connectedAt : q.started)) * 1000.0);
                        q.result = st;
                    }
                }
                enet_packet_destroy(ev.packet);
                if (q.result.state == ServerStatus::State::Online) { finish(q); break; }
            } else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
                if (q.result.state != ServerStatus::State::Online) {
                    q.result.state = ServerStatus::State::Offline;
                    q.result.error = "Can't connect to server";
                }
                finish(q);
                break;
            }
        }
        if (q.host && nowSeconds() - q.started > kStatusTimeoutSec) {
            finish(q);
            q.result.state = ServerStatus::State::Offline;
            q.result.error = "Can't connect to server";
        }
    }
}

const ServerStatus* ServerPinger::status(const std::string& address) const {
    auto it = m_queries.find(address);
    return it != m_queries.end() ? &it->second.result : nullptr;
}
