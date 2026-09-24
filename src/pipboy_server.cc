#include "pipboy_server.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "critter.h"
#include "game.h"
#include "inventory.h"
#include "item.h"
#include "automap.h"
#include "freetype_manager.h"
#include "map.h"
#include "map_defs.h"
#include "object.h"
#include "obj_types.h"
#include "perk.h"
#include "perk_defs.h"
#include "pipboy.h"
#include "proto.h"
#include "scripts.h"
#include "settings.h"
#include "skill.h"
#include "skill_defs.h"
#include "stat.h"
#include "stat_defs.h"
#include "worldmap.h"

// ---------------------------------------------------------------------------
// Platform socket glue
// ---------------------------------------------------------------------------
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET SocketHandle;
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int SocketHandle;
#endif

namespace fallout {

namespace {

#ifdef _WIN32
const SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
const SocketHandle kInvalidSocket = -1;
#endif

// Protocol constants (see PIPBOY.md).
const char* kMagic = "PIPB";
const int kProtocolVersion = 1;
const size_t kMaxPayload = 1024 * 1024;

enum MessageType {
    kMsgKeepAlive = 0x00,
    kMsgHello = 0x01,
    kMsgWelcome = 0x02,
    kMsgDataUpdate = 0x03,
    kMsgDataDelta = 0x04,
    kMsgCommand = 0x05,
    kMsgCommandReply = 0x06,
    kMsgBye = 0x07,
};

// Fallout 2 prototype id of bottle caps.
const int kPidCaps = 41;

// ---------------------------------------------------------------------------
// Small socket helpers
// ---------------------------------------------------------------------------
void closeSocket(SocketHandle handle)
{
    if (handle == kInvalidSocket) {
        return;
    }
#ifdef _WIN32
    closesocket(handle);
#else
    ::close(handle);
#endif
}

bool waitReadable(SocketHandle handle, int timeoutMs)
{
    if (handle == kInvalidSocket) {
        return false;
    }
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(handle, &readSet);

    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

#ifdef _WIN32
    int rc = select(0, &readSet, nullptr, nullptr, &tv);
#else
    int rc = select(static_cast<int>(handle) + 1, &readSet, nullptr, nullptr, &tv);
#endif
    return rc > 0;
}

bool sendAll(SocketHandle handle, const void* data, size_t size)
{
    const char* ptr = static_cast<const char*>(data);
    size_t sent = 0;
    while (sent < size) {
#ifdef _WIN32
        int n = send(handle, ptr + sent, static_cast<int>(size - sent), 0);
#else
        ssize_t n = send(handle, ptr + sent, size - sent, 0);
#endif
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

// Sends one framed message: uint32 LE length + uint8 type + payload.
bool sendFrame(SocketHandle handle, uint8_t type, const std::string& payload)
{
    if (payload.size() > kMaxPayload) {
        return false;
    }
    uint32_t length = static_cast<uint32_t>(payload.size());
    uint8_t header[5];
    header[0] = static_cast<uint8_t>(length & 0xFF);
    header[1] = static_cast<uint8_t>((length >> 8) & 0xFF);
    header[2] = static_cast<uint8_t>((length >> 16) & 0xFF);
    header[3] = static_cast<uint8_t>((length >> 24) & 0xFF);
    header[4] = type;

    if (!sendAll(handle, header, sizeof(header))) {
        return false;
    }
    if (length == 0) {
        return true;
    }
    return sendAll(handle, payload.data(), payload.size());
}

// ---------------------------------------------------------------------------
// Minimal JSON writer
// ---------------------------------------------------------------------------
std::string jsonEscape(const std::string& in)
{
    std::string out;
    out.reserve(in.size() + 8);
    for (unsigned char c : in) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Game-text transcoding: the game strings are encoded in the active font's
// encoding (GBK for the Chinese localization), while the JSON protocol is
// UTF-8. Convert with iconv so the client receives valid UTF-8 text.
// ---------------------------------------------------------------------------
#include <iconv.h>

// GNU libiconv declares the input buffer of `iconv` as `const char**`, while
// the copy bundled with macOS and glibc declares it as `char**`. Probe which
// declaration is available so the call below can be spelled portably. (Same
// trick as freetype_manager.cc.)
template <typename InputPointer>
static auto iconvInputProbe(InputPointer) -> decltype(iconv(std::declval<iconv_t>(), std::declval<InputPointer>(), std::declval<size_t*>(), std::declval<char**>(), std::declval<size_t*>()), std::true_type{});

static std::false_type iconvInputProbe(...);

using IconvInputPointer = std::conditional_t<decltype(iconvInputProbe(static_cast<const char**>(nullptr)))::value, const char*, char*>;

std::string gameTextToUtf8(const std::string& in)
{
    const char* encoding = ftGetActiveEncoding();
    if (encoding == nullptr || in.empty()) {
        return in;
    }
    if (strcmp(encoding, "UTF-8") == 0) {
        return in;
    }

    // One cached converter per encoding name.
    static std::map<std::string, iconv_t> converters;
    static std::mutex convertersMutex;
    iconv_t cd;
    {
        std::lock_guard<std::mutex> lock(convertersMutex);
        auto it = converters.find(encoding);
        if (it == converters.end()) {
            cd = iconv_open("UTF-8", encoding);
            if (cd == (iconv_t)-1) {
                // Encoding not available on this platform: pass through.
                converters[encoding] = (iconv_t)-1;
                return in;
            }
            converters[encoding] = cd;
            it = converters.find(encoding);
        }
        cd = it->second;
        if (cd == (iconv_t)-1) {
            return in;
        }
    }

    // Reset conversion state (iconv keeps state between calls).
    iconv(cd, nullptr, nullptr, nullptr, nullptr);

    size_t inBytesLeft = in.size();
    IconvInputPointer input = in.data();
    std::string out;
    out.resize(in.size() * 4 + 16);
    char* outPtr = &out[0];
    size_t outBytesLeft = out.size();

    while (inBytesLeft > 0) {
        size_t rc = iconv(cd, &input, &inBytesLeft, &outPtr, &outBytesLeft);
        if (rc != (size_t)-1) {
            break;
        }
        if (errno == E2BIG) {
            size_t used = out.size() - outBytesLeft;
            out.resize(out.size() * 2);
            outPtr = &out[used];
            outBytesLeft = out.size() - used;
            continue;
        }
        // EILSEQ / EINVAL: replace the offending byte with '?' and skip it so
        // one broken character cannot sink the whole string.
        if (inBytesLeft == 0) {
            break;
        }
        input += 1;
        inBytesLeft -= 1;
        *outPtr = '?';
        outPtr += 1;
        outBytesLeft -= 1;
    }

    out.resize(out.size() - outBytesLeft);
    return out;
}

std::string jsonString(const std::string& value)
{
    return "\"" + jsonEscape(gameTextToUtf8(value)) + "\"";
}

std::string jsonInt(int value)
{
    return std::to_string(value);
}

std::string jsonBool(bool value)
{
    return value ? "true" : "false";
}

// ---------------------------------------------------------------------------
// Base64 (for the automap grid blob)
// ---------------------------------------------------------------------------
const char* kBase64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const unsigned char* data, size_t size)
{
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (size_t i = 0; i < size; i += 3) {
        const unsigned int b0 = data[i];
        const unsigned int b1 = i + 1 < size ? data[i + 1] : 0;
        const unsigned int b2 = i + 2 < size ? data[i + 2] : 0;
        const unsigned int triple = (b0 << 16) | (b1 << 8) | b2;
        out += kBase64Chars[(triple >> 18) & 0x3F];
        out += kBase64Chars[(triple >> 12) & 0x3F];
        out += i + 1 < size ? kBase64Chars[(triple >> 6) & 0x3F] : '=';
        out += i + 2 < size ? kBase64Chars[triple & 0x3F] : '=';
    }
    return out;
}

// ---------------------------------------------------------------------------
// Real map data (automap grid + world map), cached because collecting it
// involves file I/O and decode work which must not run every sample tick.
// ---------------------------------------------------------------------------

// Automap grid of the current map/elevation, base64-encoded (10000 raw bytes,
// one byte per hex tile: 0 empty, 1 wall, 2 scenery). Empty string when the
// current map has no automap entry.
std::string gAutomapBase64;
int gAutomapMap = -1;
int gAutomapElevation = -1;

void refreshAutomapCache(int map, int elevation)
{
    if (map == gAutomapMap && elevation == gAutomapElevation) {
        return;
    }
    gAutomapMap = map;
    gAutomapElevation = elevation;

    static unsigned char grid[HEX_GRID_SIZE];
    if (automapGetGrid(map, elevation, grid) == 0) {
        gAutomapBase64 = base64Encode(grid, HEX_GRID_SIZE);
    } else {
        gAutomapBase64.clear();
    }
}

// Cities known or visited by the player, as a JSON array. Cheap to rebuild
// every sample (a few dozen entries).
std::string buildCitiesJson()
{
    std::string json = "[";
    const int count = wmGetCityCount();
    bool first = true;
    for (int index = 0; index < count; index++) {
        const char* name = nullptr;
        int x = 0;
        int y = 0;
        int state = 0;
        if (!wmGetCityWorldInfo(index, &name, &x, &y, &state)) {
            continue;
        }
        // Only expose cities the player has actually discovered.
        if (state != CITY_STATE_KNOWN && state != CITY_STATE_VISITED) {
            continue;
        }
        if (!first) {
            json += ",";
        }
        first = false;
        json += "{";
        json += "\"name\":" + jsonString(name != nullptr ? name : "");
        json += ",\"x\":" + jsonInt(x);
        json += ",\"y\":" + jsonInt(y);
        json += ",\"state\":" + jsonInt(state);
        json += "}";
    }
    json += "]";
    return json;
}

// ---------------------------------------------------------------------------
// Snapshot
//
// The snapshot is a flat map of "dotted.path" -> JSON-encoded value. Flat is
// deliberate: diffing two snapshots is then a trivial map comparison, and the
// delta frame is just the subset of keys whose value changed.
// ---------------------------------------------------------------------------
using Snapshot = std::map<std::string, std::string>;

std::mutex gSnapshotMutex;
Snapshot gSnapshot;

// Set when the listener starts; used for the Server.UptimeSec field. Declared
// before `collectSnapshot()` because that function reads it.
uint64_t gServerStartedAtMs = 0;

uint64_t nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Reads game state. MUST be called from the main thread only.
void collectSnapshot(Snapshot& out)
{
    out.clear();

    if (gDude == nullptr) {
        // No game in progress: publish an empty-ish snapshot so the client can
        // render its "waiting for game" state instead of showing stale values.
        out["PlayerInfo.PlayerName"] = jsonString("");
        out["Server.SampleIntervalMs"] = jsonInt(settings.pipboy.sample_interval_ms);
        return;
    }

    const char* name = critterGetName(gDude);
    out["PlayerInfo.PlayerName"] = jsonString(name != nullptr ? name : "");

    const int maxHp = critterGetStat(gDude, STAT_MAXIMUM_HIT_POINTS);
    out["PlayerInfo.CurrHP"] = jsonInt(critterGetHitPoints(gDude));
    out["PlayerInfo.MaxHP"] = jsonInt(maxHp);

    const int maxAp = critterGetStat(gDude, STAT_MAXIMUM_ACTION_POINTS);
    // Current AP lives in the combat data block which is only meaningful while
    // in combat; outside of combat it equals the maximum.
    out["PlayerInfo.MaxAP"] = jsonInt(maxAp);
    out["PlayerInfo.CurrAP"] = jsonInt(maxAp);

    out["PlayerInfo.MaxWeight"] = jsonInt(critterGetStat(gDude, STAT_CARRY_WEIGHT));

    const int caps = objectGetCarriedQuantityByPid(gDude, kPidCaps);
    out["PlayerInfo.Caps"] = jsonInt(caps);
    out["Inventory.caps"] = jsonInt(caps);

    out["PlayerInfo.XPLevel"] = jsonInt(pcGetStat(PC_STAT_LEVEL));
    const int xp = pcGetStat(PC_STAT_EXPERIENCE);
    const int xpNext = pcGetExperienceForNextLevel();
    out["PlayerInfo.XP"] = jsonInt(xp);
    out["PlayerInfo.XPNext"] = jsonInt(xpNext);
    if (xpNext > 0) {
        const double pct = static_cast<double>(xp) * 100.0 / static_cast<double>(xpNext);
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f", pct > 100.0 ? 100.0 : pct);
        out["PlayerInfo.XPProgressPct"] = buf;
    } else {
        out["PlayerInfo.XPProgressPct"] = "0.0";
    }

    out["PlayerInfo.Karma"] = jsonInt(pcGetStat(PC_STAT_KARMA));
    out["PlayerInfo.Reputation"] = jsonInt(pcGetStat(PC_STAT_REPUTATION));
    out["PlayerInfo.IsSneaking"] = jsonBool(dudeIsSneaking());

    // SPECIAL
    out["Special.Strength"] = jsonInt(critterGetStat(gDude, STAT_STRENGTH));
    out["Special.Perception"] = jsonInt(critterGetStat(gDude, STAT_PERCEPTION));
    out["Special.Endurance"] = jsonInt(critterGetStat(gDude, STAT_ENDURANCE));
    out["Special.Charisma"] = jsonInt(critterGetStat(gDude, STAT_CHARISMA));
    out["Special.Intelligence"] = jsonInt(critterGetStat(gDude, STAT_INTELLIGENCE));
    out["Special.Agility"] = jsonInt(critterGetStat(gDude, STAT_AGILITY));
    out["Special.Luck"] = jsonInt(critterGetStat(gDude, STAT_LUCK));

    // Derived stats
    out["Derived.ArmorClass"] = jsonInt(critterGetStat(gDude, STAT_ARMOR_CLASS));
    out["Derived.ActionPoints"] = jsonInt(maxAp);
    out["Derived.CarryWeight"] = jsonInt(critterGetStat(gDude, STAT_CARRY_WEIGHT));
    out["Derived.MeleeDamage"] = jsonInt(critterGetStat(gDude, STAT_MELEE_DAMAGE));
    out["Derived.Sequence"] = jsonInt(critterGetStat(gDude, STAT_SEQUENCE));
    out["Derived.HealingRate"] = jsonInt(critterGetStat(gDude, STAT_HEALING_RATE));
    out["Derived.CriticalChance"] = jsonInt(critterGetStat(gDude, STAT_CRITICAL_CHANCE));
    out["Derived.DamageThreshold"] = jsonInt(critterGetStat(gDude, STAT_DAMAGE_THRESHOLD));
    out["Derived.DamageResistance"] = jsonInt(critterGetStat(gDude, STAT_DAMAGE_RESISTANCE));
    out["Derived.RadiationResistance"] = jsonInt(critterGetStat(gDude, STAT_RADIATION_RESISTANCE));
    out["Derived.PoisonResistance"] = jsonInt(critterGetStat(gDude, STAT_POISON_RESISTANCE));

    // Conditions
    out["Conditions.Poison"] = jsonInt(critterGetPoison(gDude));
    out["Conditions.Radiation"] = jsonInt(critterGetRadiation(gDude));
    out["Conditions.IsDead"] = jsonBool(critterIsDead(gDude));
    out["Conditions.IsCrippled"] = jsonBool(critterIsCrippled(gDude));
    out["Conditions.IsEncumbered"] = jsonBool(critterIsEncumbered(gDude));

    // Per-body-part damage flags, so the second screen can lay condition
    // indicators over the Vault Boy silhouette (F4 app style).
    {
        const unsigned int results = gDude->data.critter.combat.results;
        out["Conditions.Eye"] = jsonBool((results & DAM_BLIND) != 0);
        out["Conditions.ArmLeft"] = jsonBool((results & DAM_CRIP_ARM_LEFT) != 0);
        out["Conditions.ArmRight"] = jsonBool((results & DAM_CRIP_ARM_RIGHT) != 0);
        out["Conditions.LegLeft"] = jsonBool((results & DAM_CRIP_LEG_LEFT) != 0);
        out["Conditions.LegRight"] = jsonBool((results & DAM_CRIP_LEG_RIGHT) != 0);
    }

    // Skills. Names come from the game data, so a localized build yields
    // localized skill names on the second screen for free.
    for (int skill = 0; skill < SKILL_COUNT; skill++) {
        const char* skillName = skillGetName(skill);
        if (skillName == nullptr) {
            continue;
        }
        out["Skills." + std::string(skillName)] = jsonInt(skillGetValue(gDude, skill));
    }

    // Perks the critter actually has (rank > 0), with localized names and
    // descriptions. Sent as one JSON array so the client can render it
    // directly; diffing keeps it delta-free until something changes.
    {
        std::string perksJson = "[";
        bool first = true;
        for (int perk = 0; perk < PERK_COUNT; perk++) {
            const int rank = perkGetRank(gDude, perk);
            if (rank <= 0) {
                continue;
            }
            const char* name = perkGetName(perk);
            if (name == nullptr || name[0] == '\0') {
                continue;
            }
            const char* description = perkGetDescription(perk);
            if (!first) {
                perksJson += ",";
            }
            first = false;
            perksJson += "{";
            perksJson += "\"name\":" + jsonString(name);
            perksJson += ",\"rank\":" + jsonInt(rank);
            perksJson += ",\"description\":" + jsonString(description != nullptr ? description : "");
            perksJson += "}";
        }
        perksJson += "]";
        out["Perks.list"] = perksJson;
    }

    // Quests currently visible in the pip-boy (gvar >= displayThreshold),
    // with localized location (map.msg) and title (quests.msg). The quest
    // table is loaded on demand: historically it was only populated while the
    // in-game pip-boy window was open.
    {
        std::string questsJson = "[";
        bool first = true;
        const int count = pipboyQuestsEnsureLoaded();
        for (int index = 0; index < count; index++) {
            int location = 0;
            int description = 0;
            int gvar = 0;
            int displayThreshold = 0;
            int completedThreshold = 0;
            if (!pipboyQuestsGetEntry(index, &location, &description, &gvar, &displayThreshold, &completedThreshold)) {
                continue;
            }
            if (gvar < 0 || gvar >= gGameGlobalVarsLength) {
                continue;
            }
            const int value = gGameGlobalVars[gvar];
            if (value < displayThreshold) {
                continue;
            }
            const char* loc = pipboyQuestGetLocationText(location);
            const char* title = pipboyQuestGetDescriptionText(description);
            if (!first) {
                questsJson += ",";
            }
            first = false;
            questsJson += "{";
            questsJson += "\"id\":" + jsonInt(gvar);
            questsJson += ",\"name\":" + jsonString(loc != nullptr ? loc : "");
            questsJson += ",\"description\":" + jsonString(title != nullptr ? title : "");
            questsJson += ",\"done\":" + jsonBool(value >= completedThreshold);
            questsJson += "}";
        }
        questsJson += "]";
        out["Quests.list"] = questsJson;
    }

    // Inventory
    int carriedWeight = 0;
    std::vector<std::string> items;
    const int count = gDude->data.inventory.length;
    for (int index = 0; index < count; index++) {
        Object* item = gDude->data.inventory.items[index].item;
        if (item == nullptr) {
            continue;
        }
        const int quantity = gDude->data.inventory.items[index].quantity;

        int weight = 0;
        Proto* proto = nullptr;
        if (protoGetProto(item->pid, &proto) == 0 && proto != nullptr) {
            // Only item prototypes carry a weight field.
            if ((item->pid >> 24) == 0) {
                weight = proto->item.weight;
            }
        }
        carriedWeight += weight * quantity;

        const char* itemName = objectGetName(item);
        std::string entry = "{";
        entry += "\"name\":" + jsonString(itemName != nullptr ? itemName : "");
        entry += ",\"count\":" + jsonInt(quantity);
        entry += ",\"weight\":" + jsonInt(weight);
        entry += ",\"pid\":" + jsonInt(item->pid);
        entry += "}";
        items.push_back(entry);
    }
    out["PlayerInfo.CurrWeight"] = jsonInt(carriedWeight);

    std::string itemsJson = "[";
    for (size_t i = 0; i < items.size(); i++) {
        if (i != 0) {
            itemsJson += ",";
        }
        itemsJson += items[i];
    }
    itemsJson += "]";
    out["Inventory.items"] = itemsJson;

    Object* weapon = critterGetItem1(gDude);
    const char* weaponName = weapon != nullptr ? objectGetName(weapon) : nullptr;
    out["Inventory.weapon"] = jsonString(weaponName != nullptr ? weaponName : "");

    Object* armor = critterGetArmor(gDude);
    const char* armorName = armor != nullptr ? objectGetName(armor) : nullptr;
    out["Inventory.armor"] = jsonString(armorName != nullptr ? armorName : "");

    // Equipped armor protection breakdown. FO2 armors cover the whole body;
    // the [7] arrays are per DAMAGE TYPE (normal/laser/fire/plasma/
    // electrical/emp/explosion), not per body part.
    {
        static const char* kDmgKeys[7] = {
            "Normal", "Laser", "Fire", "Plasma", "Electrical", "Emp", "Explosion",
        };
        for (int i = 0; i < 7; i++) {
            out[std::string("Armor.Dt.") + kDmgKeys[i]] =
                jsonInt(armor != nullptr ? armorGetDamageThreshold(armor, i) : 0);
            out[std::string("Armor.Dr.") + kDmgKeys[i]] =
                jsonInt(armor != nullptr ? armorGetDamageResistance(armor, i) : 0);
        }
    }

    // Map / date
    const int mapIndex = mapGetCurrentMap();
    if (mapIndex >= 0) {
        const char* mapName = mapGetName(mapIndex, gElevation);
        out["Map.Name"] = jsonString(mapName != nullptr ? mapName : "");
        const char* cityName = mapGetCityName(mapIndex);
        out["Map.City"] = jsonString(cityName != nullptr ? cityName : "");
    } else {
        out["Map.Name"] = jsonString("World Map");
        out["Map.City"] = jsonString("");
    }
    out["Map.Elevation"] = jsonInt(gElevation);
    out["Map.IsWorldmap"] = jsonBool(mapIndex < 0);

    // Real map data for the Pip-Boy MAP screen.
    if (mapIndex >= 0) {
        // Player position in the AUTOMAP bitmap space. The automap DB stores
        // hexes with a mirrored x axis (see _decode_map_data: v1 = 200 - x,
        // packed 4-per-byte from the high bits; the game's map view draws
        // objects at -2*x for the same reason). Convert the hex x with the
        // exact inverse of the storage packing so the marker shares the
        // bitmap's coordinate space, or it moves OPPOSITE to the player.
        const int tile = gDude->tile;
        const int v1 = HEX_GRID_WIDTH - (tile % HEX_GRID_WIDTH); // 1..200
        int col = 4 * (v1 / 4) + 3 - (v1 % 4);
        if (col < 0) col = 0;
        if (col > HEX_GRID_WIDTH - 1) col = HEX_GRID_WIDTH - 1;
        out["Map.PlayerX"] = jsonInt(col);
        out["Map.PlayerY"] = jsonInt(tile / HEX_GRID_WIDTH);

        // Automap grid from AUTOMAP.DB, cached per (map, elevation).
        refreshAutomapCache(mapIndex, gElevation);
        out["Map.Automap"] = jsonString(gAutomapBase64);
    } else {
        out["Map.PlayerX"] = jsonInt(-1);
        out["Map.PlayerY"] = jsonInt(-1);
        out["Map.Automap"] = jsonString("");
    }

    // World map: party position, world size and discovered cities.
    int worldX = 0;
    int worldY = 0;
    wmGetPartyWorldPos(&worldX, &worldY);
    out["Map.WorldX"] = jsonInt(worldX);
    out["Map.WorldY"] = jsonInt(worldY);
    int worldW = 0;
    int worldH = 0;
    if (wmGetWorldSize(&worldW, &worldH) == 0) {
        out["Map.WorldW"] = jsonInt(worldW);
        out["Map.WorldH"] = jsonInt(worldH);
    } else {
        out["Map.WorldW"] = jsonInt(0);
        out["Map.WorldH"] = jsonInt(0);
    }
    out["Map.Cities"] = buildCitiesJson();

    int month = 0;
    int day = 0;
    int year = 0;
    gameTimeGetDate(&month, &day, &year);
    out["PlayerInfo.DateMonth"] = jsonInt(month);
    out["PlayerInfo.Day"] = jsonInt(day);
    out["PlayerInfo.Year"] = jsonInt(year);
    out["PlayerInfo.TimeHour"] = jsonInt(static_cast<int>((gameTimeGetTime() / GAME_TIME_TICKS_PER_HOUR) % 24));
    out["PlayerInfo.TimeMinute"] = jsonInt(static_cast<int>((gameTimeGetTime() / (GAME_TIME_TICKS_PER_HOUR / 60)) % 60));

    out["Server.SampleIntervalMs"] = jsonInt(settings.pipboy.sample_interval_ms);
    out["Server.UptimeSec"] = jsonInt(static_cast<int>((nowMs() - gServerStartedAtMs) / 1000));
}

// ---------------------------------------------------------------------------
// Server state
// ---------------------------------------------------------------------------
std::atomic<bool> gRunning(false);
std::atomic<bool> gEnabled(false);
std::thread gThread;
SocketHandle gListenSocket = kInvalidSocket;
uint64_t gLastSampleMs = 0;
bool gSnapshotDirty = false;
std::mutex gDirtyMutex;

bool bindAndListen(int port, const std::string& bindAddress)
{
    SocketHandle handle = static_cast<SocketHandle>(socket(AF_INET, SOCK_STREAM, 0));
    if (handle == kInvalidSocket) {
        return false;
    }

    int reuse = 1;
    setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (bindAddress.empty() || bindAddress == "127.0.0.1" || bindAddress == "localhost") {
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (bind(handle, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeSocket(handle);
        return false;
    }
    if (listen(handle, 1) != 0) {
        closeSocket(handle);
        return false;
    }

    gListenSocket = handle;
    return true;
}

// Reads exactly one frame into `type` / `payload`. Returns false on timeout,
// disconnect or protocol error.
bool readFrame(SocketHandle handle, std::string& buffer, uint8_t& type, std::string& payload)
{
    while (true) {
        if (buffer.size() >= 5) {
            const unsigned char* h = reinterpret_cast<const unsigned char*>(buffer.data());
            uint32_t length = static_cast<uint32_t>(h[0])
                | (static_cast<uint32_t>(h[1]) << 8)
                | (static_cast<uint32_t>(h[2]) << 16)
                | (static_cast<uint32_t>(h[3]) << 24);
            if (length > kMaxPayload) {
                return false;
            }
            if (buffer.size() >= static_cast<size_t>(length) + 5) {
                type = h[4];
                payload = buffer.substr(5, static_cast<size_t>(length));
                buffer.erase(0, static_cast<size_t>(length) + 5);
                return true;
            }
        }
        if (!waitReadable(handle, 100)) {
            return false;
        }
        char chunk[1024];
#ifdef _WIN32
        int n = recv(handle, chunk, sizeof(chunk), 0);
#else
        ssize_t n = recv(handle, chunk, sizeof(chunk), 0);
#endif
        if (n <= 0) {
            return false;
        }
        buffer.append(chunk, static_cast<size_t>(n));
    }
}

std::string buildWelcome(bool busy)
{
    std::string json = "{";
    json += "\"magic\":" + jsonString(kMagic);
    json += ",\"protocol\":" + jsonInt(kProtocolVersion);
    if (busy) {
        json += ",\"error\":" + jsonString("busy");
        json += ",\"reason\":" + jsonString("another client is connected");
    } else {
        json += ",\"game\":" + jsonString("fallout2-ce");
        json += ",\"caps\":" + jsonString("delta,update");
        json += ",\"sample_interval_ms\":" + jsonInt(settings.pipboy.sample_interval_ms);
    }
    json += "}";
    return json;
}

// Serializes a snapshot (or a subset of it) as a JSON object.
std::string encodeObject(const Snapshot& values)
{
    std::string json = "{";
    bool first = true;
    for (const auto& kv : values) {
        if (!first) {
            json += ",";
        }
        first = false;
        json += jsonString(kv.first) + ":" + kv.second;
    }
    json += "}";
    return json;
}

// Serves a single client until it disconnects or the server is stopped.
void serveClient(SocketHandle client)
{
    std::string buffer;
    std::string readBuffer;

    // Handshake.
    uint8_t type = 0;
    std::string payload;
    const uint64_t deadline = nowMs() + 5000;
    bool handshaken = false;
    while (nowMs() < deadline && gRunning.load()) {
        if (!waitReadable(client, 100)) {
            continue;
        }
        char chunk[512];
#ifdef _WIN32
        int n = recv(client, chunk, sizeof(chunk), 0);
#else
        ssize_t n = recv(client, chunk, sizeof(chunk), 0);
#endif
        if (n <= 0) {
            break;
        }
        readBuffer.append(chunk, static_cast<size_t>(n));
        if (readFrame(client, readBuffer, type, payload)) {
            handshaken = true;
            break;
        }
    }

    if (!handshaken || type != kMsgHello || payload.find("\"PIPB\"") == std::string::npos) {
        sendFrame(client, kMsgWelcome, "{\"magic\":\"PIPB\",\"error\":\"bad_hello\"}");
        closeSocket(client);
        return;
    }

    sendFrame(client, kMsgWelcome, buildWelcome(false));

    // Push a full snapshot right after the handshake.
    Snapshot lastSent;
    {
        std::lock_guard<std::mutex> lock(gSnapshotMutex);
        lastSent = gSnapshot;
    }
    sendFrame(client, kMsgDataUpdate, encodeObject(lastSent));

    uint64_t lastSendMs = nowMs();
    uint64_t lastRecvMs = nowMs();

    while (gRunning.load()) {
        // Drain whatever the client sent (keepalives / commands / bye).
        while (waitReadable(client, 0)) {
            char chunk[512];
#ifdef _WIN32
            int n = recv(client, chunk, sizeof(chunk), 0);
#else
            ssize_t n = recv(client, chunk, sizeof(chunk), 0);
#endif
            if (n <= 0) {
                closeSocket(client);
                return;
            }
            lastRecvMs = nowMs();
            readBuffer.append(chunk, static_cast<size_t>(n));

            uint8_t incomingType = 0;
            std::string incomingPayload;
            while (readFrame(client, readBuffer, incomingType, incomingPayload)) {
                if (incomingType == kMsgBye) {
                    closeSocket(client);
                    return;
                }
                if (incomingType == kMsgCommand) {
                    // v1 has no write commands; acknowledge so the client does
                    // not wait forever.
                    sendFrame(client, kMsgCommandReply, "{\"ok\":false,\"error\":\"not_supported\"}");
                }
                // kMsgKeepAlive requires no action beyond updating lastRecvMs.
            }
        }

        // Push changes.
        Snapshot current;
        {
            std::lock_guard<std::mutex> lock(gSnapshotMutex);
            current = gSnapshot;
        }
        Snapshot delta;
        for (const auto& kv : current) {
            auto it = lastSent.find(kv.first);
            if (it == lastSent.end() || it->second != kv.second) {
                delta[kv.first] = kv.second;
            }
        }
        if (!delta.empty()) {
            if (!sendFrame(client, kMsgDataDelta, encodeObject(delta))) {
                closeSocket(client);
                return;
            }
            lastSent = current;
        }

        // Heartbeat every 2s, independent of how chatty the data stream is.
        // A moving player streams DATA_DELTA continuously (worldmap travel,
        // combat), so gating the heartbeat on "nothing else was sent" meant a
        // busy game never prompted the client to answer — the client then
        // looked silent and was reaped below, over and over.
        if (nowMs() - lastSendMs > 2000) {
            if (!sendFrame(client, kMsgKeepAlive, "")) {
                closeSocket(client);
                return;
            }
            lastSendMs = nowMs();
        }

        // No bytes from the client for 30s -> assume it is gone. Healthy
        // clients heartbeat on their own schedule; the wide margin keeps a
        // stalled game frame from dropping a perfectly good link.
        if (nowMs() - lastRecvMs > 30000) {
            closeSocket(client);
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    closeSocket(client);
}

void serverThreadMain()
{
    while (gRunning.load()) {
        if (!waitReadable(gListenSocket, 200)) {
            continue;
        }
        SocketHandle client = static_cast<SocketHandle>(accept(gListenSocket, nullptr, nullptr));
        if (client == kInvalidSocket) {
            continue;
        }
        // Single client at a time (Fallout 4 semantics). The loop naturally
        // serializes sessions: a second connection simply waits in the backlog
        // until the first one goes away.
        serveClient(client);
    }
}

void startServer()
{
    if (gRunning.load()) {
        return;
    }
    if (!bindAndListen(settings.pipboy.port, settings.pipboy.bind_address)) {
        return;
    }
    gRunning.store(true);
    gServerStartedAtMs = nowMs();
    gThread = std::thread(serverThreadMain);
}

void stopServer()
{
    if (!gRunning.load()) {
        return;
    }
    gRunning.store(false);
    // Unblock accept()/select() by closing the listening socket, then join.
    closeSocket(gListenSocket);
    gListenSocket = kInvalidSocket;
    if (gThread.joinable()) {
        gThread.join();
    }
}

} // namespace

bool pipboyServerInit()
{
    gEnabled.store(settings.pipboy.enabled);
    if (gEnabled.load()) {
        startServer();
    }
    return true;
}

void pipboyServerExit()
{
    stopServer();
    gEnabled.store(false);
}

void pipboyServerTick()
{
    if (!gEnabled.load()) {
        return;
    }

    const int interval = settings.pipboy.sample_interval_ms > 0 ? settings.pipboy.sample_interval_ms : 250;
    const uint64_t now = nowMs();
    if (now - gLastSampleMs < static_cast<uint64_t>(interval)) {
        return;
    }
    gLastSampleMs = now;

    Snapshot fresh;
    collectSnapshot(fresh);

    std::lock_guard<std::mutex> lock(gSnapshotMutex);
    gSnapshot = fresh;
}

bool pipboyServerIsEnabled()
{
    return gEnabled.load();
}

bool pipboyServerSetEnabled(bool enabled)
{
    if (enabled == gEnabled.load()) {
        return gEnabled.load();
    }

    gEnabled.store(enabled);
    settings.pipboy.enabled = enabled;

    if (enabled) {
        startServer();
    } else {
        stopServer();
    }
    return gEnabled.load();
}

bool pipboyServerToggle()
{
    return pipboyServerSetEnabled(!gEnabled.load());
}

const char* pipboyServerStatusMessage()
{
    static std::string message;
    if (!gEnabled.load()) {
        message = "Pip-Boy 链接：关闭";
        return message.c_str();
    }
    message = "Pip-Boy 链接：开启 " + settings.pipboy.bind_address + ":" + std::to_string(settings.pipboy.port);
    return message.c_str();
}

} // namespace fallout
