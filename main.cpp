#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <cmath>
#include <iostream>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <exception>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <random>

#include "Types.h"
#include "Block.h"
#include "ChunkMesh.h"
#include "World.h"
#include "Player.h"
#include "Raycast.h"
#include "Hotbar.h"
#include "FontRenderer.h"
#include "ChatSystem.h"
#include "Lighting.h"
#include "Skybox.h"
#include "Model.h"
#include "Clouds.h"
#include "Fog.h"
#include "MainMenu.h"
#include "Inventory.h" // Include your new dedicated Inventory module
#include "CaveGenerator.h"
#include "Settings.h"
#include "ParticleSystem.h"

namespace fs = std::filesystem;

namespace {
    constexpr Dimensions2D baseResolution{1920, 1080};

    void scaleCursorToFramebuffer(GLFWwindow* window, double& x, double& y,
                                  int framebufferWidth, int framebufferHeight) {
        int windowWidth = 0;
        int windowHeight = 0;
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        if (windowWidth == 0 || windowHeight == 0) return;

        x *= static_cast<double>(framebufferWidth) / windowWidth;
        y *= static_cast<double>(framebufferHeight) / windowHeight;
    }
}

struct UnderwaterSettings {
    glm::vec3 shallowColor      = glm::vec3(0.16f, 0.38f, 0.72f);
    glm::vec3 deepColor         = glm::vec3(0.02f, 0.07f, 0.20f);
    float       shallowVisibility = 14.0f;
    float       deepVisibility    = 7.0f;
    float       fullDepth         = 40.0f;
    float       fadeInSeconds     = 0.6f;
    float       fadeOutSeconds    = 0.4f;
    float       tintStrength      = 0.45f;
};

std::string g_CurrentModPath = "";
std::string g_ActiveGameFolder = "craft_and_ruin";
std::string g_ActiveWorldName = "world_alpha";
std::string g_LoadedGameFolder;   // the game whose mods were loaded at startup (worlds are tied to it)
std::map<std::string, std::string> g_ModFileStatus;   // mod .lua file (canonical path) -> "" if it ran OK, else the error
Hotbar* g_Hotbar = nullptr;
FontRenderer* g_FontRenderer = nullptr;
ChatSystem* g_ChatSystem = nullptr;
LightingSystem* g_LightingSystem = nullptr;
Skybox* g_Skybox = nullptr;
Clouds* g_Clouds = nullptr;
World* g_WorldInstance = nullptr;
MainMenu* g_MainMenu = nullptr;
InventorySystem g_Inventory; // Dedicated C++ inventory system instance
float g_FieldOfView = 70.0f;
FogSettings g_Fog;
UnderwaterSettings g_Underwater;
float g_UnderwaterBlend = 0.0f;
bool g_Paused = false;   // pause menu open (drawn by pause_menu.lua)

// Loading screen state
bool g_SpawnPlaced = false;
double g_LoadStartTime = 0.0;
float g_LoadingProgress = 0.0f;
constexpr int kSpawnReadyRadius = 2;        // chunks around the spawn that must be ready
constexpr double kLoadTimeoutSec = 30.0;    // start anyway after this long

struct WaterColors {
    glm::vec3 shallow;
    glm::vec3 deep;
};

enum class GameState {
    MENU,
    WORLD_SELECT,   // singleplayer world list (drawn by world_select.lua)
    MODS,           // installed mods list (drawn by mods_menu.lua)
    TEXTURE_PACKS,  // texture pack picker (drawn by texture_packs.lua)
    SETTINGS,       // settings from the title screen (drawn by settings_menu.lua)
    LOADING,
    PLAYING
};
GameState g_CurrentGameState = GameState::MENU;

struct GameInfo {
    std::string folderName;
    std::string title = "Unnamed Game";
    std::string author = "Unknown";
    std::string description = "";
};

GameInfo parseGameConfig(const fs::path& gameDirPath) {
    GameInfo info;
    info.folderName = gameDirPath.filename().string();

    fs::path confPath = gameDirPath / "game.conf";
    if (!fs::exists(confPath)) {
        confPath = gameDirPath / "mod.conf";
    }
    if (!fs::exists(confPath)) {
        info.title = info.folderName;
        return info;
    }

    std::ifstream file(confPath);
    std::string line;
    while (std::getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty() || line[0] == '#') continue;

        size_t delimiter = line.find('=');
        if (delimiter != std::string::npos) {
            std::string key = line.substr(0, delimiter);
            std::string val = line.substr(delimiter + 1);

            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            val.erase(0, val.find_first_not_of(" \t"));
            val.erase(val.find_last_not_of(" \t") + 1);

            if (key == "title" || key == "name") info.title = val;
            else if (key == "author") info.author = val;
            else if (key == "description") info.description = val;
        }
    }
    return info;
}

std::vector<GameInfo> getAvailableGames() {
    std::vector<GameInfo> games;
    std::string gamesDir = "games";
    if (!fs::exists(gamesDir) && fs::exists("../../games")) gamesDir = "../../games";

    if (fs::exists(gamesDir)) {
        for (const auto& entry : fs::directory_iterator(gamesDir)) {
            if (entry.is_directory()) {
                fs::path confPath = entry.path() / "game.conf";
                if (fs::exists(confPath) || fs::exists(entry.path() / "mod.conf")) {
                    games.push_back(parseGameConfig(entry.path()));
                }
            }
        }
    }

    if (games.empty()) {
        games.push_back({"craft_and_ruin", "Craft & Ruin", "Brandon", "Default Voxel Survival Game"});
    }
    return games;
}

std::vector<std::string> getAvailableWorlds() {
    std::vector<std::string> worlds;
    std::string savesDir = "saves";
    if (!fs::exists(savesDir) && fs::exists("../../saves")) savesDir = "../../saves";

    if (fs::exists(savesDir)) {
        for (const auto& entry : fs::directory_iterator(savesDir)) {
            if (entry.is_directory()) {
                worlds.push_back(entry.path().filename().string());
            }
        }
    }
    if (worlds.empty()) worlds.push_back("world_alpha");
    return worlds;
}

// ===========================================================================
// World saves
//   saves/<name>/world.dat      seed, player position/look, hotbar (text key=value)
//   saves/<name>/chunks/*.bin   chunks that were changed (written by World)
// ===========================================================================

Player* g_PlayerInstance = nullptr;
bool g_PendingWorldOpen = false;          // open g_ActiveWorldName at the start of LOADING
double g_LastAutosave = 0.0;
constexpr double kAutosaveSeconds = 120.0;

fs::path savesRoot() {
    if (!fs::exists("saves") && fs::exists("../../saves")) return fs::path("../../saves");
    return fs::path("saves");
}

uint32_t randomSeed() {
    std::random_device rd;
    return rd() ^ (rd() << 16);
}

// Empty -> random. A number -> that number. Any other text -> a hash of it.
uint32_t seedFromText(std::string t) {
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
    if (t.empty()) return randomSeed();

    const bool numeric = t.size() <= 10 &&
        std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    if (numeric) {
        const unsigned long long v = std::stoull(t);
        if (v <= 0xFFFFFFFFull) return static_cast<uint32_t>(v);
    }

    uint32_t h = 2166136261u;   // FNV-1a
    for (unsigned char c : t) { h ^= c; h *= 16777619u; }
    return h;
}

// Keeps letters, digits, spaces, '-' and '_' so the name is a safe folder name.
std::string sanitizeWorldName(const std::string& raw) {
    std::string out;
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == ' ' || c == '-' || c == '_') out.push_back(static_cast<char>(c));
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    if (out.empty()) out = "New World";
    if (out.size() > 32) out.resize(32);
    return out;
}

struct WorldMeta {
    bool valid = false;
    uint32_t seed = 0;
    bool hasPlayer = false;
    glm::vec3 position{0.0f};
    float yaw = 0.0f, pitch = 0.0f;
    std::vector<std::pair<std::string, int>> hotbar;   // block name ("" = empty), count
    std::string game;                                  // games/<folder> this world belongs to ("" = older save)
};

bool readWorldMeta(const fs::path& file, WorldMeta& meta) {
    std::ifstream in(file);
    if (!in) return false;

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = line.substr(0, eq);
        std::istringstream val(line.substr(eq + 1));

        if (key == "seed") {
            val >> meta.seed;
            meta.valid = !val.fail();
        } else if (key == "player") {
            val >> meta.position.x >> meta.position.y >> meta.position.z;
            meta.hasPlayer = !val.fail();
        } else if (key == "look") {
            val >> meta.yaw >> meta.pitch;
        } else if (key == "game") {
            meta.game = line.substr(eq + 1);
        } else if (key.rfind("hotbar", 0) == 0) {
            const int idx = std::atoi(key.c_str() + 6);
            std::string name;
            int count = 0;
            val >> name >> count;
            if (idx >= 1 && idx <= 64) {
                if (static_cast<int>(meta.hotbar.size()) < idx) meta.hotbar.resize(idx);
                meta.hotbar[idx - 1] = { name == "-" ? "" : name, count };
            }
        }
    }
    return meta.valid;
}

bool writeWorldMeta(const fs::path& file, uint32_t seed, const Player* player, const std::string& game) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const fs::path tmp = file.string() + ".tmp";

    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << std::fixed << std::setprecision(3);
        out << "version=1\n";
        if (!game.empty()) out << "game=" << game << "\n";
        out << "seed=" << seed << "\n";
        if (player) {
            out << "player=" << player->position.x << " " << player->position.y << " " << player->position.z << "\n";
            out << "look=" << player->yaw << " " << player->pitch << "\n";
        }
        if (g_Hotbar) {
            for (int i = 0; i < g_Hotbar->slotCount; ++i) {
                const HotbarSlot s = g_Hotbar->getSlot(i);
                const std::string name = s.blockId ? BlockRegistry::getInstance().getBlock(s.blockId).name : "-";
                out << "hotbar" << (i + 1) << "=" << name << " " << (s.blockId ? s.count : 0) << "\n";
            }
        }
        if (!out) return false;
    }

    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(file, ec);
        fs::rename(tmp, file, ec);
    }
    return !ec;
}

// Writes changed chunks + world.dat for the world that is currently open.
void saveCurrentWorld() {
    World* w = g_WorldInstance;
    if (!w || w->saveDirectory().empty()) return;

    const int written = w->saveAll();
    const bool metaOk = writeWorldMeta(fs::path(w->saveDirectory()) / "world.dat", w->seed(),
                                       g_PlayerInstance, g_LoadedGameFolder);
    std::cout << "[Save] Saved '" << g_ActiveWorldName << "' (" << written << " changed chunks"
              << (metaOk ? "" : ", world.dat FAILED") << ")" << std::endl;
}

// Can this world be played with the game that is currently loaded?
// Older saves without a "game=" line belong to whichever game opens them first.
bool worldBelongsToLoadedGame(const WorldMeta& meta, std::string* why = nullptr) {
    if (meta.game.empty() || meta.game == g_LoadedGameFolder) return true;
    if (why) *why = "This world was made with '" + meta.game + "'. Restart the game with that game selected to play it.";
    return false;
}

// Opens g_ActiveWorldName: creates it if new, otherwise restores seed, player and hotbar.
// Returns false (and opens nothing) if the world belongs to a different game.
bool openActiveWorld(World& world, Player& player) {
    const fs::path dir = savesRoot() / g_ActiveWorldName;

    WorldMeta meta;
    const bool existing = readWorldMeta(dir / "world.dat", meta);

    std::string why;
    if (existing && !worldBelongsToLoadedGame(meta, &why)) {
        std::cerr << "[World] Refusing to open '" << g_ActiveWorldName << "': " << why << std::endl;
        return false;
    }

    std::error_code ec;
    fs::create_directories(dir / "chunks", ec);

    world.reset();
    world.setSaveDirectory(dir.string());
    world.setSeed(existing ? meta.seed : randomSeed());

    // Hotbar: empty, then restore what was saved
    if (g_Hotbar) {
        for (int i = 0; i < g_Hotbar->slotCount; ++i) g_Hotbar->setSlot(i, 0, 0);
        for (size_t i = 0; i < meta.hotbar.size() && static_cast<int>(i) < g_Hotbar->slotCount; ++i) {
            const auto& entry = meta.hotbar[i];
            if (entry.first.empty()) continue;
            const uint8_t id = BlockRegistry::getInstance().getBlockId(entry.first);
            if (id != 0) g_Hotbar->setSlot(static_cast<int>(i), id, entry.second > 0 ? entry.second : 64);
        }
    }

    // Player: saved position, or a fresh spawn (LOADING finds the ground)
    if (existing && meta.hasPlayer) {
        player.position = meta.position;
        player.yaw = meta.yaw;
        player.pitch = meta.pitch;
        g_SpawnPlaced = true;
    } else {
        player.position = glm::vec3(8.0f, 100.0f, 8.0f);
        g_SpawnPlaced = false;
    }

    // Make sure the seed is on disk straight away
    // (also stamps older saves with the game that opened them)
    writeWorldMeta(dir / "world.dat", world.seed(), (existing && meta.hasPlayer) ? &player : nullptr,
                   g_LoadedGameFolder);
    g_LastAutosave = glfwGetTime();

    std::cout << "[World] " << (existing ? "Loaded" : "Created") << " world '" << g_ActiveWorldName
              << "' (seed " << world.seed() << ", game " << g_LoadedGameFolder << ")" << std::endl;
    return true;
}

// Switches to the loading screen; the world itself is opened from the main loop.
void beginLoadingWorld(GLFWwindow* win) {
    g_CurrentGameState = GameState::LOADING;
    g_PendingWorldOpen = true;
    g_SpawnPlaced = false;
    g_LoadStartTime = glfwGetTime();
    g_LoadingProgress = 0.0f;
    g_Paused = false;
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

struct SavedWorldInfo {
    std::string name;
    std::string seed;
    std::string game;
    fs::file_time_type lastPlayed;
};

// Worlds for the game currently selected on the title screen, most recently played first.
// Older saves with no game recorded are shown for every game until one opens them.
std::vector<SavedWorldInfo> listSavedWorlds() {
    std::vector<SavedWorldInfo> list;
    const fs::path root = savesRoot();
    std::error_code ec;
    if (!fs::exists(root, ec)) return list;

    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory()) continue;
        SavedWorldInfo info;
        info.name = entry.path().filename().string();

        WorldMeta meta;
        const fs::path dat = entry.path() / "world.dat";
        info.seed = readWorldMeta(dat, meta) ? std::to_string(meta.seed) : "?";
        info.game = meta.game;
        if (!info.game.empty() && info.game != g_ActiveGameFolder) continue;   // another game's world

        info.lastPlayed = fs::exists(dat, ec) ? fs::last_write_time(dat, ec) : fs::last_write_time(entry.path(), ec);
        list.push_back(info);
    }

    std::sort(list.begin(), list.end(),
              [](const SavedWorldInfo& a, const SavedWorldInfo& b) { return a.lastPlayed > b.lastPlayed; });
    return list;
}

// Creates saves/<name>/ with a world.dat holding the seed. Returns the folder name used.
std::string createWorld(const std::string& rawName, const std::string& seedText) {
    const std::string base = sanitizeWorldName(rawName);
    std::string name = base;
    for (int n = 2; fs::exists(savesRoot() / name); ++n) {
        name = base + " (" + std::to_string(n) + ")";
    }

    std::error_code ec;
    fs::create_directories(savesRoot() / name / "chunks", ec);
    writeWorldMeta(savesRoot() / name / "world.dat", seedFromText(seedText), nullptr, g_LoadedGameFolder);
    std::cout << "[World] Created save folder '" << name << "' for game '" << g_LoadedGameFolder << "'" << std::endl;
    return name;
}

// Can the world list be used right now? Only if the selected game is the one whose mods are loaded.
bool selectedGameIsLoaded() {
    return g_ActiveGameFolder == g_LoadedGameFolder;
}

// Permanently deletes saves/<name>. Refuses anything that isn't a world folder directly
// inside saves/, a world from another game, or the world that is currently open.
bool deleteWorld(const std::string& name, std::string& error) {
    if (name.empty() || name == "." || name == ".." ||
        name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos) {
        error = "Invalid world name.";
        return false;
    }

    std::error_code ec;
    const fs::path root = fs::weakly_canonical(savesRoot(), ec);
    const fs::path dir = fs::weakly_canonical(savesRoot() / name, ec);
    if (ec || dir.parent_path() != root || !fs::is_directory(dir, ec)) {
        error = "World folder not found.";
        return false;
    }

    WorldMeta meta;
    readWorldMeta(dir / "world.dat", meta);
    if (!meta.game.empty() && meta.game != g_ActiveGameFolder) {
        error = "That world belongs to another game.";
        return false;
    }

    if (g_WorldInstance && !g_WorldInstance->saveDirectory().empty() &&
        fs::weakly_canonical(g_WorldInstance->saveDirectory(), ec) == dir) {
        error = "That world is currently open.";
        return false;
    }

    const auto removed = fs::remove_all(dir, ec);
    if (ec) {
        error = "Could not delete: " + ec.message();
        return false;
    }
    std::cout << "[World] Deleted '" << name << "' (" << removed << " files)" << std::endl;
    return true;
}

