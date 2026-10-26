#pragma once
// A mod's own message line between players, over the game's own Steam sessions (as AnyAPI's sync-channel hello,
// docs/development/mod-sync-channel.md). Anymaker runs co-op on ISteamNetworkingMessages002 and reads only small
// channel numbers, so a far channel is never seen by the game. Sends only over a session the game already opened
// (never starts a connection; direct-IP games have none, so nothing is sent there). Receivers must check the
// Steam-verified sender against the players in the world. Any fault in a Steam call turns the line off.
// Windows only; every call into steam_api64 sits in its own SEH leaf with POD locals.
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace steam_messages {
// ISteamNetworkingMessages002 vtable order (Steamworks SDK steamnetworkingmessages.h).
struct Identity { int32_t type{}; int32_t size{}; union { uint64_t steam_id; uint8_t raw[128]; } u{}; };
static_assert(sizeof(Identity) == 136);
constexpr int32_t kIdentitySteamId = 16, kSendReliable = 8, kStateConnected = 3;
using SendFn = int32_t (*)(void*, const Identity*, const void*, uint32_t, int32_t, int32_t);
using ReceiveFn = int32_t (*)(void*, int32_t, void**, int32_t);
using SessionInfoFn = int32_t (*)(void*, const Identity*, void*, void*);
using ReleaseFn = void (*)(void*);
using UserFn = int32_t (*)();
using FindInterfaceFn = void* (*)(int32_t, const char*);

struct Line {
    void* messages = nullptr;
    bool resolved = false, faulted = false;
    int32_t channel = 0;

    static Identity identity(uint64_t id) { Identity i{}; i.type = kIdentitySteamId; i.size = 8; i.u.steam_id = id; return i; }
    void** table() const { return messages ? *reinterpret_cast<void***>(messages) : nullptr; }

    static void* find_interface() {
        __try {
            HMODULE steam = GetModuleHandleW(L"steam_api64.dll");
            if (!steam) return nullptr;
            auto user = reinterpret_cast<UserFn>(GetProcAddress(steam, "SteamAPI_GetHSteamUser"));
            auto find = reinterpret_cast<FindInterfaceFn>(GetProcAddress(steam, "SteamInternal_FindOrCreateUserInterface"));
            if (!user || !find) return nullptr;
            int32_t h = user();
            return h ? find(h, "SteamNetworkingMessages002") : nullptr;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    // True once the interface is there (looked up once).
    bool ready(int32_t on_channel) {
        if (faulted) return false;
        if (!resolved) { resolved = true; channel = on_channel; messages = find_interface(); }
        return messages != nullptr;
    }
    // The game already has a connected Steam session with this player.
    bool connected(uint64_t id) {
        if (!ready(channel)) return false;
        int32_t state = 0;
        bool ok = false;
        __try { Identity i = identity(id); state = reinterpret_cast<SessionInfoFn>(table()[5])(messages, &i, nullptr, nullptr); ok = true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { faulted = true; }
        return ok && state == kStateConnected;
    }
    // Reliable send; Steam's EResult (1: OK), or 0 when the line is off.
    int32_t send(uint64_t id, const void* data, uint32_t size) {
        if (!ready(channel) || !connected(id)) return 0;
        int32_t result = 0;
        __try { Identity i = identity(id); result = reinterpret_cast<SendFn>(table()[0])(messages, &i, data, size, kSendReliable, channel); }
        __except (EXCEPTION_EXECUTE_HANDLER) { faulted = true; result = 0; }
        return result;
    }
    // One message copied out and released: 1 (size and Steam-verified sender set; size > capacity: too big, not
    // copied), 0 none waiting, -1 a fault (the line is off).
    int receive(uint8_t* out, uint32_t capacity, uint32_t& size, uint64_t& sender) {
        if (!ready(channel)) return 0;
        __try {
            void* msg = nullptr;
            if (reinterpret_cast<ReceiveFn>(table()[1])(messages, channel, &msg, 1) != 1 || !msg) return 0;
            auto base = static_cast<uint8_t*>(msg);
            void* data = *reinterpret_cast<void**>(base);
            int32_t n = *reinterpret_cast<int32_t*>(base + 8);
            int32_t sender_type = *reinterpret_cast<int32_t*>(base + 16);
            sender = sender_type == kIdentitySteamId ? *reinterpret_cast<uint64_t*>(base + 24) : 0;
            size = n > 0 ? uint32_t(n) : 0;
            if (size <= capacity && data) std::memcpy(out, data, size);
            else if (!data) size = 0;
            reinterpret_cast<ReleaseFn>(*reinterpret_cast<void**>(base + 184))(msg);
            return 1;
        } __except (EXCEPTION_EXECUTE_HANDLER) { faulted = true; return -1; }
    }
};
}  // namespace steam_messages
