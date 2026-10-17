#include "../anyapi_net_protocol.h"
#include <cassert>
using namespace anyapi_net;
int main() {
    Hello h; h.kind=HELLO; h.nonce=0x12345678; h.api_version="0.39.0"; h.mods={"anyengineparts","mccode"};
    std::vector<uint8_t> bytes; assert(encode(h,bytes));
    Hello back; assert(decode(bytes.data(),bytes.size(),back));
    assert(back.kind==HELLO && back.nonce==h.nonce && back.api_version=="0.39.0" && back.mods==h.mods);
    // Truncated, trailing, wrong magic and wrong protocol are all rejected.
    for(size_t n=0;n<bytes.size();++n) assert(!decode(bytes.data(),n,back));
    auto extra=bytes; extra.push_back(0); assert(!decode(extra.data(),extra.size(),back));
    auto bad=bytes; bad[0]^=1; assert(!decode(bad.data(),bad.size(),back));
    bad=bytes; bad[4]=2; assert(!decode(bad.data(),bad.size(),back));
    bad=bytes; bad[6]=9; assert(!decode(bad.data(),bad.size(),back));
    bad=bytes; bad[13]=0x01; assert(!decode(bad.data(),bad.size(),back)); // control byte in text
    assert(!decode(nullptr,0,back));
    // Encoder refuses bad text, too many mods, unknown kinds.
    Hello e=h; e.mods.push_back(""); assert(!encode(e,bytes));
    e=h; e.mods.assign(MAX_MODS+1,"x"); assert(!encode(e,bytes));
    e=h; e.kind=7; assert(!encode(e,bytes));
    e=h; e.mods.assign(MAX_MODS,std::string(MAX_TEXT,'a')); assert(!encode(e,bytes)); // over MAX_MESSAGE
    // Steam IDs.
    assert(parse_steam_id("76561198000000001")==76561198000000001ull);
    assert(!parse_steam_id("") && !parse_steam_id("12a") && !parse_steam_id("-1") && !parse_steam_id("99999999999999999999"));
    // Handshake: three hellos 3 s apart, timeout once after 10 s, none after an answer.
    Peers peers; peers.begin(); auto& p=peers.seen(5,2,"Friend"); peers.prune();
    assert(Peers::hello_due(p,1000) && !Peers::hello_due(p,2000) && Peers::hello_due(p,4000) && Peers::hello_due(p,7000) && !Peers::hello_due(p,20000));
    assert(!Peers::timed_out(p,10999) && Peers::timed_out(p,11000) && !Peers::timed_out(p,12000));
    peers.begin(); auto& q=peers.seen(6,3,"Modded"); peers.prune(); assert(peers.list.size()==1 && peers.find(6));
    assert(Peers::hello_due(q,0)); q.answered=true; assert(!Peers::hello_due(q,5000) && !Peers::timed_out(q,50000));
    // Rate limit: 10 per second per sender.
    for(uint32_t i=0;i<RATE_LIMIT;++i) assert(Peers::admit(q,100000));
    assert(!Peers::admit(q,100500) && Peers::admit(q,101000));
    assert(join_mods({})=="none" && join_mods({"a","b"})=="a, b");
}