// ===========================================================================
// Installed mods (for the Mods screen)
// ===========================================================================

std::string modPathKey(const fs::path& p) {
    std::error_code ec;
    const fs::path c = fs::weakly_canonical(p, ec);
    return (ec ? p : c).string();
}

struct ModRoot {
    fs::path path;
    std::string source;   // "Global" or "Game: <folder>"
};

// Same folders the startup loader reads, each listed once.
std::vector<ModRoot> modRoots() {
    std::vector<ModRoot> roots;
    std::set<std::string> seen;
    auto add = [&](const std::string& rel, const std::string& source) {
        std::string resolved = rel;
        if (!fs::exists(resolved) && fs::exists("../../" + rel)) resolved = "../../" + rel;
        std::error_code ec;
        if (!fs::is_directory(resolved, ec)) return;
        const std::string key = modPathKey(resolved);
        if (seen.insert(key).second) roots.push_back({ fs::path(resolved), source });
    };
    add("mods", "Global");
    add("../mods", "Global");
    add("../../mods", "Global");
    add("games/" + g_LoadedGameFolder + "/mods", "Game: " + g_LoadedGameFolder);
    return roots;
}

// key = value lines (mod.conf style)
std::map<std::string, std::string> readConfFile(const fs::path& file) {
    std::map<std::string, std::string> out;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t"));
            const size_t e = s.find_last_not_of(" \t");
            s.erase(e == std::string::npos ? 0 : e + 1);
        };
        trim(key);
        trim(val);
        out[key] = val;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Turning mods in the mods/ folder on and off
//   Saved in mods/mod_settings.txt as lines like:   my_mod = false
//   Mods are on unless listed as false. Changes apply on the next start.
//   Mods in games/<game>/mods are part of the game and can't be switched off here.
// ---------------------------------------------------------------------------

std::map<std::string, bool> g_ModEnabledAtStartup;   // what the loader actually did this run

fs::path globalModsFolder() {
    for (const auto& r : modRoots()) {
        if (r.source == "Global") return r.path;
    }
    return fs::path("mods");
}

fs::path modSettingsFile() {
    return globalModsFolder() / "mod_settings.txt";
}

std::map<std::string, bool> loadModSettings() {
    std::map<std::string, bool> settings;
    for (const auto& [key, val] : readConfFile(modSettingsFile())) {
        settings[key] = !(val == "false" || val == "0" || val == "off" || val == "no");
    }
    return settings;
}

bool saveModSettings(const std::map<std::string, bool>& settings) {
    const fs::path file = modSettingsFile();
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << "# Mods switched on/off on the Mods screen. Changes apply when the game restarts.\n";
        for (const auto& [id, on] : settings) out << id << " = " << (on ? "true" : "false") << "\n";
        if (!out) return false;
    }
    std::error_code ec;
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(file, ec);
        fs::rename(tmp, file, ec);
    }
    return !ec;
}

// A mod that can't be switched off: the base mod (it contains the Mods screen itself),
// or any mod whose mod.conf says "required = true".
bool modIsRequired(const fs::path& modFolder, const std::string& id) {
    if (id == "base") return true;
    std::error_code ec;
    const fs::path conf = modFolder / "mod.conf";
    if (!fs::exists(conf, ec)) return false;
    auto kv = readConfFile(conf);
    return kv["required"] == "true";
}

bool modIsEnabled(const fs::path& rootFolder, const std::string& id) {
    if (modIsRequired(rootFolder / id, id)) return true;
    const auto settings = loadModSettings();
    const auto it = settings.find(id);
    return it == settings.end() || it->second;
}

struct ModInfo {
    std::string id;           // folder or file name
    std::string title, author, description, version, depends;
    std::string source;       // Global / Game: x
    std::string path;
    std::string status;       // "loaded", "error", "not_loaded", "disabled"
    std::string error;
    int luaFiles = 0;
    bool isFolder = false;
    bool enabled = true;      // switched on (takes effect on restart)
    bool required = false;    // can't be switched off
    bool restartNeeded = false;
};

// Fills status/error from what happened when the files were run at startup.
void applyLoadStatus(ModInfo& m, const std::vector<fs::path>& files) {
    bool ran = false;
    for (const auto& f : files) {
        const auto it = g_ModFileStatus.find(modPathKey(f));
        if (it == g_ModFileStatus.end()) continue;
        ran = true;
        if (!it->second.empty() && m.error.empty()) m.error = it->second;
    }
    m.status = !m.error.empty() ? "error" : (ran ? "loaded" : "not_loaded");
}

// Every mod folder and loose .lua file in the mods/ folder (game mods are not listed).
std::vector<ModInfo> listInstalledMods() {
    std::vector<ModInfo> mods;
    for (const auto& root : modRoots()) {
        if (root.source != "Global") continue;   // only the mods/ folder

        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(root.path, ec)) {
            ModInfo m;
            m.source = root.source;
            m.path = entry.path().string();
            std::vector<fs::path> files;

            if (entry.is_directory()) {
                m.isFolder = true;
                m.id = entry.path().filename().string();
                for (const auto& f : fs::recursive_directory_iterator(entry.path(), ec)) {
                    if (f.is_regular_file() && f.path().extension() == ".lua") files.push_back(f.path());
                }
                if (files.empty()) continue;   // not a mod (e.g. a textures folder)

                const fs::path conf = entry.path() / "mod.conf";
                if (fs::exists(conf, ec)) {
                    auto kv = readConfFile(conf);
                    m.title = kv.count("title") ? kv["title"] : kv["name"];
                    m.author = kv["author"];
                    m.description = kv["description"];
                    m.version = kv["version"];
                    m.depends = kv["depends"];
                }
            } else if (entry.is_regular_file() && entry.path().extension() == ".lua") {
                m.id = entry.path().filename().string();
                files.push_back(entry.path());
            } else {
                continue;
            }

            if (m.title.empty()) m.title = m.id;
            m.luaFiles = static_cast<int>(files.size());
            m.required = modIsRequired(entry.path(), m.id);
            m.enabled = modIsEnabled(root.path, m.id);

            applyLoadStatus(m, files);
            const auto started = g_ModEnabledAtStartup.find(m.id);
            const bool wasOn = started == g_ModEnabledAtStartup.end() || started->second;
            if (!wasOn) m.status = "disabled";
            m.restartNeeded = (wasOn != m.enabled);

            mods.push_back(m);
        }
    }
    std::sort(mods.begin(), mods.end(), [](const ModInfo& a, const ModInfo& b) {
        return a.title < b.title;
    });
    return mods;
}

// Switches a mod in mods/ on or off (applies on restart). Returns false + reason on failure.
bool setModEnabled(const std::string& id, bool on, std::string& error) {
    if (id.empty() || id.find('/') != std::string::npos || id.find('\\') != std::string::npos ||
        id.find("..") != std::string::npos) {
        error = "Invalid mod name.";
        return false;
    }
    const fs::path root = globalModsFolder();
    std::error_code ec;
    if (!fs::exists(root / id, ec)) {
        error = "Mod not found.";
        return false;
    }
    if (!on && modIsRequired(root / id, id)) {
        error = "This mod is required and can't be switched off.";
        return false;
    }

    auto settings = loadModSettings();
    settings[id] = on;
    if (!saveModSettings(settings)) {
        error = "Could not save " + modSettingsFile().string();
        return false;
    }
    std::cout << "[Mods] " << id << " will be " << (on ? "enabled" : "disabled") << " after a restart" << std::endl;
    return true;
}

// Opens the main mods folder in Explorer / Finder / the desktop file manager.
void openModsFolder() {
    auto roots = modRoots();
    fs::path target;
    for (const auto& r : roots) {
        if (r.source == "Global") { target = r.path; break; }
    }
    if (target.empty()) {
        std::error_code ec;
        fs::create_directories("mods", ec);
        target = "mods";
    }
    std::error_code ec;
    std::string abs = fs::absolute(target, ec).string();
    abs.erase(std::remove(abs.begin(), abs.end(), '"'), abs.end());   // keep the command well-formed

#if defined(_WIN32)
    const std::string cmd = "explorer \"" + abs + "\"";
#elif defined(__APPLE__)
    const std::string cmd = "open \"" + abs + "\"";
#else
    const std::string cmd = "xdg-open \"" + abs + "\" &";
#endif
    std::cout << "[Mods] Opening " << abs << std::endl;
    (void)std::system(cmd.c_str());
}

// Calls a Lua UI function if it exists. Returns true only if it returned true.
template <typename... Args>
bool callLuaUI(sol::state* lua, const char* fn, Args&&... args) {
    if (!lua) return false;
    try {
        sol::protected_function f = (*lua)[fn];
        if (!f.valid()) return false;
        sol::protected_function_result r = f(std::forward<Args>(args)...);
        if (!r.valid()) {
            sol::error err = r;
            std::cerr << "[Lua] " << fn << ": " << err.what() << std::endl;
            return false;
        }
        if (r.return_count() > 0 && r.get_type() == sol::type::boolean) return r.get<bool>();
    } catch (...) {}
    return false;
}

struct EngineContext {
    Player* player = nullptr;
    sol::state* lua = nullptr;
};

const int TEX_SIZE = 16;
const int MAX_TEXTURE_LAYERS = 256;
GLuint g_TextureArrayID = 0;
int g_NextTextureLayer = 0;

void initTextureArray() {
    glGenTextures(1, &g_TextureArrayID);
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, TEX_SIZE, TEX_SIZE, MAX_TEXTURE_LAYERS, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

// ===========================================================================
// Texture packs
//   texturepacks/<pack>/          one folder per pack
//   texturepacks/<pack>/pack.conf title, author, description (all optional)
//   Any image in a pack with the same file name as a game texture replaces it,
//   e.g. texturepacks/MyPack/stone.png (or .../textures/stone.png).
//   The chosen pack is saved in texturepacks/active_pack.txt.
// ===========================================================================

struct TextureSource {
    std::string requested;   // name the game asked for, e.g. "stone.png"
    std::string resolved;    // file it was found at without a pack ("" = not found)
};

std::vector<TextureSource> g_LayerSources;                 // block texture layer -> source
std::map<GLuint, TextureSource> g_StandardTextureSources;  // 2D UI textures (hotbar, etc.)
std::string g_ActiveTexturePack;                           // "" = default textures

fs::path texturePacksRoot() {
    if (!fs::exists("texturepacks") && fs::exists("../../texturepacks")) return fs::path("../../texturepacks");
    return fs::path("texturepacks");
}

bool isSafeFolderName(const std::string& name) {
    return !name.empty() && name != "." && name != ".." &&
           name.find('/') == std::string::npos && name.find('\\') == std::string::npos &&
           name.find(':') == std::string::npos && name.find("..") == std::string::npos;
}

// The file in `pack` that replaces `requested`, or "".
std::string packOverrideFor(const std::string& pack, const std::string& requested) {
    if (pack.empty() || requested.empty()) return "";
    const fs::path root = texturePacksRoot() / pack;
    const fs::path req(requested);
    if (req.is_absolute() || requested.find("..") != std::string::npos) return "";
    const fs::path candidates[] = { root / req, root / "textures" / req.filename(), root / req.filename() };
    for (const fs::path& c : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(c, ec)) return c.string();
    }
    return "";
}

std::string packOverride(const std::string& requested) {
    return packOverrideFor(g_ActiveTexturePack, requested);
}

// Loads an image as RGBA, resized (nearest neighbour) to size x size.
// Tall animation strips (frames stacked vertically) use their first frame.
bool loadImageScaled(const std::string& file, int size, std::vector<unsigned char>& out) {
    int w = 0, h = 0, n = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* d = stbi_load(file.c_str(), &w, &h, &n, 4);
    if (!d) return false;

    int srcH = h;
    if (h > w && w > 0 && h % w == 0) srcH = w;   // first frame of an animation strip

    out.resize(static_cast<size_t>(size) * size * 4);
    for (int y = 0; y < size; ++y) {
        const int sy = y * srcH / size;
        for (int x = 0; x < size; ++x) {
            const int sx = x * w / size;
            const unsigned char* s = d + (static_cast<size_t>(sy) * w + sx) * 4;
            unsigned char* o = out.data() + (static_cast<size_t>(y) * size + x) * 4;
            o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = s[3];
        }
    }
    stbi_image_free(d);
    return true;
}

// (Re)uploads one block texture layer: pack image, else the original, else white.
void uploadLayer(int layer, const TextureSource& src) {
    std::vector<unsigned char> px;
    const std::string override = packOverride(src.requested);
    bool ok = !override.empty() && loadImageScaled(override, TEX_SIZE, px);
    if (!ok && !src.resolved.empty()) ok = loadImageScaled(src.resolved, TEX_SIZE, px);
    if (!ok) px.assign(static_cast<size_t>(TEX_SIZE) * TEX_SIZE * 4, 255);   // missing texture: white

    glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, TEX_SIZE, TEX_SIZE, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
}

int loadTextureToLayer(const std::string& path) {
    std::string finalPath = path;

    std::vector<std::string> searchPaths = {
        path,
        "textures/" + path,
        "../../textures/" + path,
        g_CurrentModPath + "/" + path,
        g_CurrentModPath + "/textures/" + path,
        "../../" + g_CurrentModPath + "/" + path
    };

    bool found = false;
    for (const auto& p : searchPaths) {
        if (!p.empty() && fs::exists(p)) {
            finalPath = p;
            found = true;
            break;
        }
    }

    if (!found && packOverride(path).empty()) {
        std::cerr << "[Texture] Missing block texture: " << path << std::endl;
    }
    if (found && packOverride(path).empty()) {
        int w, h, n;
        if (!stbi_info(finalPath.c_str(), &w, &h, &n)) return -1;   // unreadable image
    }
    if (g_NextTextureLayer >= MAX_TEXTURE_LAYERS) {
        std::cerr << "[Texture] Out of texture layers (max " << MAX_TEXTURE_LAYERS << "): " << path << std::endl;
        return -1;
    }

    const int layer = g_NextTextureLayer++;
    if (static_cast<int>(g_LayerSources.size()) <= layer) g_LayerSources.resize(layer + 1);
    g_LayerSources[layer] = TextureSource{ path, found ? finalPath : "" };
    uploadLayer(layer, g_LayerSources[layer]);
    return layer;
}

// (Re)uploads a 2D texture at its own size: pack image, else the original.
bool uploadStandard(GLuint texID, const TextureSource& src) {
    std::string file = packOverride(src.requested);
    int width = 0, height = 0, nrChannels = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = file.empty() ? nullptr : stbi_load(file.c_str(), &width, &height, &nrChannels, 4);
    if (!data && !src.resolved.empty()) {
        file = src.resolved;
        data = stbi_load(file.c_str(), &width, &height, &nrChannels, 4);
    }
    if (!data) return false;

    glBindTexture(GL_TEXTURE_2D, texID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);

    std::cout << "[Texture] Loaded " << file << " (" << width << "x" << height << ")" << std::endl;
    return true;
}

GLuint loadStandardTexture(const std::string& path) {
    std::string finalPath = path;
    std::vector<std::string> searchPaths = {
        path,
        "textures/" + path,
        "../../textures/" + path,
        g_CurrentModPath + "/" + path,
        g_CurrentModPath + "/textures/" + path,
        "../../" + g_CurrentModPath + "/" + path,
        "games/" + g_ActiveGameFolder + "/textures/" + path,
        "../../games/" + g_ActiveGameFolder + "/textures/" + path
    };

    bool found = false;
    for (const auto& p : searchPaths) {
        if (!p.empty() && fs::exists(p)) {
            finalPath = fs::absolute(p).string();
            found = true;
            break;
        }
    }
    if (!found && packOverride(path).empty()) {
        std::cerr << "[Texture] NOT FOUND: " << path
                  << " (also looked in games/" << g_ActiveGameFolder << "/textures/)" << std::endl;
        return 0;
    }

    GLuint texID = 0;
    glGenTextures(1, &texID);
    const TextureSource src{ path, found ? finalPath : "" };
    if (!uploadStandard(texID, src)) {
        std::cerr << "[Texture] Failed to decode: " << (found ? finalPath : path) << std::endl;
        glDeleteTextures(1, &texID);
        return 0;
    }
    g_StandardTextureSources[texID] = src;
    return texID;
}

