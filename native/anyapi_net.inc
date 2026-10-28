// Mod sync channel, step 1: AnyAPI-to-AnyAPI hello over the game's own Steam sessions.
// game.exe 0.1.25 runs co-op on ISteamNetworkingMessages002 (low channel numbers only). AnyAPI uses the far
// channel anyapi_net::CHANNEL, sends only to roster peers that already have a connected Steam session, and
// drops anything whose Steam-verified sender is not in the current roster. Nothing here touches game messages.
#include "anyapi_net_protocol.h"
namespace net_runtime {
// ISteamNetworkingMessages002 vtable order (Steamworks SDK steamnetworkingmessages.h).
struct Identity {int32_t type{};int32_t size{};union {uint64_t steam_id;uint8_t raw[128];} u{};};
static_assert(sizeof(Identity)==136);
constexpr int32_t IDENTITY_STEAM_ID=16,SEND_RELIABLE=8,STATE_CONNECTED=3;
using SendFn=int32_t(*)(void*,const Identity*,const void*,uint32_t,int32_t,int32_t);
using ReceiveFn=int32_t(*)(void*,int32_t,void**,int32_t);
using SessionInfoFn=int32_t(*)(void*,const Identity*,void*,void*);
using ReleaseFn=void(*)(void*);
using UserFn=int32_t(*)();
using FindInterfaceFn=void*(*)(int32_t,const char*);
static void* messages{};
static bool resolved{},faulted{};
static uint64_t last_tick{},last_epoch{},rejected{},last_reject_log{};
static uint32_t nonce{};
static anyapi_net::Peers peers;
static Identity identity(uint64_t id){Identity i{};i.type=IDENTITY_STEAM_ID;i.size=8;i.u.steam_id=id;return i;}
static void** table(){return messages?*(void***)messages:nullptr;}
// POD-only SEH leaves around every call into steam_api64.
static void* find_interface(){
    __try{
        HMODULE steam=GetModuleHandleW(L"steam_api64.dll");if(!steam)return nullptr;
        auto user=(UserFn)GetProcAddress(steam,"SteamAPI_GetHSteamUser");
        auto find=(FindInterfaceFn)GetProcAddress(steam,"SteamInternal_FindOrCreateUserInterface");
        if(!user||!find)return nullptr;int32_t h=user();return h?find(h,"SteamNetworkingMessages002"):nullptr;
    }__except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}
static bool session_connected(uint64_t id,int32_t& state){
    __try{Identity i=identity(id);state=((SessionInfoFn)table()[5])(messages,&i,nullptr,nullptr);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool send_raw(uint64_t id,const uint8_t* data,uint32_t size,int32_t& result){
    __try{Identity i=identity(id);result=((SendFn)table()[0])(messages,&i,data,size,SEND_RELIABLE,anyapi_net::CHANNEL);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Copies one message out and releases it. Returns 0 when empty, 1 on a message, -1 on a fault.
static int receive_one(uint8_t* out,uint32_t& size,int32_t& sender_type,uint64_t& sender){
    __try{
        void* msg{};if(((ReceiveFn)table()[1])(messages,anyapi_net::CHANNEL,&msg,1)!=1||!msg)return 0;
        auto base=(uint8_t*)msg;void* data=*(void**)base;int32_t n=*(int32_t*)(base+8);
        sender_type=*(int32_t*)(base+16);sender=*(uint64_t*)(base+24);
        size=n>0&&uint32_t(n)<=anyapi_net::MAX_MESSAGE?uint32_t(n):anyapi_net::MAX_MESSAGE+1;
        if(size<=anyapi_net::MAX_MESSAGE&&data)memcpy(out,data,size);else if(!data)size=0;
        ((ReleaseFn)*(void**)(base+184))(msg);return 1;
    }__except(EXCEPTION_EXECUTE_HANDLER){return -1;}
}
static void fault(const char* where){faulted=true;char msg[160]{};std::snprintf(msg,sizeof(msg),"NET disabled after a Steam call fault in=%s",where);log_line(ANY_LOG_ERROR,"framework",msg);}
static anyapi_net::Hello make(uint8_t kind){
    anyapi_net::Hello h;h.kind=kind;h.nonce=nonce;{std::lock_guard lock(build_runtime::mutex);h.api_version=build_runtime::info.api_version[0]?build_runtime::info.api_version:"unknown";}
    for(uint32_t i=0;i<platform::plugin_count.load()&&h.mods.size()<anyapi_net::MAX_MODS;++i){
        auto& p=platform::plugins[i];const char* id=p.callbacks.id;
        if(!p.faulted&&id&&anyapi_net::text_ok(id,strnlen(id,anyapi_net::MAX_TEXT+1)))h.mods.push_back(id);}
    return h;
}
static void send(anyapi_net::Peer& p,uint8_t kind){
    std::vector<uint8_t> bytes;if(!anyapi_net::encode(make(kind),bytes))return;
    int32_t result{};if(!send_raw(p.steam_id,bytes.data(),uint32_t(bytes.size()),result)){fault("send");return;}
    char msg[512]{};std::snprintf(msg,sizeof(msg),"NET %s to %s (peer %d) attempt=%u bytes=%zu result=%d",kind==anyapi_net::HELLO?"hello":"hello_ack",
        p.name.c_str(),p.peer_id,p.attempts,bytes.size(),result);log_line(ANY_LOG_INFO,"framework",msg);
}
static void reject(const char* why,uint64_t sender){
    ++rejected;const uint64_t now=GetTickCount64();if(now-last_reject_log<5000)return;last_reject_log=now;
    char msg[200]{};std::snprintf(msg,sizeof(msg),"NET dropped message reason=%s sender=%llu total_dropped=%llu",why,(unsigned long long)sender,(unsigned long long)rejected);
    log_line(ANY_LOG_WARN,"framework",msg);
}
static void receive(uint64_t now){
    uint8_t buffer[anyapi_net::MAX_MESSAGE]{};
    for(int i=0;i<32;++i){
        uint32_t size{};int32_t type{};uint64_t sender{};int got=receive_one(buffer,size,type,sender);
        if(got<0){fault("receive");return;}if(!got)return;
        auto* p=type==IDENTITY_STEAM_ID?peers.find(sender):nullptr;
        if(!p){reject("not_in_session",sender);continue;}
        if(!anyapi_net::Peers::admit(*p,now)){reject("rate_limit",sender);continue;}
        anyapi_net::Hello h;if(size>anyapi_net::MAX_MESSAGE||!anyapi_net::decode(buffer,size,h)){reject("bad_format",sender);continue;}
        const bool first=!p->answered;p->answered=true;
        char msg[1400]{};std::snprintf(msg,sizeof(msg),"NET %s from %s (peer %d) AnyAPI %s, mods: %s",h.kind==anyapi_net::HELLO?"hello":"hello_ack",
            p->name.c_str(),p->peer_id,h.api_version.c_str(),anyapi_net::join_mods(h.mods).c_str());log_line(ANY_LOG_INFO,"framework",msg);
        if(h.kind==anyapi_net::HELLO&&first)send(*p,anyapi_net::HELLO_ACK);
    }
}
static void tick(const AnySessionPlayersV1& roster){
    if(faulted)return;const uint64_t now=GetTickCount64();if(now-last_tick<100)return;last_tick=now;
    if(!resolved){resolved=true;messages=find_interface();if(!nonce)nonce=uint32_t(now)^GetCurrentProcessId();
        log_line(messages?ANY_LOG_INFO:ANY_LOG_WARN,"framework",messages?"NET channel ready transport=SteamNetworkingMessages002":"NET channel unavailable reason=no_steam_networking_messages");}
    if(!messages)return;
    if(roster.world_epoch!=last_epoch){last_epoch=roster.world_epoch;peers.list.clear();}
    peers.begin();
    for(uint32_t i=0;i<roster.count;++i){auto& r=roster.players[i];
        if((r.valid_fields&PLAYER_LOCAL)||!(r.valid_fields&PLAYER_STEAM_ID))continue;
        uint64_t id=anyapi_net::parse_steam_id(r.steam_id);if(id)peers.seen(id,r.peer_id,(r.valid_fields&PLAYER_NAME)?r.steam_name:"?");}
    peers.prune();
    receive(now);if(faulted)return;
    for(auto& p:peers.list){
        if(anyapi_net::Peers::timed_out(p,now)){char msg[400]{};std::snprintf(msg,sizeof(msg),"NET %s (peer %d) did not answer, no AnyAPI (or an older one)",p.name.c_str(),p.peer_id);log_line(ANY_LOG_INFO,"framework",msg);}
        if(p.answered||p.attempts>=anyapi_net::MAX_ATTEMPTS)continue;
        int32_t state{};if(!session_connected(p.steam_id,state)){fault("session_info");return;}
        if(state!=STATE_CONNECTED)continue; // Never open a new Steam session; direct-IP games have none.
        if(anyapi_net::Peers::hello_due(p,now))send(p,anyapi_net::HELLO);if(faulted)return;
    }
}
}
static void anyapi_net_tick(const AnySessionPlayersV1& roster){net_runtime::tick(roster);}
