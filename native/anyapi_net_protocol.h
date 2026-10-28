#pragma once
// Mod sync channel, step 1: platform-free hello wire format and per-peer handshake state.
// The Steam transport lives in anyapi_net.inc; this header has no Windows or Steam types.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace anyapi_net {
constexpr uint32_t MAGIC=0x4E594E41;        // "ANYN" little-endian
constexpr uint16_t PROTOCOL=1;
constexpr int32_t CHANNEL=0x414E5900;       // Far from the game's own small channel numbers
constexpr uint32_t MAX_MESSAGE=1024,MAX_MODS=32,MAX_TEXT=95;
constexpr uint32_t HEADER=12;
enum Kind:uint8_t {HELLO=1,HELLO_ACK=2};
struct Hello {uint8_t kind{};uint32_t nonce{};std::string api_version;std::vector<std::string> mods;};

// Text fields are 1-95 printable ASCII bytes (mod ids and version strings).
inline bool text_ok(const char* s,size_t n){if(!n||n>MAX_TEXT)return false;for(size_t i=0;i<n;++i)if(uint8_t(s[i])<0x20||uint8_t(s[i])>0x7e)return false;return true;}

// Layout: magic u32, protocol u16, kind u8, reserved u8, nonce u32, api len u8 + bytes, mod count u8, (len u8 + bytes)*count.
inline bool encode(const Hello& h,std::vector<uint8_t>& out){
    out.clear();if((h.kind!=HELLO&&h.kind!=HELLO_ACK)||!text_ok(h.api_version.data(),h.api_version.size())||h.mods.size()>MAX_MODS)return false;
    auto u8=[&](uint8_t v){out.push_back(v);};auto u16=[&](uint16_t v){u8(uint8_t(v));u8(uint8_t(v>>8));};
    auto u32=[&](uint32_t v){u16(uint16_t(v));u16(uint16_t(v>>16));};auto text=[&](const std::string& s){u8(uint8_t(s.size()));out.insert(out.end(),s.begin(),s.end());};
    u32(MAGIC);u16(PROTOCOL);u8(h.kind);u8(0);u32(h.nonce);text(h.api_version);u8(uint8_t(h.mods.size()));
    for(auto& m:h.mods){if(!text_ok(m.data(),m.size()))return false;text(m);}
    return out.size()<=MAX_MESSAGE;
}
// Bounded parse; rejects wrong magic, other protocols, bad text and trailing bytes.
inline bool decode(const uint8_t* p,size_t n,Hello& h){
    h={};if(!p||n<HEADER+2||n>MAX_MESSAGE)return false;size_t at=0;
    auto u8=[&](uint8_t& v){if(at>=n)return false;v=p[at++];return true;};
    auto u32=[&](uint32_t& v){if(n-at<4)return false;v=uint32_t(p[at])|uint32_t(p[at+1])<<8|uint32_t(p[at+2])<<16|uint32_t(p[at+3])<<24;at+=4;return true;};
    auto text=[&](std::string& s){uint8_t len{};if(!u8(len)||n-at<len||!text_ok((const char*)p+at,len))return false;s.assign((const char*)p+at,len);at+=len;return true;};
    uint32_t magic{};uint8_t lo{},hi{},kind{},reserved{},count{};
    if(!u32(magic)||magic!=MAGIC||!u8(lo)||!u8(hi)||uint16_t(lo|hi<<8)!=PROTOCOL||!u8(kind)||(kind!=HELLO&&kind!=HELLO_ACK)||!u8(reserved)||!u32(h.nonce))return false;
    h.kind=kind;if(!text(h.api_version)||!u8(count)||count>MAX_MODS)return false;
    h.mods.resize(count);for(auto& m:h.mods)if(!text(m))return false;
    return at==n;
}
// Strict decimal SteamID64 ("7656...") to integer; 0 means invalid.
inline uint64_t parse_steam_id(const char* s){
    if(!s||!*s)return 0;uint64_t v=0;size_t n=0;
    for(;s[n];++n){if(s[n]<'0'||s[n]>'9'||n>=20)return 0;uint64_t d=uint64_t(s[n]-'0');if(v>(UINT64_MAX-d)/10)return 0;v=v*10+d;}
    return v;
}

// One remote player in the current world. Sending is only allowed over a Steam session the game already opened,
// so AnyAPI never starts new connections and vanilla players only ever receive the first few hellos.
struct Peer {uint64_t steam_id{};int32_t peer_id{};std::string name;bool seen{},answered{},timeout_logged{};uint32_t attempts{};uint64_t next_send{},first_send{};
    uint64_t window_start{};uint32_t window_count{};};
constexpr uint32_t MAX_ATTEMPTS=3;
constexpr uint64_t RETRY_MS=3000,TIMEOUT_MS=10000,RATE_WINDOW_MS=1000;constexpr uint32_t RATE_LIMIT=10;

class Peers {
public:
    std::vector<Peer> list;
    Peer* find(uint64_t id){for(auto& p:list)if(p.steam_id==id)return &p;return nullptr;}
    // Mark everyone unseen, then call seen() for each remote roster entry, then prune().
    void begin(){for(auto& p:list)p.seen=false;}
    Peer& seen(uint64_t id,int32_t peer_id,const char* name){auto* p=find(id);if(!p){list.push_back({});p=&list.back();p->steam_id=id;}
        p->seen=true;p->peer_id=peer_id;p->name=name?name:"";return *p;}
    void prune(){std::erase_if(list,[](const Peer& p){return !p.seen;});}
    // True when a hello is due to this peer now; records the attempt.
    static bool hello_due(Peer& p,uint64_t now){
        if(p.answered||p.attempts>=MAX_ATTEMPTS||now<p.next_send)return false;
        if(!p.attempts)p.first_send=now;++p.attempts;p.next_send=now+RETRY_MS;return true;}
    // True once, when a peer never answered within TIMEOUT_MS of the first hello.
    static bool timed_out(Peer& p,uint64_t now){
        if(p.answered||p.timeout_logged||!p.attempts||now-p.first_send<TIMEOUT_MS)return false;
        p.timeout_logged=true;return true;}
    // Per-sender flood guard: at most RATE_LIMIT messages per second are processed.
    static bool admit(Peer& p,uint64_t now){
        if(now-p.window_start>=RATE_WINDOW_MS){p.window_start=now;p.window_count=0;}
        return ++p.window_count<=RATE_LIMIT;}
};
inline std::string join_mods(const std::vector<std::string>& mods){std::string s;for(auto& m:mods){if(!s.empty())s+=", ";s+=m;}return s.empty()?"none":s;}
}