// Re-uploads every texture with the current pack. No restart needed.
void reloadAllTextures() {
    for (int i = 0; i < static_cast<int>(g_LayerSources.size()); ++i) uploadLayer(i, g_LayerSources[i]);
    for (const auto& [id, src] : g_StandardTextureSources) uploadStandard(id, src);
    std::cout << "[Texture] Reloaded " << g_LayerSources.size() << " block textures and "
              << g_StandardTextureSources.size() << " UI textures"
              << (g_ActiveTexturePack.empty() ? " (default)" : " from pack '" + g_ActiveTexturePack + "'")
              << std::endl;
}

std::string readActiveTexturePack() {
    std::ifstream in(texturePacksRoot() / "active_pack.txt");
    std::string name;
    std::getline(in, name);
    while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
    std::error_code ec;
    if (!isSafeFolderName(name) || !fs::is_directory(texturePacksRoot() / name, ec)) return "";
    return name;
}

struct TexturePackInfo {
    std::string id, title, author, description, path;
    int images = 0;      // image files in the pack
    int replaces = 0;    // how many of the game's textures it replaces
};

// "Default" first, then every folder in texturepacks/.
std::vector<TexturePackInfo> listTexturePacks() {
    std::vector<TexturePackInfo> packs;
    TexturePackInfo def;
    def.title = "Default";
    def.description = "The game's own textures.";
    packs.push_back(def);

    const fs::path root = texturePacksRoot();
    std::error_code ec;
    fs::create_directories(root, ec);

    std::vector<TexturePackInfo> found;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory()) continue;
        TexturePackInfo p;
        p.id = entry.path().filename().string();
        p.path = entry.path().string();

        const fs::path conf = entry.path() / "pack.conf";
        if (fs::exists(conf, ec)) {
            auto kv = readConfFile(conf);
            p.title = kv.count("title") ? kv["title"] : kv["name"];
            p.author = kv["author"];
            p.description = kv["description"];
        }
        if (p.title.empty()) p.title = p.id;

        for (const auto& f : fs::recursive_directory_iterator(entry.path(), ec)) {
            if (f.is_regular_file() && f.path().extension() == ".png") ++p.images;
        }

        std::set<std::string> counted;
        auto count = [&](const std::string& requested) {
            if (counted.insert(requested).second && !packOverrideFor(p.id, requested).empty()) ++p.replaces;
        };
        for (const auto& s : g_LayerSources) count(s.requested);
        for (const auto& [id, s] : g_StandardTextureSources) count(s.requested);

        found.push_back(p);
    }
    std::sort(found.begin(), found.end(),
              [](const TexturePackInfo& a, const TexturePackInfo& b) { return a.title < b.title; });
    packs.insert(packs.end(), found.begin(), found.end());
    return packs;
}

// Switches pack ("" = default), saves the choice and reloads every texture.
bool setTexturePack(const std::string& id, std::string& error) {
    std::error_code ec;
    if (!id.empty() && (!isSafeFolderName(id) || !fs::is_directory(texturePacksRoot() / id, ec))) {
        error = "Texture pack not found.";
        return false;
    }
    g_ActiveTexturePack = id;

    fs::create_directories(texturePacksRoot(), ec);
    std::ofstream out(texturePacksRoot() / "active_pack.txt", std::ios::trunc);
    out << id << "\n";
    if (!out) std::cerr << "[Texture] Could not save the chosen texture pack" << std::endl;

    reloadAllTextures();
    return true;
}

void openTexturePacksFolder() {
    std::error_code ec;
    fs::create_directories(texturePacksRoot(), ec);
    std::string abs = fs::absolute(texturePacksRoot(), ec).string();
    abs.erase(std::remove(abs.begin(), abs.end(), '"'), abs.end());
#if defined(_WIN32)
    const std::string cmd = "explorer \"" + abs + "\"";
#elif defined(__APPLE__)
    const std::string cmd = "open \"" + abs + "\"";
#else
    const std::string cmd = "xdg-open \"" + abs + "\" &";
#endif
    (void)std::system(cmd.c_str());
}

// ===========================================================================
// Models: finding model files, and models placed in the world from Lua
// ===========================================================================

// Finds a mod asset (model or texture). Tries the path as given, then the current
// mod's folder (and its models/ or textures/ subfolder), then the game folder.
std::string resolveAssetPath(const std::string& path, const std::string& subfolder) {
    if (path.empty()) return "";
    const std::string file = fs::path(path).filename().string();
    const std::vector<std::string> candidates = {
        path,
        "../../" + path,
        g_CurrentModPath + "/" + path,
        g_CurrentModPath + "/" + subfolder + "/" + file,
        "games/" + g_ActiveGameFolder + "/" + path,
        "../../games/" + g_ActiveGameFolder + "/" + path,
        subfolder + "/" + file,
        "../../" + subfolder + "/" + file,
    };
    for (const auto& p : candidates) {
        std::error_code ec;
        if (!p.empty() && fs::is_regular_file(p, ec)) return p;
    }
    return "";
}

struct ModelInstance {
    std::shared_ptr<AnimatedModel> model;
    glm::vec3 position{0.0f};
    float yaw = 0.0f;        // degrees
    float scale = 1.0f;
    bool visible = true;
};

std::map<int, ModelInstance> g_ModelInstances;   // models placed with engine.add_model
int g_NextModelInstance = 1;
bool g_ThirdPerson = false;                      // F5 camera (the player model is only drawn in third person)

// ===========================================================================
// Settings (settings.txt next to the mods folder) and key bindings
//   key_<action> = <GLFW key code>      e.g. key_forward = 87
//   fov = 70, render_distance = 10, vsync = 1
// ===========================================================================

struct KeyAction {
    const char* id;
    const char* label;
    int defaultKey;
};

// Every rebindable action, in the order shown on the Controls tab.
const std::vector<KeyAction>& keyActions() {
    static const std::vector<KeyAction> actions = {
        { "forward",   "Walk Forward",   GLFW_KEY_W },
        { "back",      "Walk Backward",  GLFW_KEY_S },
        { "left",      "Strafe Left",    GLFW_KEY_A },
        { "right",     "Strafe Right",   GLFW_KEY_D },
        { "jump",      "Jump",           GLFW_KEY_SPACE },
        { "sneak",     "Sneak",          GLFW_KEY_LEFT_SHIFT },
        { "sprint",    "Sprint",         GLFW_KEY_LEFT_CONTROL },
        { "inventory", "Open Inventory", GLFW_KEY_E },
        { "chat",      "Open Chat",      GLFW_KEY_T },
        { "command",   "Open Command",   GLFW_KEY_SLASH },
        { "third_person", "Toggle Camera View", GLFW_KEY_F5 },
        { "hotbar1",   "Hotbar Slot 1",  GLFW_KEY_1 },
        { "hotbar2",   "Hotbar Slot 2",  GLFW_KEY_2 },
        { "hotbar3",   "Hotbar Slot 3",  GLFW_KEY_3 },
        { "hotbar4",   "Hotbar Slot 4",  GLFW_KEY_4 },
        { "hotbar5",   "Hotbar Slot 5",  GLFW_KEY_5 },
        { "hotbar6",   "Hotbar Slot 6",  GLFW_KEY_6 },
        { "hotbar7",   "Hotbar Slot 7",  GLFW_KEY_7 },
        { "hotbar8",   "Hotbar Slot 8",  GLFW_KEY_8 },
        { "hotbar9",   "Hotbar Slot 9",  GLFW_KEY_9 },
    };
    return actions;
}

std::map<std::string, std::string> g_Settings;
bool g_SettingsInGame = false;   // settings opened from the pause menu (vs the title screen)

fs::path settingsFile() {
    const fs::path mods = globalModsFolder();
    return (mods.has_parent_path() ? mods.parent_path() : fs::path(".")) / "settings.txt";
}

void loadSettings() {
    g_Settings = readConfFile(settingsFile());
}

void saveSettings() {
    const fs::path file = settingsFile();
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            std::cerr << "[Settings] Could not write " << file.string() << std::endl;
            return;
        }
        out << "# Game settings. Edited from the Settings screen.\n";
        for (const auto& [k, v] : g_Settings) out << k << " = " << v << "\n";
    }
    std::error_code ec;
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(file, ec);
        fs::rename(tmp, file, ec);
    }
}

int settingInt(const std::string& name, int fallback) {
    const auto it = g_Settings.find(name);
    if (it == g_Settings.end()) return fallback;
    try { return std::stoi(it->second); } catch (...) { return fallback; }
}

const KeyAction* findKeyAction(const std::string& id) {
    for (const auto& a : keyActions()) if (id == a.id) return &a;
    return nullptr;
}

// Key currently bound to an action (GLFW_KEY_UNKNOWN if the action doesn't exist).
int keyForAction(const std::string& action) {
    const KeyAction* a = findKeyAction(action);
    if (!a) return GLFW_KEY_UNKNOWN;
    return settingInt("key_" + action, a->defaultKey);
}

// True while the key bound to `action` is held. Use this instead of glfwGetKey for game controls.
bool isActionDown(GLFWwindow* window, const std::string& action) {
    const int key = keyForAction(action);
    return key >= GLFW_KEY_SPACE && key <= GLFW_KEY_LAST && glfwGetKey(window, key) == GLFW_PRESS;
}

std::string keyName(int key) {
    if (key == GLFW_KEY_UNKNOWN) return "None";
    if (const char* n = glfwGetKeyName(key, 0)) {
        std::string s(n);
        for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        return s;
    }
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F25) return "F" + std::to_string(key - GLFW_KEY_F1 + 1);
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9) return "Numpad " + std::to_string(key - GLFW_KEY_KP_0);
    switch (key) {
        case GLFW_KEY_SPACE:         return "Space";
        case GLFW_KEY_LEFT_SHIFT:    return "Left Shift";
        case GLFW_KEY_RIGHT_SHIFT:   return "Right Shift";
        case GLFW_KEY_LEFT_CONTROL:  return "Left Ctrl";
        case GLFW_KEY_RIGHT_CONTROL: return "Right Ctrl";
        case GLFW_KEY_LEFT_ALT:      return "Left Alt";
        case GLFW_KEY_RIGHT_ALT:     return "Right Alt";
        case GLFW_KEY_TAB:           return "Tab";
        case GLFW_KEY_ENTER:         return "Enter";
        case GLFW_KEY_KP_ENTER:      return "Numpad Enter";
        case GLFW_KEY_BACKSPACE:     return "Backspace";
        case GLFW_KEY_CAPS_LOCK:     return "Caps Lock";
        case GLFW_KEY_UP:            return "Up";
        case GLFW_KEY_DOWN:          return "Down";
        case GLFW_KEY_LEFT:          return "Left";
        case GLFW_KEY_RIGHT:         return "Right";
        case GLFW_KEY_INSERT:        return "Insert";
        case GLFW_KEY_DELETE:        return "Delete";
        case GLFW_KEY_HOME:          return "Home";
        case GLFW_KEY_END:           return "End";
        case GLFW_KEY_PAGE_UP:       return "Page Up";
        case GLFW_KEY_PAGE_DOWN:     return "Page Down";
        default:                     return "Key " + std::to_string(key);
    }
}

bool setKeybind(const std::string& action, int key, std::string& error) {
    if (!findKeyAction(action)) { error = "Unknown action."; return false; }
    if (key == GLFW_KEY_ESCAPE) { error = "Escape is reserved for menus."; return false; }
    if (key < GLFW_KEY_SPACE || key > GLFW_KEY_LAST) { error = "That key can't be used."; return false; }
    g_Settings["key_" + action] = std::to_string(key);
    saveSettings();
    return true;
}

void resetKeybinds() {
    for (auto it = g_Settings.begin(); it != g_Settings.end();) {
        if (it->first.rfind("key_", 0) == 0) it = g_Settings.erase(it);
        else ++it;
    }
    saveSettings();
}

// Video settings: value limits, current value, and applying them.
struct VideoSetting { const char* id; int minV, maxV, step; };
const VideoSetting kVideoSettings[] = {
    { "fov",             30, 110, 5 },
    { "render_distance",  2,  16, 1 },
    { "vsync",            0,   1, 1 },
};

const VideoSetting* findVideoSetting(const std::string& id) {
    for (const auto& s : kVideoSettings) if (id == s.id) return &s;
    return nullptr;
}

int currentVideoSetting(const std::string& id) {
    if (id == "fov") return static_cast<int>(std::lround(g_FieldOfView));
    if (id == "render_distance") return g_WorldInstance ? g_WorldInstance->renderDistance() : settingInt(id, 10);
    if (id == "vsync") return settingInt(id, 1);
    return 0;
}

void applyVideoSetting(const std::string& id, int value) {
    if (id == "fov") g_FieldOfView = static_cast<float>(value);
    else if (id == "render_distance") { if (g_WorldInstance) g_WorldInstance->setRenderDistance(value); }
    else if (id == "vsync") glfwSwapInterval(value ? 1 : 0);
}

bool setVideoSetting(const std::string& id, int value, std::string& error) {
    const VideoSetting* s = findVideoSetting(id);
    if (!s) { error = "Unknown setting."; return false; }
    value = std::clamp(value, s->minV, s->maxV);
    g_Settings[id] = std::to_string(value);
    saveSettings();
    applyVideoSetting(id, value);
    return true;
}

// Re-applies saved video settings (after mods, so the player's choices win).
void applyStoredVideoSettings() {
    for (const auto& s : kVideoSettings) {
        if (g_Settings.count(s.id)) applyVideoSetting(s.id, std::clamp(settingInt(s.id, s.minV), s.minV, s.maxV));
    }
}

bool settingsOpen() {
    return g_CurrentGameState == GameState::SETTINGS ||
           (g_CurrentGameState == GameState::PLAYING && g_Paused && g_SettingsInGame);
}

void closeSettings() {
    if (g_CurrentGameState == GameState::SETTINGS) g_CurrentGameState = GameState::MENU;
    g_SettingsInGame = false;   // in-game: back to the pause menu
}

static uint8_t g_NextBlockId = 1;

void registerBlockFromLua(const std::string& name, float r, float g, float b, sol::object texObj, sol::object overlayObj, sol::optional<sol::table> propsTable) {
    if (r > 1.0f || g > 1.0f || b > 1.0f) {
        r = glm::clamp(r / 255.0f, 0.0f, 1.0f);
        g = glm::clamp(g / 255.0f, 0.0f, 1.0f);
        b = glm::clamp(b / 255.0f, 0.0f, 1.0f);
    }

    BlockProperty block;
    block.id = g_NextBlockId++;
    block.name = name;
    block.defaultTint = glm::vec3(r, g, b);

    bool shouldTint = (name == "grass" || name == "short_grass");

    if (propsTable.has_value()) {
        sol::table props = propsTable.value();
        block.isTransparent = props.get_or("transparent", false);
        block.isCutout      = props.get_or("cutout", false);
        block.isWalkable    = props.get_or("walkable", (name != "air" && name != "water" && name != "flowing_water" && name != "short_grass" && name != "flower_red"));
        block.isSelectable  = props.get_or("selectable", (name != "water" && name != "flowing_water" && name != "air" && name != "short_grass" && name != "flower_red"));
        block.hasGravity    = props.get_or("gravity", false);
        block.pattern       = props.get_or("pattern", std::string("cube"));
        shouldTint          = props.get_or("tint", shouldTint);
        // in_creative = false hides the block from the creative inventory (it still exists in the world)
        block.in_creative   = props.get_or("in_creative", true);

        // inventory_image = "stick.png" shows a flat picture in the inventory/hotbar instead of a cube
        sol::optional<std::string> invImage = props["inventory_image"];
        if (invImage.has_value() && !invImage.value().empty()) {
            block.inventoryImage = invImage.value();
            block.inventoryLayer = loadTextureToLayer(block.inventoryImage);
        }
    } else {
        block.isTransparent = (name == "water" || name == "flowing_water" || name == "leaves" || name == "birch_leaves" || name == "short_grass" || name == "flower_red");
        block.isCutout      = (name.find("leaves") != std::string::npos);
        block.isWalkable    = (name != "air" && name != "water" && name != "flowing_water" && name != "short_grass" && name != "flower_red");
        block.isSelectable  = (name != "water" && name != "flowing_water" && name != "air" && name != "short_grass" && name != "flower_red");
        block.hasGravity    = (name == "sand" || name == "gravel");
        block.pattern       = (name == "short_grass" || name == "flower_red") ? "plantlike" : "cube";
    }

    if (texObj.is<std::string>()) {
        int layer = loadTextureToLayer(texObj.as<std::string>());
        for (int f = 0; f < 6; ++f) block.faces[f].baseLayer = layer;
        if (shouldTint) {
            if (name == "grass") block.faces[2].tintBase = true;
            else { for (int f = 0; f < 6; ++f) block.faces[f].tintBase = true; }
        }
    } else if (texObj.is<sol::table>()) {
        sol::table t = texObj.as<sol::table>();
        sol::optional<std::string> top = t["top"];
        sol::optional<std::string> bottom = t["bottom"];
        sol::optional<std::string> sides = t["sides"];

        int topL = top.has_value() ? loadTextureToLayer(top.value()) : -1;
        int botL = bottom.has_value() ? loadTextureToLayer(bottom.value()) : -1;
        int sideL = sides.has_value() ? loadTextureToLayer(sides.value()) : -1;

        block.faces[2].baseLayer = topL;
        block.faces[3].baseLayer = botL;
        block.faces[0].baseLayer = block.faces[1].baseLayer = block.faces[4].baseLayer = block.faces[5].baseLayer = sideL;

        if (shouldTint) {
            if (name == "grass") block.faces[2].tintBase = true;
            else { for (int f = 0; f < 6; ++f) block.faces[f].tintBase = true; }
        }
    }

    if (overlayObj.is<std::string>()) {
        int overlayLayer = loadTextureToLayer(overlayObj.as<std::string>());
        for (int f = 0; f < 6; ++f) {
            if (f != 2 && f != 3) {
                block.faces[f].overlayLayer = overlayLayer;
                block.faces[f].tintOverlay = true;
            }
        }
    } else if (overlayObj.is<sol::table>()) {
        sol::table ot = overlayObj.as<sol::table>();
        sol::optional<std::string> sideOverlay = ot["sides"];
        if (sideOverlay.has_value()) {
            int sideL = loadTextureToLayer(sideOverlay.value());
            for (int f = 0; f < 6; ++f) {
                if (f != 2 && f != 3) {
                    block.faces[f].overlayLayer = sideL;
                    block.faces[f].tintOverlay = true;
                }
            }
        }
    }

    BlockRegistry::getInstance().registerBlock(block);
}

uint8_t getBlockIdLua(const std::string& name) {
    return BlockRegistry::getInstance().getBlockId(name);
}

bool isWaterBlockName(const std::string& name) {
    return name == "water" || name == "flowing_water";
}

float underwaterDepth(const World& world, const glm::vec3& eye) {
    const int bx = static_cast<int>(std::floor(eye.x));
    const int bz = static_cast<int>(std::floor(eye.z));
    const int by = static_cast<int>(std::floor(eye.y));

    auto isWaterAt = [&](int y) {
        const uint8_t id = world.getBlockAt(glm::ivec3(bx, y, bz));
        return id != 0 && isWaterBlockName(BlockRegistry::getInstance().getBlock(id).name);
    };

    if (!isWaterAt(by)) return -1.0f;

    int top = by;
    for (int y = by + 1; y < by + 128 && isWaterAt(y); ++y) {
        top = y;
    }

    const float surfaceY = static_cast<float>(top) + 1.0f;
    return glm::max(surfaceY - eye.y, 0.0f);
}

WaterColors queryWaterColors(sol::state& lua, const glm::vec3& eye) {
    const WaterColors fallback{ g_Underwater.shallowColor, g_Underwater.deepColor };
    try {
        sol::function fn = lua["get_water_color"];
        if (!fn.valid()) return fallback;
        sol::table t = fn(static_cast<int>(std::floor(eye.x)), static_cast<int>(std::floor(eye.z)));
        if (t.size() < 6) return fallback;
        return WaterColors{
            glm::vec3(t.get<float>(1), t.get<float>(2), t.get<float>(3)),
            glm::vec3(t.get<float>(4), t.get<float>(5), t.get<float>(6))
        };
    } catch (...) {
        return fallback;
    }
}

const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aTintColor;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in float aBaseLayer;
layout (location = 4) in float aOverlayLayer;
layout (location = 5) in float aTintBase;
layout (location = 6) in float aTintOverlay;
layout (location = 7) in float aFaceShading;

out vec3 TintColor;
out vec2 TexCoord;
out float BaseLayer;
out float OverlayLayer;
out float TintBase;
out float TintOverlay;
out float LightIntensity;
out vec3 FragPos;
out vec3 WorldPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 u_SunDir;
uniform int u_Unlit;
uniform float u_Time;

void main() {
    vec3 pos = aPos;
    TintColor = aTintColor;
    TexCoord = aTexCoord;
    BaseLayer = aBaseLayer;
    OverlayLayer = aOverlayLayer;
    TintBase = aTintBase;
    TintOverlay = aTintOverlay;

    WorldPos = (model * vec4(pos, 1.0)).xyz;

    if (u_Unlit == 1) {
        LightIntensity = 1.0f;
    } else {
        float sunDot = dot(vec3(0.0, 1.0, 0.0), u_SunDir);
        float directLight = clamp(sunDot * 0.6f + 0.4f, 0.28f, 1.0f);
        LightIntensity = clamp(aFaceShading * directLight, 0.25f, 1.0f);
    }

    vec4 viewPos = view * model * vec4(pos, 1.0);
    FragPos = viewPos.xyz;
    gl_Position = projection * viewPos;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
in vec3 TintColor;
in vec2 TexCoord;
in float BaseLayer;
in float OverlayLayer;
in float TintBase;
in float TintOverlay;
in float LightIntensity;
in vec3 FragPos;
in vec3 WorldPos;

out vec4 FragColorOut;
uniform sampler2DArray textureArray;
uniform int u_IsGUI;
uniform int u_Unlit;
uniform vec3 u_SunDir;
uniform float u_Time;

struct PointLight {
    vec3 position;
    vec3 color;
    float radius;
    float intensity;
};
uniform int u_NumPointLights;
uniform PointLight u_PointLights[16];

uniform int   u_FogEnabled;
uniform int   u_FogExp;
uniform float u_FogStart;
uniform float u_FogEnd;
uniform float u_FogDensity;
uniform vec3  u_FogColor;

uniform vec3  u_UnderwaterTint;
uniform float u_UnderwaterAmount;
uniform float u_ChunkFade;

void main() {
    vec4 finalColor = vec4(TintColor, 1.0);

    if (u_Unlit == 0 && u_IsGUI == 0 && u_ChunkFade < 1.0) {
        float threshold = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
        if (threshold > u_ChunkFade) discard;
    }

    if (BaseLayer >= 0.0) {
        vec2 animatedUV = TexCoord;

        if (TintColor.b > TintColor.r && TintColor.b > TintColor.g && BaseLayer > 0.0 && u_IsGUI == 0) {
            animatedUV.x += sin(u_Time * 1.2f + WorldPos.z * 0.5f) * 0.02f;
            animatedUV.y += u_Time * 0.08f;
        }

        vec4 baseTex = texture(textureArray, vec3(animatedUV, BaseLayer));
        if (baseTex.a < 0.05) discard;

        vec3 baseRgb = (TintBase > 0.5) ? (baseTex.rgb * TintColor) : baseTex.rgb;
        finalColor = vec4(baseRgb, baseTex.a);

        if (OverlayLayer >= 0.0) {
            vec4 overlayTex = texture(textureArray, vec3(TexCoord, OverlayLayer));
            if (overlayTex.a > 0.05) {
                vec3 overlayRgb = (TintOverlay > 0.5) ? (overlayTex.rgb * TintColor) : overlayTex.rgb;
                finalColor.rgb = mix(finalColor.rgb, overlayRgb, overlayTex.a);
            }
        }
    }

    if (u_IsGUI == 1) {
        FragColorOut = finalColor;
        return;
    }

    vec3 dayLightColor = vec3(1.0f, 0.98f, 0.92f);
    vec3 sunsetLightColor = vec3(1.0f, 0.62f, 0.32f);
    vec3 nightLightColor = vec3(0.35f, 0.5f, 0.75f);

    vec3 currentSunLightColor = mix(sunsetLightColor, dayLightColor, clamp(u_SunDir.y + 0.15f, 0.0f, 1.0f));
    if (u_SunDir.y < 0.0f) {
        currentSunLightColor = mix(sunsetLightColor, nightLightColor, clamp(-u_SunDir.y * 2.0f, 0.0f, 1.0f));
    }

    vec3 shadedColor = finalColor.rgb * LightIntensity * currentSunLightColor;

    vec3 pointLightContribution = vec3(0.0f);
    for(int i = 0; i < u_NumPointLights && i < 16; i++) {
        float dist = distance(WorldPos, u_PointLights[i].position);
        if(dist < u_PointLights[i].radius) {
            float attenuation = 1.0f - (dist / u_PointLights[i].radius);
            attenuation = attenuation * attenuation;
            pointLightContribution += u_PointLights[i].color * u_PointLights[i].intensity * attenuation;
        }
    }
    shadedColor += finalColor.rgb * pointLightContribution;

    if (u_Unlit == 0) {
        shadedColor *= mix(vec3(1.0f), u_UnderwaterTint, u_UnderwaterAmount);
    }

    float dist = length(FragPos);
    float fogFactor = 1.0;
    if (u_FogEnabled == 1) {
        if (u_FogExp == 1) {
            float d = dist * u_FogDensity;
            fogFactor = exp(-d * d);
        } else {
            fogFactor = clamp((u_FogEnd - dist) / max(u_FogEnd - u_FogStart, 0.001), 0.0, 1.0);
        }
    }

    vec3 finalRgb = mix(u_FogColor, shadedColor, fogFactor);
    FragColorOut = vec4(finalRgb, finalColor.a);
}
)";

const char* sunVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    TexCoord = aTexCoord;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* sunFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D u_Texture;
uniform int u_HasTexture;

void main() {
    if (u_HasTexture == 1) {
        vec4 texColor = texture(u_Texture, TexCoord);
        if (texColor.a < 0.1) discard;
        FragColor = texColor;
    } else {
        FragColor = vec4(1.0, 0.95, 0.6, 1.0);
    }
}
)";

const char* wireframeVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* wireframeFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
void main() {
    FragColor = vec4(0.0, 0.0, 0.0, 0.9);
}
)";

const char* uiVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec4 aColor;
out vec4 FragColor;
void main() {
    FragColor = aColor;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* uiFragmentShaderSource = R"(
#version 330 core
in vec4 FragColor;
out vec4 finalColor;
void main() {
    finalColor = FragColor;
}
)";

const char* texturedUIVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;
out vec2 TexCoords;
void main() {
    TexCoords = aTexCoords;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* texturedUIFragmentShaderSource = R"(
#version 330 core
in vec2 TexCoords;
out vec4 FragColor;
uniform sampler2D u_Texture;
void main() {
    vec4 texColor = texture(u_Texture, TexCoords);
    if (texColor.a < 0.01) discard;
    FragColor = texColor;
}
)";

const char* fontVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;
uniform mat4 projection;
out vec2 TexCoords;
void main() {
    TexCoords = aTexCoords;
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
}
)";

const char* fontFragmentShaderSource = R"(
#version 330 core
in vec2 TexCoords;
out vec4 FragColor;
uniform sampler2D fontTexture;
uniform vec4 textColor;
void main() {
    float alpha = texture(fontTexture, TexCoords).r;
    if (alpha < 0.05) discard;
    FragColor = vec4(textColor.rgb, textColor.a * alpha);
}
)";

const char* modelVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

out vec2 TexCoords;
out vec3 Normal;
out vec3 FragPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoords = aTexCoords;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

const char* modelFragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoords;
in vec3 Normal;
in vec3 FragPos;

uniform sampler2D texture_diffuse1;
uniform vec3 u_LightDir;
uniform vec3  u_UnderwaterTint;
uniform float u_UnderwaterAmount;

void main() {
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, -u_LightDir), 0.2f);
    vec4 texColor = texture(texture_diffuse1, TexCoords);
    if(texColor.a < 0.1) discard;

    vec3 litColor = texColor.rgb * diff * mix(vec3(1.0f), u_UnderwaterTint, u_UnderwaterAmount);
    FragColor = vec4(litColor, texColor.a);
}
)";

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    if (width == 0 || height == 0) return;
    glViewport(0, 0, width, height);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    if (g_ChatSystem && g_ChatSystem->isOpen) return;
    EngineContext* ctx = static_cast<EngineContext*>(glfwGetWindowUserPointer(window));
    if (!ctx || !ctx->player) return;

    if (settingsOpen()) {
        callLuaUI(ctx->lua, "handle_settings_scroll", yoffset);
        return;
    }
    if (g_CurrentGameState == GameState::WORLD_SELECT) {
        callLuaUI(ctx->lua, "handle_world_select_scroll", yoffset);
        return;
    }
    if (g_CurrentGameState == GameState::MODS) {
        callLuaUI(ctx->lua, "handle_mods_scroll", yoffset);
        return;
    }
    if (g_CurrentGameState == GameState::TEXTURE_PACKS) {
        callLuaUI(ctx->lua, "handle_texture_packs_scroll", yoffset);
        return;
    }

    if (g_Inventory.isCreativeOpen()) {
        // Creative inventory open: scroll its item grid (handled in inventory.lua)
        g_Inventory.handleCreativeScroll(yoffset);

        try {
            sol::state* lua = ctx->lua;
            if (lua) {
                sol::function scrollLua = (*lua)["on_scroll_input"];
                if (scrollLua.valid()) scrollLua(xoffset, yoffset);
            }
        } catch (...) {}
        return;
    }

    if (g_Hotbar) {
        if (yoffset > 0) g_Hotbar->previousSlot();
        else if (yoffset < 0) g_Hotbar->nextSlot();
    }
}

// Puts OpenGL into a clean state for 2D UI and text (menus, loading screen, chat).
void resetUIRenderState() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glBindVertexArray(0);
}

void drawLoadingScreen(GLuint uiProgram, GLuint fontProgram, float progress, int width, int height) {
    static GLuint vao = 0, vbo = 0;
    if (vao == 0) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    auto quad = [&](float x0, float y0, float x1, float y1, glm::vec4 c) {
        const float v[36] = {
            x0, y0, c.r, c.g, c.b, c.a,  x1, y0, c.r, c.g, c.b, c.a,  x1, y1, c.r, c.g, c.b, c.a,
            x0, y0, c.r, c.g, c.b, c.a,  x1, y1, c.r, c.g, c.b, c.a,  x0, y1, c.r, c.g, c.b, c.a
        };
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    };

    glUseProgram(uiProgram);
    quad(-1.0f, -1.0f, 1.0f, 1.0f, glm::vec4(0.12f, 0.12f, 0.12f, 1.0f));
    quad(-0.40f, -0.12f, 0.40f, -0.06f, glm::vec4(0.05f, 0.05f, 0.05f, 1.0f));
    const float fillX = -0.40f + 0.80f * glm::clamp(progress, 0.0f, 1.0f);
    quad(-0.40f, -0.12f, fillX, -0.06f, glm::vec4(0.35f, 0.75f, 0.35f, 1.0f));
    glBindVertexArray(0);

    if (g_FontRenderer) {
        const Dimensions2D screen{width, height};
        glUseProgram(fontProgram);
        g_FontRenderer->renderText(fontProgram, "Loading world...",
                                   width * 0.5f - 120.0f, height * 0.5f + 60.0f,
                                   1.2f, 1.2f, glm::vec4(1.0f), screen);
        const int percent = static_cast<int>(glm::clamp(progress, 0.0f, 1.0f) * 100.0f);
        g_FontRenderer->renderText(fontProgram, std::to_string(percent) + "%",
                                   width * 0.5f - 20.0f, height * 0.5f - 60.0f,
                                   0.9f, 0.9f, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f), screen);
    }
}

int main() {
    try {
        if (!glfwInit()) return -1;

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        GLFWwindow* window = glfwCreateWindow(1280, 720, "Craft & Ruin - Voxel Engine", NULL, NULL);
        if (!window) { glfwTerminate(); return -1; }
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1); // vsync (1 = once per screen refresh)

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "[Error] Failed to initialize GLAD\n";
            return -1;
        }

        glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
        glfwSetScrollCallback(window, scroll_callback);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

        g_Hotbar = new Hotbar();
        g_FontRenderer = new FontRenderer(baseResolution);
        g_ChatSystem = new ChatSystem();
        g_LightingSystem = new LightingSystem();
        g_Skybox = new Skybox();
        g_Skybox->init();
        g_Clouds = new Clouds();
        g_Clouds->init();
        g_MainMenu = new MainMenu();

        g_FontRenderer->init("fonts/PixeloidSans.ttf");

        EngineContext ctx;
        glfwSetWindowUserPointer(window, &ctx);

        glfwSetCharCallback(window, [](GLFWwindow* win, unsigned int codepoint) {
            if (g_ChatSystem && g_ChatSystem->isOpen) {
                g_ChatSystem->handleCharInput(codepoint);
                return;
            }

            // Creative inventory search box (inventory.lua decides if it wants the character)
            if (g_CurrentGameState == GameState::PLAYING && g_Inventory.handleCreativeChar(codepoint)) {
                return;
            }

            EngineContext* c = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));

            // World select screen text fields (world name / seed)
            if (g_CurrentGameState == GameState::WORLD_SELECT) {
                std::string ch;
                if (codepoint < 128) ch.push_back(static_cast<char>(codepoint));
                if (c) callLuaUI(c->lua, "handle_world_select_char", static_cast<int>(codepoint), ch);
                return;
            }

            if (c && c->lua) {
                try {
                    sol::state* lua = c->lua;
                    sol::function charLua = (*lua)["on_char_input"];
                    if (charLua.valid()) charLua(codepoint);
                } catch (...) {}
            }
        });

        glfwSetKeyCallback(window, [](GLFWwindow* win, int key, int scancode, int action, int mods) {
            EngineContext* c = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));
            if (!c) return;

            if (g_ChatSystem && g_ChatSystem->isOpen) {
                g_ChatSystem->handleKeyInput(key, scancode, action, mods, win);
                return;
            }

            // Settings (title screen or pause menu): Lua gets every key first, so it can
            // capture a key for rebinding. Otherwise Escape closes the settings.
            if (settingsOpen()) {
                if (callLuaUI(c->lua, "handle_settings_key", key, action, mods)) return;
                if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) closeSettings();
                return;
            }

            // While typing in the creative search box, inventory.lua gets the keys first
            // (so 'E', 'T' or Escape don't close the inventory / open chat mid-typing).
            if (g_CurrentGameState == GameState::PLAYING && g_Inventory.handleCreativeKey(key, action, mods)) {
                return;
            }

            // World select: Lua gets the key first; otherwise Escape goes back to the title
            if (g_CurrentGameState == GameState::WORLD_SELECT) {
                if (callLuaUI(c->lua, "handle_world_select_key", key, action, mods)) return;
                if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) g_CurrentGameState = GameState::MENU;
                return;
            }

            // Mods screen: same idea
            if (g_CurrentGameState == GameState::MODS) {
                if (callLuaUI(c->lua, "handle_mods_key", key, action, mods)) return;
                if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) g_CurrentGameState = GameState::MENU;
                return;
            }

            // Texture packs screen
            if (g_CurrentGameState == GameState::TEXTURE_PACKS) {
                if (callLuaUI(c->lua, "handle_texture_packs_key", key, action, mods)) return;
                if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE) g_CurrentGameState = GameState::MENU;
                return;
            }

            if (g_CurrentGameState == GameState::MENU && action == GLFW_PRESS) {
                if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(win, true);
                return;
            }

            if (g_CurrentGameState == GameState::PLAYING && action == GLFW_PRESS) {
                // While paused, only Escape does anything (it resumes the game)
                if (g_Paused) {
                    if (key == GLFW_KEY_ESCAPE) {
                        g_Paused = false;
                        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    }
                    return;
                }

                // Keys come from the Controls settings (see keyActions())
                if (key == keyForAction("chat") || key == keyForAction("command")) {
                    // The chat system expects T (plain chat) or Slash (starts with "/")
                    g_ChatSystem->openChat(win, key == keyForAction("command") ? GLFW_KEY_SLASH : GLFW_KEY_T);
                    return;
                }

                if (key == keyForAction("inventory")) {
                    // Use native C++ inventory toggle & cursor update
                    g_Inventory.toggleCreative();
                    int currentMode = glfwGetInputMode(win, GLFW_CURSOR);
                    if (currentMode == GLFW_CURSOR_DISABLED) glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    else glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

                    try {
                        sol::state* lua = c->lua;
                        sol::function toggleInv = (*lua)["toggle_main_inventory"];
                        if (toggleInv.valid()) toggleInv();
                    } catch (...) {}
                    return;
                }

                if (key == GLFW_KEY_ESCAPE) {
                    // Escape closes the inventory first; otherwise it opens the pause menu
                    if (g_Inventory.isCreativeOpen()) {
                        g_Inventory.setCreativeOpen(false);
                        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        return;
                    }
                    g_Paused = true;
                    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                }
            }
        });

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        initTextureArray();

        sol::state lua;
        lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::math, sol::lib::string, sol::lib::table);

        auto compileShader = [](GLenum type, const char* src) {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &src, NULL);
            glCompileShader(shader);
            return shader;
        };

        GLuint uiProgram = glCreateProgram();
        glAttachShader(uiProgram, compileShader(GL_VERTEX_SHADER, uiVertexShaderSource));
        glAttachShader(uiProgram, compileShader(GL_FRAGMENT_SHADER, uiFragmentShaderSource));
        glLinkProgram(uiProgram);

        GLuint texturedUIProgram = glCreateProgram();
        glAttachShader(texturedUIProgram, compileShader(GL_VERTEX_SHADER, texturedUIVertexShaderSource));
        glAttachShader(texturedUIProgram, compileShader(GL_FRAGMENT_SHADER, texturedUIFragmentShaderSource));
        glLinkProgram(texturedUIProgram);

        GLuint fontProgram = glCreateProgram();
        glAttachShader(fontProgram, compileShader(GL_VERTEX_SHADER, fontVertexShaderSource));
        glAttachShader(fontProgram, compileShader(GL_FRAGMENT_SHADER, fontFragmentShaderSource));
        glLinkProgram(fontProgram);

        GLuint modelShaderProgram = glCreateProgram();
        glAttachShader(modelShaderProgram, compileShader(GL_VERTEX_SHADER, modelVertexShaderSource));
        glAttachShader(modelShaderProgram, compileShader(GL_FRAGMENT_SHADER, modelFragmentShaderSource));
        glLinkProgram(modelShaderProgram);

        // ------------------------------------------------------------------
        // ui.* table
        // ------------------------------------------------------------------
        auto uiTable = lua.create_named_table("ui");

        uiTable["get_mouse_pos"] = [window]() {
            double x, y;
            glfwGetCursorPos(window, &x, &y);
            int framebufferWidth, framebufferHeight;
            glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
            scaleCursorToFramebuffer(window, x, y, framebufferWidth, framebufferHeight);
            return std::make_tuple(x, y);
        };

        // Adds ui.draw_rect, ui.draw_block_icon, ui.get_all_blocks, ui.get_block_info,
        // ui.get_time, ui.keys, ui.actions (and a 0-based hotbar.* that gets replaced below).
        g_Inventory.registerLuaBindings(lua);

        // ui.draw_text - uses the real font shader. Supports both call styles:
        //   ui.draw_text(text, x, y, r, g, b)                    (old style, scale 1)
        //   ui.draw_text(text, x, y, scale, r, g, b [, a])       (new style)
        uiTable["draw_text"] = [&](const std::string& text, float x, float y, sol::variadic_args va) {
            if (!g_FontRenderer) return;

            std::vector<float> v;
            for (auto arg : va) v.push_back(arg.as<float>());

            float scale = 1.0f, r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
            if (v.size() == 3) {
                r = v[0]; g = v[1]; b = v[2];
            } else if (v.size() >= 4) {
                scale = v[0]; r = v[1]; g = v[2]; b = v[3];
                if (v.size() >= 5) a = v[4];
            }

            Dimensions2D windowSize;
            glfwGetFramebufferSize(window, &windowSize.width, &windowSize.height);

            resetUIRenderState();
            glUseProgram(fontProgram);
            g_FontRenderer->renderText(fontProgram, text, x, y, scale, scale, glm::vec4(r, g, b, a), windowSize);
        };

        // ui.get_mouse() -> x, y, left_down, right_down   (framebuffer pixels, same as clicks)
        uiTable["get_mouse"] = [window]() {
            double x, y;
            glfwGetCursorPos(window, &x, &y);
            int fw, fh;
            glfwGetFramebufferSize(window, &fw, &fh);
            scaleCursorToFramebuffer(window, x, y, fw, fh);
            const bool left  = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
            const bool right = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
            return std::make_tuple(x, y, left, right);
        };

        lua.new_usertype<Bone>("Bone",
            "set_rotation", [](Bone& b, float x, float y, float z) { b.rotation = glm::vec3(x, y, z); },
            "get_rotation", [](Bone& b) { return std::make_tuple(b.rotation.x, b.rotation.y, b.rotation.z); },
            "set_position", [](Bone& b, float x, float y, float z) { b.position = glm::vec3(x, y, z); }
        );

        lua.new_usertype<AnimatedModel>("Model",
            "get_bone", &AnimatedModel::getBone
        );

        // ------------------------------------------------------------------
        // hotbar.* table - ALL slot numbers are 1-based (Lua convention)
        // ------------------------------------------------------------------
        sol::table hotbarTable = lua.create_named_table("hotbar");
        hotbarTable["set_size"] = [](int size) { if (g_Hotbar) g_Hotbar->resize(size); };
        hotbarTable["get_size"] = []() { return g_Hotbar ? g_Hotbar->slotCount : 9; };
        hotbarTable["get_slot_count"] = []() { return g_Hotbar ? g_Hotbar->slotCount : 9; };
        hotbarTable["set_slot_size"] = [](float size) { if (g_Hotbar) g_Hotbar->slotSize = size; };
        hotbarTable["set_position"] = [](float x, float y) { if (g_Hotbar) { g_Hotbar->posX = x; g_Hotbar->posY = y; } };

        // hotbar.set_slot(slot, name [, count])  - name "" or "air" clears the slot
        hotbarTable["set_slot"] = [](int slotIndex, const std::string& blockName, sol::optional<int> count) {
            if (!g_Hotbar) return;
            int adj = slotIndex - 1;
            if (adj < 0 || adj >= g_Hotbar->slotCount) return;
            uint8_t blockId = (blockName.empty() || blockName == "air")
                ? 0 : BlockRegistry::getInstance().getBlockId(blockName);
            g_Hotbar->setSlot(adj, blockId, blockId == 0 ? 0 : count.value_or(64));
        };

        // hotbar.get_slot(slot) -> name, count   ("" and 0 when empty)
        hotbarTable["get_slot"] = [](int slotIndex) {
            if (!g_Hotbar) return std::make_tuple(std::string(), 0);
            HotbarSlot slot = g_Hotbar->getSlot(slotIndex - 1);
            if (slot.blockId == 0) return std::make_tuple(std::string(), 0);
            return std::make_tuple(BlockRegistry::getInstance().getBlock(slot.blockId).name, slot.count);
        };

        hotbarTable["get_active_slot"] = []() { return g_Hotbar ? g_Hotbar->activeSlot + 1 : 1; };
        hotbarTable["set_active_slot"] = [](int slotIndex) {
            if (g_Hotbar) {
                int adj = slotIndex - 1;
                if (adj >= 0 && adj < g_Hotbar->slotCount) g_Hotbar->activeSlot = adj;
            }
        };
        hotbarTable["is_creative_open"] = []() { return g_Inventory.isCreativeOpen(); };
        hotbarTable["toggle_creative"] = []() { g_Inventory.toggleCreative(); };
        hotbarTable["get_slot_block"] = [](int slotIndex) -> std::string {
            if (!g_Hotbar) return "air";
            int adj = slotIndex - 1;
            HotbarSlot slot = g_Hotbar->getSlot(adj);
            if (slot.blockId == 0) return "air";
            return BlockRegistry::getInstance().getBlock(slot.blockId).name;
        };
        hotbarTable["set_texture"] = [](std::string path) {
            if (g_Hotbar) g_Hotbar->customTextureID = loadStandardTexture(path);
        };
        hotbarTable["set_selected_texture"] = [](std::string path) {
            if (g_Hotbar) g_Hotbar->selectedTextureID = loadStandardTexture(path);
        };
        hotbarTable["set_texture_scale"] = [](float s) {
            if (g_Hotbar) g_Hotbar->textureScale = glm::clamp(s, 0.25f, 4.0f);
        };

        auto skyTable = lua.create_named_table("sky");
        skyTable["set_time"] = [](float time) { if (g_LightingSystem) g_LightingSystem->customTimeOfDay = time; };
        skyTable["get_time"] = []() { return g_LightingSystem ? g_LightingSystem->getTimeOfDay() : 0.0f; };
        skyTable["set_moon_phase"] = [](int phase) { if (g_LightingSystem) g_LightingSystem->forcedMoonPhase = phase; };
        skyTable["get_moon_phase"] = []() { return g_LightingSystem ? g_LightingSystem->getMoonPhase() : 0; };
        skyTable["set_moon_textures"] = [](sol::table paths) {
            if (!g_LightingSystem) return;
            std::vector<std::string> pList;
            for (int i = 0; i < paths.size(); ++i) pList.push_back(paths[i + 1]);
            g_LightingSystem->setMoonPhaseTextures(pList);
        };
        skyTable["set_rain_intensity"] = [](float intensity) {
            if (g_LightingSystem) g_LightingSystem->weatherRainIntensity = glm::clamp(intensity, 0.0f, 1.0f);
        };
        skyTable["get_rain_intensity"] = []() { return g_LightingSystem ? g_LightingSystem->weatherRainIntensity : 0.0f; };
        skyTable["set_sun_color"] = [](float r, float g, float b) { if (g_LightingSystem) g_LightingSystem->setSunColor(r, g, b); };
        skyTable["set_ambient_color"] = [](float r, float g, float b) { if (g_LightingSystem) g_LightingSystem->setAmbientColor(r, g, b); };
        skyTable["set_fog_color"] = [](float r, float g, float b) { if (g_LightingSystem) g_LightingSystem->setFogColor(r, g, b); };
        skyTable["set_sun_intensity"] = [](float intensity) { if (g_LightingSystem) g_LightingSystem->setSunIntensity(intensity); };
        skyTable["set_ambient_intensity"] = [](float intensity) { if (g_LightingSystem) g_LightingSystem->setAmbientIntensity(intensity); };
        skyTable["reset"] = []() { if (g_LightingSystem) g_LightingSystem->resetToDefaults(); };
        skyTable["add_light"] = [](float x, float y, float z, float r, float g, float b, float radius, float intensity) {
            return g_LightingSystem ? g_LightingSystem->addPointLight(glm::vec3(x, y, z), glm::vec3(r, g, b), radius, intensity) : -1;
        };
        skyTable["remove_light"] = [](int id) { if (g_LightingSystem) g_LightingSystem->removePointLight(id); };

        auto cloudTable = lua.create_named_table("clouds");
        cloudTable["set_color"] = [](float r, float g, float b) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.color = glm::vec3(r, g, b); g_Clouds->setSettings(s);
        };
        cloudTable["set_night_color"] = [](float r, float g, float b) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.nightColor = glm::vec3(r, g, b); g_Clouds->setSettings(s);
        };
        cloudTable["set_height"] = [](float y) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.height = y; g_Clouds->setSettings(s);
        };
        cloudTable["set_thickness"] = [](float t) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.thickness = t; g_Clouds->setSettings(s);
        };
        cloudTable["set_cell_size"] = [](float c) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.cellSize = c; g_Clouds->setSettings(s);
        };
        cloudTable["set_threshold"] = [](float t) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.threshold = t; g_Clouds->setSettings(s);
        };
        cloudTable["set_scroll_speed"] = [](float v) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.scrollSpeed = v; g_Clouds->setSettings(s);
        };
        cloudTable["set_texture"] = [](const std::string& path) {
            if (!g_Clouds) return;
            CloudSettings s = g_Clouds->settings(); s.texturePath = path; g_Clouds->setSettings(s);
        };
        cloudTable["set_weather"] = [](const std::string& name, float seconds) {
            if (g_Clouds) g_Clouds->setWeather(name, seconds);
        };

        auto fogTable = lua.create_named_table("fog");
        fogTable["set_enabled"]      = [](bool on) { g_Fog.enabled = on; };
        fogTable["set_exponential"]  = [](bool on) { g_Fog.exponential = on; };
        fogTable["set_range"]        = [](float s, float e) { g_Fog.start = s; g_Fog.end = e; };
        fogTable["set_density"]      = [](float d) { g_Fog.density = d; };
        fogTable["set_rain_scale"]   = [](float s) { g_Fog.rainScale = glm::clamp(s, 0.0f, 1.0f); };
        fogTable["set_day_color"]    = [](float r, float g, float b) { g_Fog.dayColor = glm::vec3(r, g, b); };
        fogTable["set_sunset_color"] = [](float r, float g, float b) { g_Fog.sunsetColor = glm::vec3(r, g, b); };
        fogTable["set_night_color"]  = [](float r, float g, float b) { g_Fog.nightColor = glm::vec3(r, g, b); };

        auto chatTable = lua.create_named_table("chat");
        chatTable["register_command"] = [](const std::string& name,
                                          const std::string& usage,
                                          const std::string& description) {
            if (g_ChatSystem) g_ChatSystem->registerCommand(name, usage, description);
        };
        chatTable["send"] = [](const std::string& text,
                               sol::optional<float> r,
                               sol::optional<float> g,
                               sol::optional<float> b) {
            if (!g_ChatSystem) return;
            if (r.has_value() && g.has_value() && b.has_value()) {
                g_ChatSystem->addMessage(text, glm::vec3(*r, *g, *b));
            } else {
                g_ChatSystem->addMessage(text);
            }
        };

        g_ChatSystem->setCommandHandler([&lua](const std::string& command) -> bool {
            try {
                sol::protected_function fn = lua["on_chat_command"];
                if (!fn.valid()) return false;
                sol::protected_function_result result = fn(command);
                if (!result.valid()) return false;
                if (result.get_type() == sol::type::boolean) return result.get<bool>();
                return false;
            } catch (...) {
                return false;
            }
        });

        auto worldTable = lua.create_named_table("world");
        worldTable["set_render_distance"] = [](int chunks) { if (g_WorldInstance) g_WorldInstance->setRenderDistance(chunks); };
        worldTable["get_render_distance"] = []() { return g_WorldInstance ? g_WorldInstance->renderDistance() : 10; };
        worldTable["set_load_budget"]      = [](float ms) { if (g_WorldInstance) g_WorldInstance->setLoadBudgetMs(ms); };
        worldTable["set_mesh_budget"]      = [](float ms) { if (g_WorldInstance) g_WorldInstance->setMeshBudgetMs(ms); };
        worldTable["get_loaded_chunks"]   = []() { return g_WorldInstance ? static_cast<int>(g_WorldInstance->loadedChunkCount()) : 0; };
        worldTable["get_pending_chunks"]  = []() { return g_WorldInstance ? static_cast<int>(g_WorldInstance->pendingLoadCount()) : 0; };
        // world.set_caves_enabled(false) turns off the built-in caves (e.g. if your mapgen makes its own)
        worldTable["set_caves_enabled"]   = [](bool on) { CaveGenerator::setEnabled(on); };
        worldTable["get_caves_enabled"]   = []() { return CaveGenerator::isEnabled(); };

        auto underwaterTable = lua.create_named_table("underwater");
        underwaterTable["set_shallow_color"] = [](float r, float g, float b) { g_Underwater.shallowColor = glm::vec3(r, g, b); };
        underwaterTable["set_deep_color"]    = [](float r, float g, float b) { g_Underwater.deepColor = glm::vec3(r, g, b); };
        underwaterTable["set_visibility"]    = [](float shallow, float deep) {
            g_Underwater.shallowVisibility = glm::max(shallow, 1.0f);
            g_Underwater.deepVisibility    = glm::max(deep, 1.0f);
        };
        underwaterTable["set_full_depth"]    = [](float d) { g_Underwater.fullDepth = glm::max(d, 1.0f); };
        underwaterTable["set_fade"]          = [](float inSeconds, float outSeconds) {
            g_Underwater.fadeInSeconds  = glm::max(inSeconds, 0.01f);
            g_Underwater.fadeOutSeconds = glm::max(outSeconds, 0.01f);
        };
        underwaterTable["set_tint_strength"] = [](float s) { g_Underwater.tintStrength = glm::clamp(s, 0.0f, 1.0f); };

        auto menuTable = lua.create_named_table("menu");
        menuTable["get_games"] = [&]() {
            auto games = getAvailableGames();
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& g : games) {
                sol::table gt = lua.create_table();
                gt["folder"] = g.folderName;
                gt["title"] = g.title;
                gt["author"] = g.author;
                gt["description"] = g.description;
                t[idx++] = gt;
            }
            return t;
        };
        menuTable["get_active_game"] = []() { return g_ActiveGameFolder; };
        menuTable["set_active_game"] = [](std::string folderName) { g_ActiveGameFolder = folderName; };

        auto engineTable = lua.create_named_table("engine");
        engineTable["register_block"] = &registerBlockFromLua;
        engineTable["get_block_id"] = &getBlockIdLua;
        engineTable["set_fov"] = [](float fov) { g_FieldOfView = fov; };
        engineTable["set_sun_texture"] = [](std::string path) { if (g_LightingSystem) g_LightingSystem->setSunTexture(path); };
        engineTable["set_moon_texture"] = [](std::string path) { if (g_LightingSystem) g_LightingSystem->setMoonTexture(path); };

        // engine.load_model(path [, texture [, scale]])
        //   path    - .json (Bedrock geometry) or .b3d (Luanti mesh), found in the mod's folder or models/
        //   texture - image for the model; defaults to the one named inside a .b3d
        //   scale   - size multiplier; .b3d models default to 0.1 (10 units = 1 block)
        engineTable["load_model"] = [](std::string path, sol::optional<std::string> texture, sol::optional<float> scale) {
            auto model = std::make_shared<AnimatedModel>();
            const std::string resolved = resolveAssetPath(path, "models");
            if (resolved.empty()) {
                std::cerr << "[Model Error] Model file not found: " << path
                          << " (looked in the mod's folder and its models/ folder)" << std::endl;
                return model;
            }
            model->loadFromFile(resolved);

            const std::string tex = texture.value_or(model->textureHint);
            if (!tex.empty()) {
                const std::string texPath = resolveAssetPath(tex, "textures");
                model->texture = loadStandardTexture(texPath.empty() ? tex : texPath);
            }
            if (scale.has_value()) model->scale = scale.value();
            return model;
        };

        engineTable["set_block"] = [](int x, int y, int z, const std::string& blockName) {
            if (g_WorldInstance) {
                uint8_t id = BlockRegistry::getInstance().getBlockId(blockName);
                g_WorldInstance->setBlockAt(glm::ivec3(x, y, z), id);
            }
        };
        engineTable["get_block_name"] = [](int x, int y, int z) -> std::string {
            if (g_WorldInstance) {
                uint8_t id = g_WorldInstance->getBlockAt(glm::ivec3(x, y, z));
                if (id == 0) return "air";
                return BlockRegistry::getInstance().getBlock(id).name;
            }
            return "air";
        };

        engineTable["draw_text"] = [&](std::string text, float x, float y) {
            if (g_FontRenderer) {
                Dimensions2D windowSize;
                glfwGetFramebufferSize(window, &windowSize.width, &windowSize.height);
                g_FontRenderer->renderText(fontProgram, text, x, y, 1.0f, 1.0f, glm::vec4(1.0f), windowSize);
            }
        };

        engineTable["get_worlds"] = [&]() {
            auto worlds = getAvailableWorlds();
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& w : worlds) t[idx++] = w;
            return t;
        };

        engineTable["get_active_world"] = []() { return g_ActiveWorldName; };
        engineTable["set_active_world"] = [](std::string name) { g_ActiveWorldName = name; };
        engineTable["quit"] = [&]() { glfwSetWindowShouldClose(window, true); };

        // Pause menu controls (used by pause_menu.lua)
        engineTable["is_paused"] = []() { return g_Paused; };
        engineTable["pause"] = [window]() {
            if (g_CurrentGameState != GameState::PLAYING) return;
            g_Paused = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        };
        engineTable["resume"] = [window]() {
            g_Paused = false;
            g_SettingsInGame = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        };

        // ---- Settings (used by settings_menu.lua and pause_menu.lua) ----
        // engine.open_settings() - from the pause menu (the title screen's Settings button opens it too)
        engineTable["open_settings"] = [&lua]() {
            if (g_CurrentGameState == GameState::PLAYING) {
                g_Paused = true;
                g_SettingsInGame = true;
            } else {
                g_CurrentGameState = GameState::SETTINGS;
            }
            callLuaUI(&lua, "on_settings_opened");
        };
        engineTable["close_settings"] = []() { closeSettings(); };
        engineTable["is_in_game"] = []() { return g_CurrentGameState == GameState::PLAYING; };

        // engine.get_keybinds() -> { { action, label, key, key_name, default_key }, ... } in display order
        engineTable["get_keybinds"] = [&lua]() {
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& a : keyActions()) {
                sol::table e = lua.create_table();
                const int key = keyForAction(a.id);
                e["action"] = std::string(a.id);
                e["label"] = std::string(a.label);
                e["key"] = key;
                e["key_name"] = keyName(key);
                e["default_key"] = a.defaultKey;
                t[idx++] = e;
            }
            return t;
        };
        // engine.set_keybind(action, key) -> true, or false + reason
        engineTable["set_keybind"] = [](const std::string& action, int key) {
            std::string error;
            const bool ok = setKeybind(action, key, error);
            return std::make_tuple(ok, error);
        };
        engineTable["reset_keybinds"] = []() { resetKeybinds(); };
        engineTable["key_name"] = [](int key) { return keyName(key); };

        // engine.get_video_settings() -> { { id, value, min, max, step }, ... }
        engineTable["get_video_settings"] = [&lua]() {
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& s : kVideoSettings) {
                sol::table e = lua.create_table();
                e["id"] = std::string(s.id);
                e["value"] = currentVideoSetting(s.id);
                e["min"] = s.minV;
                e["max"] = s.maxV;
                e["step"] = s.step;
                t[idx++] = e;
            }
            return t;
        };
        // engine.set_video_setting(id, value) -> true, or false + reason. Applies immediately.
        engineTable["set_video_setting"] = [](const std::string& id, int value) {
            std::string error;
            const bool ok = setVideoSetting(id, value, error);
            return std::make_tuple(ok, error);
        };

        engineTable["quit_to_menu"] = [window]() {
            g_SettingsInGame = false;
            // Save, then unload the world so the next one starts clean
            if (g_CurrentGameState == GameState::PLAYING) saveCurrentWorld();
            if (g_WorldInstance) {
                g_WorldInstance->reset();
                g_WorldInstance->setSaveDirectory("");
            }
            g_Paused = false;
            g_Inventory.setCreativeOpen(false);
            g_CurrentGameState = GameState::MENU;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        };
        engineTable["start_game"] = [window]() { beginLoadingWorld(window); };

        // ---- World saves (used by world_select.lua) ----
        // engine.list_worlds() -> { { name = "My World", seed = "123" }, ... }  newest first
        engineTable["list_worlds"] = [&lua]() {
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& w : listSavedWorlds()) {
                sol::table e = lua.create_table();
                e["name"] = w.name;
                e["seed"] = w.seed;
                t[idx++] = e;
            }
            return t;
        };
        // engine.create_world(name, seedText) -> folder name actually used, or nil + error
        engineTable["create_world"] = [&lua](const std::string& name, sol::optional<std::string> seed) {
            if (!selectedGameIsLoaded()) {
                return std::make_tuple(sol::make_object(lua, sol::lua_nil),
                                       std::string("Restart the game to create worlds for this game."));
            }
            return std::make_tuple(sol::make_object(lua, createWorld(name, seed.value_or(""))), std::string());
        };
        // engine.play_world(name) -> true, or false + error message
        engineTable["play_world"] = [window](const std::string& name) {
            if (!selectedGameIsLoaded()) {
                return std::make_tuple(false, std::string("Restart the game to play worlds for this game."));
            }
            WorldMeta meta;
            std::string why;
            if (readWorldMeta(savesRoot() / name / "world.dat", meta) && !worldBelongsToLoadedGame(meta, &why)) {
                return std::make_tuple(false, why);
            }
            g_ActiveWorldName = name;
            beginLoadingWorld(window);
            return std::make_tuple(true, std::string());
        };
        // engine.delete_world(name) -> true, or false + error message
        engineTable["delete_world"] = [](const std::string& name) {
            std::string error;
            const bool ok = deleteWorld(name, error);
            return std::make_tuple(ok, error);
        };
        // engine.get_game_info() -> { selected, loaded, title, can_play }
        engineTable["get_game_info"] = [&lua]() {
            sol::table t = lua.create_table();
            std::string title = g_ActiveGameFolder;
            for (const auto& g : getAvailableGames()) {
                if (g.folderName == g_ActiveGameFolder) { title = g.title; break; }
            }
            t["selected"] = g_ActiveGameFolder;
            t["loaded"] = g_LoadedGameFolder;
            t["title"] = title;
            t["can_play"] = selectedGameIsLoaded();
            return t;
        };
        engineTable["save_world"] = []() {
            if (g_CurrentGameState == GameState::PLAYING) saveCurrentWorld();
        };
        engineTable["back_to_title"] = []() { g_CurrentGameState = GameState::MENU; };

        // ---- Mods screen (used by mods_menu.lua) ----
        // engine.list_mods() -> { { id, title, author, description, version, depends,
        //                           source, path, status, error, lua_files, is_folder }, ... }
        // status is "loaded", "error" or "not_loaded"
        engineTable["list_mods"] = [&lua]() {
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& m : listInstalledMods()) {
                sol::table e = lua.create_table();
                e["id"] = m.id;
                e["title"] = m.title;
                e["author"] = m.author;
                e["description"] = m.description;
                e["version"] = m.version;
                e["depends"] = m.depends;
                e["source"] = m.source;
                e["path"] = m.path;
                e["status"] = m.status;
                e["error"] = m.error;
                e["lua_files"] = m.luaFiles;
                e["is_folder"] = m.isFolder;
                e["enabled"] = m.enabled;
                e["required"] = m.required;
                e["restart_needed"] = m.restartNeeded;
                t[idx++] = e;
            }
            return t;
        };
        engineTable["open_mods_folder"] = []() { openModsFolder(); };

        // ---- Texture packs (used by texture_packs.lua) ----
        // engine.list_texture_packs() -> { { id, title, author, description, path, images, replaces, active }, ... }
        //   The first entry is always the default textures (id = "").
        engineTable["list_texture_packs"] = [&lua]() {
            sol::table t = lua.create_table();
            int idx = 1;
            for (const auto& p : listTexturePacks()) {
                sol::table e = lua.create_table();
                e["id"] = p.id;
                e["title"] = p.title;
                e["author"] = p.author;
                e["description"] = p.description;
                e["path"] = p.path;
                e["images"] = p.images;
                e["replaces"] = p.replaces;
                e["total"] = static_cast<int>(g_LayerSources.size() + g_StandardTextureSources.size());
                e["active"] = (p.id == g_ActiveTexturePack);
                t[idx++] = e;
            }
            return t;
        };
        // engine.set_texture_pack(id) -> true, or false + reason. "" = default. Applies immediately.
        engineTable["set_texture_pack"] = [](const std::string& id) {
            std::string error;
            const bool ok = setTexturePack(id, error);
            return std::make_tuple(ok, error);
        };
        engineTable["get_texture_pack"] = []() { return g_ActiveTexturePack; };
        engineTable["open_texture_packs_folder"] = []() { openTexturePacksFolder(); };
        // engine.set_mod_enabled(id, on) -> true, or false + reason. Applies on the next start.
        engineTable["set_mod_enabled"] = [](const std::string& id, bool on) {
            std::string error;
            const bool ok = setModEnabled(id, on, error);
            return std::make_tuple(ok, error);
        };

        World world;
        g_WorldInstance = &world;

        // Worlds are tied to the game whose mods are loaded now (see openActiveWorld)
        g_LoadedGameFolder = g_ActiveGameFolder;

        // Player settings (key bindings, FOV, ...) from settings.txt
        loadSettings();

        // The chosen texture pack must be known before mods load their textures
        g_ActiveTexturePack = readActiveTexturePack();
        if (!g_ActiveTexturePack.empty()) {
            std::cout << "[Texture] Using texture pack '" << g_ActiveTexturePack << "'" << std::endl;
        }

        std::vector<std::string> modSearchPaths = { "mods", "../mods", "../../mods", "games/" + g_ActiveGameFolder + "/mods" };
        std::vector<fs::path> modFiles;
        std::set<std::string> initFolders;

        std::set<std::string> seenModRoots;   // the same folder reached by two paths is only loaded once
        for (const auto& searchPath : modSearchPaths) {
            std::string resolvedPath = searchPath;
            if (!fs::exists(resolvedPath) && fs::exists("../../" + searchPath)) resolvedPath = "../../" + searchPath;
            if (!fs::exists(resolvedPath)) continue;

            const std::string rootKey = modPathKey(resolvedPath);
            if (!seenModRoots.insert(rootKey).second) {
                std::cout << "[Mod] Search path '" << searchPath << "' leads to " << rootKey
                          << ", which is already loaded - skipping it" << std::endl;
                continue;
            }
            std::cout << "[Mod] Mod folder: " << rootKey << " (search path '" << searchPath << "')" << std::endl;

            // Mods in the mods/ folder can be switched off on the Mods screen; game mods always load.
            const bool isGlobalFolder = searchPath.rfind("games/", 0) != 0;
            std::set<std::string> reportedDisabled;

            for (const auto& entry : fs::recursive_directory_iterator(resolvedPath)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".lua") continue;

                if (isGlobalFolder) {
                    std::error_code relEc;
                    const fs::path rel = fs::relative(entry.path(), resolvedPath, relEc);
                    const std::string modId = (relEc || rel.empty()) ? "" : rel.begin()->string();
                    if (!modId.empty()) {
                        const bool on = modIsEnabled(resolvedPath, modId);
                        g_ModEnabledAtStartup[modId] = on;
                        if (!on) {
                            if (reportedDisabled.insert(modId).second) {
                                std::cout << "[Mod] Skipping disabled mod: " << modId << std::endl;
                            }
                            continue;
                        }
                    }
                }

                if (entry.path().filename() == "init.lua") {
                    initFolders.insert(entry.path().parent_path().string());
                }
                modFiles.push_back(entry.path());
            }
        }

        auto isInsideInitFolder = [&initFolders](const fs::path& file) {
            for (fs::path p = file.parent_path(); ; p = p.parent_path()) {
                if (initFolders.count(p.string())) return true;
                if (p.empty() || p == p.parent_path()) return false;
            }
        };

        auto runModFile = [&](const fs::path& file) {
            g_CurrentModPath = file.parent_path().string();
            lua["g_CurrentModPath"] = g_CurrentModPath;
            std::cout << "[Mod] Running " << modPathKey(file) << std::endl;
            try {
                lua.script_file(file.string());
                g_ModFileStatus[modPathKey(file)] = "";              // ran OK (shown on the Mods screen)
            } catch (const std::exception& e) {
                std::cerr << "[Mod] Failed to load " << file.string() << ": " << e.what() << std::endl;
                g_ModFileStatus[modPathKey(file)] = e.what();
            }
        };

        for (const auto& file : modFiles) {
            if (file.filename() == "init.lua") runModFile(file);
        }
        for (const auto& file : modFiles) {
            if (file.filename() == "init.lua" || isInsideInitFolder(file)) continue;
            runModFile(file);
        }

        // Saved video settings win over values set by mods during loading
        applyStoredVideoSettings();

        lua.new_usertype<ChunkMesh>("Chunk",
            "set_block", &ChunkMesh::setBlock, "setBlock", &ChunkMesh::setBlock,
            "get_block", &ChunkMesh::getBlock, "getBlock", &ChunkMesh::getBlock,
            "set_block_with_meta", &ChunkMesh::setBlockWithMeta,
            "set_metadata", &ChunkMesh::setMetadata, "get_metadata", &ChunkMesh::getMetadata,
            "generate_mesh_data", &ChunkMesh::generateMeshData, "upload_mesh", &ChunkMesh::uploadMesh
        );

        GLuint shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, compileShader(GL_VERTEX_SHADER, vertexShaderSource));
        glAttachShader(shaderProgram, compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource));
        glLinkProgram(shaderProgram);

        GLuint sunProgram = glCreateProgram();
        glAttachShader(sunProgram, compileShader(GL_VERTEX_SHADER, sunVertexShaderSource));
        glAttachShader(sunProgram, compileShader(GL_FRAGMENT_SHADER, sunFragmentShaderSource));
        glLinkProgram(sunProgram);

        GLuint wireframeProgram = glCreateProgram();
        glAttachShader(wireframeProgram, compileShader(GL_VERTEX_SHADER, wireframeVertexShaderSource));
        glAttachShader(wireframeProgram, compileShader(GL_FRAGMENT_SHADER, wireframeFragmentShaderSource));
        glLinkProgram(wireframeProgram);

        float sunVertices[] = { -12.0f, 12.0f, 0.0f, 0.0f, 1.0f, -12.0f, -12.0f, 0.0f, 0.0f, 0.0f, 12.0f, -12.0f, 0.0f, 1.0f, 0.0f, 12.0f, 12.0f, 0.0f, 1.0f, 1.0f };
        unsigned int sunIndices[] = { 0, 1, 2, 2, 3, 0 };
        GLuint sunVAO, sunVBO, sunEBO;
        glGenVertexArrays(1, &sunVAO); glGenBuffers(1, &sunVBO); glGenBuffers(1, &sunEBO);
        glBindVertexArray(sunVAO);
        glBindBuffer(GL_ARRAY_BUFFER, sunVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(sunVertices), sunVertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sunEBO); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(sunIndices), sunIndices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
        glBindVertexArray(0);

        float crosshairVertices[] = { -0.015f, 0.0f, 1,1,1,0.8f, 0.015f, 0.0f, 1,1,1,0.8f, 0.0f, -0.025f, 1,1,1,0.8f, 0.0f, 0.025f, 1,1,1,0.8f };
        GLuint crosshairVAO, crosshairVBO;
        glGenVertexArrays(1, &crosshairVAO); glGenBuffers(1, &crosshairVBO);
        glBindVertexArray(crosshairVAO);
        glBindBuffer(GL_ARRAY_BUFFER, crosshairVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(crosshairVertices), crosshairVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));

        Player player;
        ParticleSystem particles;   // block-break particles (needs the GL context, so created here)
        player.position = glm::vec3(8.0f, 100.0f, 8.0f);

        ctx.player = &player; ctx.lua = &lua;
        g_PlayerInstance = &player;

        glfwSetCursorPosCallback(window, [](GLFWwindow* win, double xpos, double ypos) {
            if (g_CurrentGameState == GameState::PLAYING) {
                if (g_ChatSystem && g_ChatSystem->isOpen) return;
                EngineContext* c = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));
                if (c && c->player && glfwGetInputMode(win, GLFW_CURSOR) == GLFW_CURSOR_DISABLED) {
                    c->player->handleMouseLook(xpos, ypos);
                }
            }
        });

        glfwSetMouseButtonCallback(window, [](GLFWwindow* win, int button, int action, int mods) {
            if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
                EngineContext* ctx = static_cast<EngineContext*>(glfwGetWindowUserPointer(win));
                if (!ctx) return;

                int sw, sh;
                glfwGetFramebufferSize(win, &sw, &sh);
                double mx, my;
                glfwGetCursorPos(win, &mx, &my);
                scaleCursorToFramebuffer(win, mx, my, sw, sh);

                if (g_CurrentGameState == GameState::MENU && g_MainMenu) {
                    std::string actionResult = g_MainMenu->handleMouseClick(mx, my, sw, sh);
                    if (actionResult == "play") {
                        // Singleplayer -> world selection screen (world_select.lua)
                        g_CurrentGameState = GameState::WORLD_SELECT;
                        callLuaUI(ctx->lua, "on_world_select_opened");
                    } else if (actionResult == "mods") {
                        // Mods -> installed mods screen (mods_menu.lua)
                        g_CurrentGameState = GameState::MODS;
                        callLuaUI(ctx->lua, "on_mods_opened");
                    } else if (actionResult == "texturepacks") {
                        // Textures -> texture pack picker (texture_packs.lua)
                        g_CurrentGameState = GameState::TEXTURE_PACKS;
                        callLuaUI(ctx->lua, "on_texture_packs_opened");
                    } else if (actionResult == "settings") {
                        // Settings -> key bindings and video settings (settings_menu.lua)
                        g_CurrentGameState = GameState::SETTINGS;
                        callLuaUI(ctx->lua, "on_settings_opened");
                    } else if (actionResult == "quit") {
                        glfwSetWindowShouldClose(win, true);
                    }
                } else if (settingsOpen()) {
                    callLuaUI(ctx->lua, "handle_settings_click", mx, my, sw, sh);
                } else if (g_CurrentGameState == GameState::WORLD_SELECT) {
                    callLuaUI(ctx->lua, "handle_world_select_click", mx, my, sw, sh);
                } else if (g_CurrentGameState == GameState::MODS) {
                    callLuaUI(ctx->lua, "handle_mods_click", mx, my, sw, sh);
                } else if (g_CurrentGameState == GameState::TEXTURE_PACKS) {
                    callLuaUI(ctx->lua, "handle_texture_packs_click", mx, my, sw, sh);
                } else if (g_CurrentGameState == GameState::PLAYING && g_Paused) {
                    // Forwarded to handle_pause_click in pause_menu.lua
                    try {
                        sol::protected_function clickFn = (*ctx->lua)["handle_pause_click"];
                        if (clickFn.valid()) {
                            sol::protected_function_result res = clickFn(mx, my, sw, sh);
                            if (!res.valid()) {
                                sol::error err = res;
                                std::cerr << "[Pause Lua] handle_pause_click: " << err.what() << std::endl;
                            }
                        }
                    } catch (...) {}
                } else if (g_CurrentGameState == GameState::PLAYING && g_Inventory.isCreativeOpen()) {
                    // Forwarded to handle_creative_click in inventory.lua
                    g_Inventory.handleCreativeClick(mx, my, static_cast<float>(sw), static_cast<float>(sh));

                    try {
                        sol::state* lua = ctx->lua;
                        sol::function handleInvClick = (*lua)["handle_inventory_click"];
                        if (handleInvClick.valid()) {
                            handleInvClick(mx, my, sw, sh);
                        }
                    } catch (...) {}
                }
            }
        });

        float lastFrameTime = static_cast<float>(glfwGetTime());
        float fluidTimer = 0.0f;
        bool leftMousePressedLast = false;
        bool rightMousePressedLast = false;

        while (!glfwWindowShouldClose(window)) {
            float currentFrameTime = static_cast<float>(glfwGetTime());
            float deltaTime = currentFrameTime - lastFrameTime;
            lastFrameTime = currentFrameTime;

            glfwPollEvents();

            try {
                sol::function onUpdate = lua["on_update"];
                if (onUpdate.valid()) onUpdate(deltaTime);
            } catch (...) {}

            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            if (height == 0) height = 1;

            bool isCursorDisabled = (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED);

            if (g_CurrentGameState == GameState::LOADING) {
                if (g_PendingWorldOpen) {
                    g_PendingWorldOpen = false;
                    if (!openActiveWorld(world, player)) {
                        // Wrong game for this world - back to the world list
                        world.setSaveDirectory("");
                        g_CurrentGameState = GameState::WORLD_SELECT;
                        callLuaUI(&lua, "on_world_select_opened");
                        continue;
                    }
                }

                world.setLoadBudgetMs(40.0f);
                world.setMeshBudgetMs(30.0f);
                world.update(player.position, lua);
                world.upload();

                const int sx = static_cast<int>(std::floor(player.position.x));
                const int sz = static_cast<int>(std::floor(player.position.z));
                const bool timedOut = (glfwGetTime() - g_LoadStartTime) > kLoadTimeoutSec;

                if (!g_SpawnPlaced) {
                    const int ground = world.groundHeightAt(sx, sz);
                    if (ground >= 0) {
                        player.position.y = static_cast<float>(ground);
                        g_SpawnPlaced = true;
                    } else if (timedOut) {
                        player.position.y = 100.0f;
                        g_SpawnPlaced = true;
                    }
                }

                g_LoadingProgress = world.areaLoadProgress(player.position, kSpawnReadyRadius);

                if (g_SpawnPlaced && (g_LoadingProgress >= 1.0f || timedOut)) {
                    world.setLoadBudgetMs(4.0f);
                    world.setMeshBudgetMs(4.0f);
                    g_CurrentGameState = GameState::PLAYING;
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                }
            }

            if (g_CurrentGameState == GameState::PLAYING) {
                // Autosave
                if (glfwGetTime() - g_LastAutosave >= kAutosaveSeconds) {
                    saveCurrentWorld();
                    g_LastAutosave = glfwGetTime();
                }

                if (isCursorDisabled && (!g_ChatSystem || !g_ChatSystem->isOpen)) {
                    // Hotbar slot keys (rebindable in Settings > Controls)
                    for (int slot = 0; slot < 9; ++slot) {
                        if (isActionDown(window, "hotbar" + std::to_string(slot + 1)) && slot < g_Hotbar->slotCount) {
                            g_Hotbar->activeSlot = slot;
                        }
                    }
                    world.update(player.position, lua);
                    player.update(deltaTime, world, window, lua);

                    try {
                        sol::function updateAnimFunc = lua["update_player_animation"];
                        if (updateAnimFunc.valid()) {
                            bool isMoving = isActionDown(window, "forward") || isActionDown(window, "back") ||
                                            isActionDown(window, "left")    || isActionDown(window, "right");
                            updateAnimFunc(deltaTime, isMoving, player.pitch);
                        }
                    } catch (...) {}

                    fluidTimer += deltaTime;
                    if (fluidTimer >= 0.15f) { world.updateFluids(); fluidTimer = 0.0f; }
                }
            }

            glm::vec3 rayOrigin = player.position + glm::vec3(0.0f, 1.62f, 0.0f);
            glm::vec3 rayDir;
            rayDir.x = cos(glm::radians(player.yaw)) * cos(glm::radians(player.pitch));
            rayDir.y = sin(glm::radians(player.pitch));
            rayDir.z = sin(glm::radians(player.yaw)) * cos(glm::radians(player.pitch));
            rayDir = glm::normalize(rayDir);

            RaycastResult rayResult = raycastBlock(rayOrigin, rayDir, 6.0f, world);

            bool leftPress = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            bool rightPress = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

            if (g_CurrentGameState == GameState::PLAYING && isCursorDisabled && (!g_ChatSystem || !g_ChatSystem->isOpen)) {
                uint8_t activeBlockId = g_Hotbar->getActiveBlockId();
                std::string activeItemName = (activeBlockId != 0) ? BlockRegistry::getInstance().getBlock(activeBlockId).name : "air";

                if (leftPress && !leftMousePressedLast && rayResult.hit) {
                    const glm::ivec3 hitPos(rayResult.blockPos.x, rayResult.blockPos.y, rayResult.blockPos.z);
                    uint8_t targetBlockId = world.getBlockAt(hitPos);
                    if (targetBlockId != 0) {
                        const auto& targetProp = BlockRegistry::getInstance().getBlock(targetBlockId);

                        if (targetProp.isSelectable) {
                            if (activeItemName.find("scythe") != std::string::npos) {
                                sol::protected_function scytheFunc = lua["on_use_scythe"];
                                if (scytheFunc.valid()) {
                                    scytheFunc(hitPos.x, hitPos.y, hitPos.z, targetProp.name);
                                }
                            } else {
                                // Break particles use the block's side texture (or any face it has)
                                int particleLayer = -1;
                                for (int f : { 4, 0, 2, 1, 5, 3 }) {
                                    if (targetProp.faces[f].baseLayer >= 0) { particleLayer = targetProp.faces[f].baseLayer; break; }
                                }
                                particles.spawnBreakParticles(glm::vec3(hitPos), targetBlockId, static_cast<float>(particleLayer));

                                world.setBlockAt(hitPos, 0);
                            }
                        }
                    }
                }

                if (rightPress && !rightMousePressedLast && rayResult.hit) {
                    const glm::ivec3 hitPos(rayResult.blockPos.x, rayResult.blockPos.y, rayResult.blockPos.z);
                    uint8_t targetBlockId = world.getBlockAt(hitPos);
                    if (targetBlockId != 0) {
                        const auto& targetProp = BlockRegistry::getInstance().getBlock(targetBlockId);
                        if (activeItemName.find("hoe") != std::string::npos) {
                            sol::protected_function hoeFunc = lua["on_use_hoe"];
                            if (hoeFunc.valid()) {
                                hoeFunc(hitPos.x, hitPos.y, hitPos.z, targetProp.name);
                            }
                        } else {
                            sol::protected_function clickFunc = lua["on_right_click_block"];
                            bool handled = false;
                            if (clickFunc.valid()) {
                                sol::protected_function_result res = clickFunc(hitPos.x, hitPos.y, hitPos.z, targetProp.name, activeItemName);
                                if (res.valid()) handled = res.get<bool>();
                            }

                            if (!handled) {
                                uint8_t selectedBlock = g_Hotbar->getActiveBlockId();
                                if (selectedBlock != 0) {
                                    bool replaceTarget = false;
                                    if (targetProp.name == "short_grass" || targetProp.name == "flower_red" || targetProp.pattern == "plantlike" || targetProp.name.find("farmland") != std::string::npos) {
                                        replaceTarget = true;
                                    }
                                    // Work in whole-block (integer) coordinates for both cases so the
                                    // ternary isn't mixing glm::ivec3 and glm::vec3.
                                    const glm::ivec3 hitCell(rayResult.blockPos);
                                    const int farmlandLift = (targetProp.name.find("farmland") != std::string::npos) ? 1 : 0;
                                    const glm::ivec3 cell = replaceTarget
                                        ? hitCell + glm::ivec3(0, farmlandLift, 0)
                                        : hitCell + glm::ivec3(glm::round(glm::vec3(rayResult.faceNormal)));

                                    // Don't place a solid block where the player is standing (or jumping).
                                    // Player box: 0.6 wide, 1.8 tall, feet at player.position.
                                    constexpr float kHalfWidth = 0.3f;
                                    constexpr float kHeight    = 1.8f;
                                    constexpr float kEps       = 0.001f;   // touching is fine, overlapping isn't
                                    const glm::vec3 pMin(player.position.x - kHalfWidth, player.position.y, player.position.z - kHalfWidth);
                                    const glm::vec3 pMax(player.position.x + kHalfWidth, player.position.y + kHeight, player.position.z + kHalfWidth);
                                    const bool overlapsPlayer =
                                        pMin.x < cell.x + 1 - kEps && pMax.x > cell.x + kEps &&
                                        pMin.y < cell.y + 1 - kEps && pMax.y > cell.y + kEps &&
                                        pMin.z < cell.z + 1 - kEps && pMax.z > cell.z + kEps;
                                    const bool solid = BlockRegistry::getInstance().getBlock(selectedBlock).isWalkable;

                                    if (!(solid && overlapsPlayer)) {
                                        world.setBlockAt(cell, selectedBlock);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            leftMousePressedLast = leftPress;
            rightMousePressedLast = rightPress;

            float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
            glm::mat4 proj = glm::perspective(glm::radians(g_FieldOfView), aspectRatio, 0.1f, 500.0f);
            glm::mat4 view = player.getViewMatrix();

            glm::vec3 sunDir = g_LightingSystem->getSunDirection();
            int moonPhase = g_LightingSystem->getMoonPhase();

            const glm::vec3 eyePos = player.position + glm::vec3(0.0f, 1.62f, 0.0f);
            const float waterDepth = (g_CurrentGameState == GameState::PLAYING)
                ? underwaterDepth(world, eyePos)
                : -1.0f;
            const bool submerged = waterDepth >= 0.0f;

            {
                const float target   = submerged ? 1.0f : 0.0f;
                const float seconds = submerged ? g_Underwater.fadeInSeconds : g_Underwater.fadeOutSeconds;
                const float step    = deltaTime / glm::max(seconds, 0.01f);
                if (g_UnderwaterBlend < target) g_UnderwaterBlend = glm::min(g_UnderwaterBlend + step, target);
                else                            g_UnderwaterBlend = glm::max(g_UnderwaterBlend - step, target);
            }
            const float blend      = g_UnderwaterBlend;
            const bool  skyHidden = blend > 0.5f;

            const WaterColors water = (blend > 0.0f)
                ? queryWaterColors(lua, eyePos)
                : WaterColors{ g_Underwater.shallowColor, g_Underwater.deepColor };
            const float depthT = submerged
                ? glm::clamp(waterDepth / g_Underwater.fullDepth, 0.0f, 1.0f)
                : 0.0f;
            const glm::vec3 underwaterFog = glm::mix(water.shallow, water.deep, depthT);
            const float underwaterVis = glm::mix(g_Underwater.shallowVisibility, g_Underwater.deepVisibility, depthT);

            const glm::vec3 underwaterTint = glm::mix(
                glm::vec3(0.6f, 0.85f, 1.0f),
                glm::normalize(underwaterFog),
                0.4f);
            const float tintAmount = blend * g_Underwater.tintStrength;

            const glm::vec3 skyClear(0.5f, 0.8f, 0.95f);
            const glm::vec3 clearCol = glm::mix(skyClear, underwaterFog, blend);
            glClearColor(clearCol.r, clearCol.g, clearCol.b, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            if (g_CurrentGameState == GameState::PLAYING) {
                const glm::vec3 aboveFog   = fogColorForSun(g_Fog, sunDir.y);
                const float loadedEdge     = g_WorldInstance->renderDistanceBlocks();
                const float aboveEnd       = glm::min(effectiveFogEnd(g_Fog, g_LightingSystem->weatherRainIntensity), loadedEdge - 8.0f);
                const float aboveStart     = effectiveFogStart(g_Fog, aboveEnd);

                const glm::vec3 fogCol     = glm::mix(aboveFog, underwaterFog, blend);
                const float     fogStart   = glm::mix(aboveStart, 0.0f, blend);
                const float     fogEnd     = glm::mix(aboveEnd, underwaterVis, blend);
                const float     fogDensity = g_Fog.density;
                const bool fogExp = g_Fog.exponential && blend <= 0.0f;
                const bool fogOn  = g_Fog.enabled || blend > 0.0f;

                if (!skyHidden) {
                    g_Skybox->render(view, proj, sunDir.y);

                    glDepthMask(GL_FALSE);
                    glUseProgram(sunProgram);

                    glm::vec3 sunWorldPos = player.position + (sunDir * 80.0f);
                    glm::mat4 sunModel = glm::translate(glm::mat4(1.0f), sunWorldPos);
                    sunModel[0][0] = view[0][0]; sunModel[0][1] = view[1][0]; sunModel[0][2] = view[2][0];
                    sunModel[1][0] = view[0][1]; sunModel[1][1] = view[1][1]; sunModel[1][2] = view[2][1];
                    sunModel[2][0] = view[0][2]; sunModel[2][1] = view[1][2]; sunModel[2][2] = view[2][2];
                    glUniformMatrix4fv(glGetUniformLocation(sunProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                    glUniformMatrix4fv(glGetUniformLocation(sunProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                    glUniformMatrix4fv(glGetUniformLocation(sunProgram, "model"), 1, GL_FALSE, glm::value_ptr(sunModel));

                    if (g_LightingSystem->sunTextureID != 0) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, g_LightingSystem->sunTextureID);
                        glUniform1i(glGetUniformLocation(sunProgram, "u_Texture"), 0);
                        glUniform1i(glGetUniformLocation(sunProgram, "u_HasTexture"), 1);
                    } else {
                        glUniform1i(glGetUniformLocation(sunProgram, "u_HasTexture"), 0);
                    }

                    glBindVertexArray(sunVAO);
                    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

                    glm::vec3 moonDir = -sunDir;
                    glm::vec3 moonWorldPos = player.position + (moonDir * 80.0f);
                    glm::mat4 moonModel = glm::translate(glm::mat4(1.0f), moonWorldPos);
                    moonModel[0][0] = view[0][0]; moonModel[0][1] = view[1][0]; moonModel[0][2] = view[2][0];
                    moonModel[1][0] = view[0][1]; moonModel[1][1] = view[1][1]; moonModel[1][2] = view[2][1];
                    moonModel[2][0] = view[0][2]; moonModel[2][1] = view[1][2]; moonModel[2][2] = view[2][2];
                    glUniformMatrix4fv(glGetUniformLocation(sunProgram, "model"), 1, GL_FALSE, glm::value_ptr(moonModel));

                    GLuint activeMoonTex = g_LightingSystem->moonTextureIDs[moonPhase] != 0
                        ? g_LightingSystem->moonTextureIDs[moonPhase]
                        : g_LightingSystem->moonTextureIDs[0];
                    if (activeMoonTex != 0) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, activeMoonTex);
                        glUniform1i(glGetUniformLocation(sunProgram, "u_Texture"), 0);
                        glUniform1i(glGetUniformLocation(sunProgram, "u_HasTexture"), 1);
                    } else {
                        glUniform1i(glGetUniformLocation(sunProgram, "u_HasTexture"), 0);
                    }
                    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

                    glBindVertexArray(0);
                    glDepthMask(GL_TRUE);
                }

                glUseProgram(shaderProgram);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
                glUniform1i(glGetUniformLocation(shaderProgram, "textureArray"), 0);
                glUniform3f(glGetUniformLocation(shaderProgram, "u_SunDir"), sunDir.x, sunDir.y, sunDir.z);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_Time"), static_cast<float>(glfwGetTime()));
                glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));

                glUniform1i(glGetUniformLocation(shaderProgram, "u_FogEnabled"), fogOn ? 1 : 0);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_FogExp"), fogExp ? 1 : 0);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_FogStart"), fogStart);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_FogEnd"), fogEnd);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_FogDensity"), fogDensity);
                glUniform3f(glGetUniformLocation(shaderProgram, "u_FogColor"), fogCol.r, fogCol.g, fogCol.b);
                glUniform3f(glGetUniformLocation(shaderProgram, "u_UnderwaterTint"), underwaterTint.r, underwaterTint.g, underwaterTint.b);
                glUniform1f(glGetUniformLocation(shaderProgram, "u_UnderwaterAmount"), tintAmount);

                const auto& pointLights = g_LightingSystem->getPointLights();
                glUniform1i(glGetUniformLocation(shaderProgram, "u_NumPointLights"), static_cast<int>(pointLights.size()));
                for (size_t i = 0; i < pointLights.size() && i < 16; ++i) {
                    std::string base = "u_PointLights[" + std::to_string(i) + "].";
                    glUniform3f(glGetUniformLocation(shaderProgram, (base + "position").c_str()), pointLights[i].position.x, pointLights[i].position.y, pointLights[i].position.z);
                    glUniform3f(glGetUniformLocation(shaderProgram, (base + "color").c_str()), pointLights[i].color.x, pointLights[i].color.y, pointLights[i].color.z);
                    glUniform1f(glGetUniformLocation(shaderProgram, (base + "radius").c_str()), pointLights[i].radius);
                    glUniform1f(glGetUniformLocation(shaderProgram, (base + "intensity").c_str()), pointLights[i].intensity);
                }

                glEnable(GL_DEPTH_TEST);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_Unlit"), 0);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_IsGUI"), 0);

                g_WorldInstance->render(shaderProgram, proj * view, eyePos);

                // Block-break particles (same shader as the world, so they get fog and lighting)
                if (!g_Paused) particles.update(deltaTime);
                particles.render(view, proj, shaderProgram, g_TextureArrayID);

                g_Clouds->update(deltaTime);
                if (!skyHidden) {
                    g_Clouds->render(view, proj, player.position,
                                       static_cast<float>(glfwGetTime()),
                                       sunDir.y,
                                       fogCol);
                }

                glUseProgram(modelShaderProgram);
                glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                glUniform3f(glGetUniformLocation(modelShaderProgram, "u_LightDir"), sunDir.x, sunDir.y, sunDir.z);
                glUniform3f(glGetUniformLocation(modelShaderProgram, "u_UnderwaterTint"), underwaterTint.r, underwaterTint.g, underwaterTint.b);
                glUniform1f(glGetUniformLocation(modelShaderProgram, "u_UnderwaterAmount"), tintAmount);

                glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), player.position);
                modelMatrix = glm::rotate(modelMatrix, glm::radians(-player.yaw + 90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

                try {
                    sol::object modelObj = lua["g_PlayerModel"];
                    if (modelObj.is<std::shared_ptr<AnimatedModel>>()) {
                        auto model = modelObj.as<std::shared_ptr<AnimatedModel>>();
                        if (model) {
                            model->draw(modelShaderProgram, modelMatrix);
                        }
                    }
                } catch (...) {}

                if (isCursorDisabled && (!g_ChatSystem || !g_ChatSystem->isOpen) && rayResult.hit) {
                    glUseProgram(wireframeProgram);
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(rayResult.blockPos) + glm::vec3(0.5f));
                    glUniformMatrix4fv(glGetUniformLocation(wireframeProgram, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                    glUniformMatrix4fv(glGetUniformLocation(wireframeProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                    glUniformMatrix4fv(glGetUniformLocation(wireframeProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));

                    glEnable(GL_DEPTH_TEST);
                    glDepthFunc(GL_LESS);
                    glDepthMask(GL_TRUE);
                    glDisable(GL_BLEND);
                    glDisable(GL_CULL_FACE);
                    glLineWidth(3.0f);

                    static GLuint boxVAO = 0, boxVBO = 0;
                    if (boxVAO == 0) {
                        float vertices[] = {
                            -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,
                             0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,
                             0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f,
                            -0.5f,  0.5f, -0.5f, -0.5f, -0.5f, -0.5f,

                            -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,
                             0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
                             0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,
                            -0.5f,  0.5f,  0.5f, -0.5f, -0.5f,  0.5f,

                            -0.5f, -0.5f, -0.5f, -0.5f, -0.5f,  0.5f,
                             0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,
                             0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,
                            -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f
                        };
                        glGenVertexArrays(1, &boxVAO);
                        glGenBuffers(1, &boxVBO);
                        glBindVertexArray(boxVAO);
                        glBindBuffer(GL_ARRAY_BUFFER, boxVBO);
                        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
                        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
                        glEnableVertexAttribArray(0);
                        glBindVertexArray(0);
                    }

                    glBindVertexArray(boxVAO);
                    glDrawArrays(GL_LINES, 0, 24);
                    glBindVertexArray(0);

                    // Restore the state the rest of the frame expects (culling was off before)
                    glDisable(GL_CULL_FACE);
                    glEnable(GL_BLEND);
                }

                // ---------------- HUD ----------------
                glDisable(GL_DEPTH_TEST);
                glUseProgram(uiProgram);
                if (isCursorDisabled && (!g_ChatSystem || !g_ChatSystem->isOpen)) {
                    glBindVertexArray(crosshairVAO);
                    glLineWidth(2.0f);
                    glDrawArrays(GL_LINES, 0, 4);
                    glBindVertexArray(0);
                }

                // 1. Hotbar (background + 3D icons) - drawn BEFORE the inventory so the
                //    creative inventory always sits on top of it.
                g_Hotbar->updateBuffers(width, height);
                g_Hotbar->renderSlots(uiProgram, texturedUIProgram);

                glEnable(GL_DEPTH_TEST);
                glUseProgram(shaderProgram);
                glUniform1i(glGetUniformLocation(shaderProgram, "u_Unlit"), 1);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D_ARRAY, g_TextureArrayID);
                glUniform1i(glGetUniformLocation(shaderProgram, "textureArray"), 0);

                g_Hotbar->render3DIcons(shaderProgram, g_TextureArrayID);

                glDisable(GL_DEPTH_TEST);

                // 2. Creative inventory (drawn by inventory.lua)
                g_Inventory.renderCreativeInventory(window, lua, static_cast<float>(width), static_cast<float>(height), uiProgram, texturedUIProgram);
                glDisable(GL_DEPTH_TEST);

                // 3. Other Lua UI
                try {
                    sol::function renderInvUI = lua["render_inventory"];
                    if (renderInvUI.valid()) renderInvUI(width, height);
                } catch (...) {}

                // 4. Pause menu (pause_menu.lua), or Settings if opened from it (settings_menu.lua)
                if (g_Paused) {
                    g_Inventory.renderLuaScreen(window, lua, g_SettingsInGame ? "render_settings" : "render_pause_menu",
                                                static_cast<float>(width), static_cast<float>(height),
                                                uiProgram, texturedUIProgram);
                    glDisable(GL_DEPTH_TEST);
                }

                // 5. Chat always on top
                resetUIRenderState();
                if (g_ChatSystem) g_ChatSystem->renderChatOverlay(g_FontRenderer, fontProgram, width, height);

                glEnable(GL_DEPTH_TEST);
            } else if (g_CurrentGameState == GameState::LOADING) {
                resetUIRenderState();
                drawLoadingScreen(uiProgram, fontProgram, g_LoadingProgress, width, height);
            } else if (g_CurrentGameState == GameState::WORLD_SELECT) {
                resetUIRenderState();
                g_Inventory.renderLuaScreen(window, lua, "render_world_select",
                                            static_cast<float>(width), static_cast<float>(height),
                                            uiProgram, texturedUIProgram);
            } else if (g_CurrentGameState == GameState::MODS) {
                resetUIRenderState();
                g_Inventory.renderLuaScreen(window, lua, "render_mods",
                                            static_cast<float>(width), static_cast<float>(height),
                                            uiProgram, texturedUIProgram);
            } else if (g_CurrentGameState == GameState::TEXTURE_PACKS) {
                resetUIRenderState();
                g_Inventory.renderLuaScreen(window, lua, "render_texture_packs",
                                            static_cast<float>(width), static_cast<float>(height),
                                            uiProgram, texturedUIProgram);
            } else if (g_CurrentGameState == GameState::SETTINGS) {
                resetUIRenderState();
                g_Inventory.renderLuaScreen(window, lua, "render_settings",
                                            static_cast<float>(width), static_cast<float>(height),
                                            uiProgram, texturedUIProgram);
            } else {
                // Gameplay leaves face culling / depth state behind, which hides the
                // font when coming back to the title screen. Reset it every frame.
                resetUIRenderState();

                int sw, sh;
                glfwGetFramebufferSize(window, &sw, &sh);
                double mx, my;
                glfwGetCursorPos(window, &mx, &my);
                scaleCursorToFramebuffer(window, mx, my, sw, sh);

                if (g_MainMenu) {
                    g_MainMenu->updateAndBuildBuffers(mx, my, sw, sh);
                    g_MainMenu->render(uiProgram, texturedUIProgram, fontProgram, *g_FontRenderer, sw, sh);
                }
            }

            glfwSwapBuffers(window);
        }

        // Closing the window (or Quit Game) while playing saves the world first
        if (g_CurrentGameState == GameState::PLAYING) saveCurrentWorld();

        glfwDestroyWindow(window);
        glfwTerminate();

        delete g_Hotbar;
        delete g_FontRenderer;
        delete g_ChatSystem;
        delete g_LightingSystem;
        delete g_Skybox;
        delete g_Clouds;
        delete g_MainMenu;
    } catch (const std::exception& e) {
        std::cerr << "\n[CRITICAL EXCEPTION]: " << e.what() << std::endl;
        system("pause");
        return -1;
    }
    return 0;
}
