#include "dinput8_proxy.cpp"
#include <cassert>
#include <array>
// Execute the exact current actor-lookup body with controlled iterator imports.
// This verifies producer contracts with fixtures, never real-game acceptance.
static std::array<std::array<unsigned char,0x910>,65> actors;
static std::array<std::array<uintptr_t,2>,65> refs;
static std::array<std::array<unsigned char,0x60>,65> peers;
static std::array<std::array<unsigned char,0xc10>,65> scene_peers;
static size_t actor_count=65,peer_count=65,forwards=0;
static void iter_begin(uintptr_t* out,void*){*out=1;}
static void actor_end(uintptr_t* out,void*){*out=actor_count+1;}
static void peer_end(uintptr_t* out,void*){*out=peer_count+1;}
static void actor_value(uintptr_t* out,void*,const uintptr_t* i){*out=(uintptr_t)refs.at(*i-1).data();}
static void peer_value(uintptr_t* out,void*,const uintptr_t* i){*out=(uintptr_t)peers.at(*i-1).data();}
static void scene_peer_value(uintptr_t* out,void*,const uintptr_t* i){*out=(uintptr_t)scene_peers.at(*i-1).data();}
static void iter_next(uintptr_t* out,void*,const uintptr_t* i){*out=*i+1;}
static void stalled_next(uintptr_t* out,void*,const uintptr_t* i){*out=*i;}
static void original(void*,void*,void*,const double*,const double*,const void*){++forwards;}
static void copy_transform(void* out,const void* source){memcpy(out,source,96);}
static void live_transform(double* out,void*){memset(out,0,96);out[0]=out[4]=out[8]=1;out[9]=81234;out[10]=17;out[11]=52789;}
template<class T> static void put(unsigned char* p,size_t offset,const T& value){memcpy(p+offset,&value,sizeof(value));}
static void column_rotation(double* out,const double* angle){double c=std::cos(*angle),v=std::sin(*angle);double m[9]={c,0,-v,0,1,0,v,0,c};memcpy(out,m,sizeof(m));}
static void column_transform(double* out,const double* m,const double* v){for(int i=0;i<3;++i)out[i]=m[i]*v[0]+m[3+i]*v[1]+m[6+i]*v[2];}
static void row_rotation(double* out,const double* angle){column_rotation(out,angle);std::swap(out[2],out[6]);}
static void row_transform(double* out,const double* m,const double* v){for(int i=0;i<3;++i)out[i]=m[i*3]*v[0]+m[i*3+1]*v[1]+m[i*3+2]*v[2];}
static int view_calls{};
static void copy_view(double* out,void* source){++view_calls;memcpy(out,source,96);}
int main(){
    g_p34_math={column_rotation,column_transform};
    uintptr_t imports[]={(uintptr_t)&iter_begin,(uintptr_t)&actor_end,(uintptr_t)&actor_value,(uintptr_t)&iter_next};
    auto body=(unsigned char*)VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(body);
    memcpy(body,P34_ACTOR_BY_ID,sizeof(P34_ACTOR_BY_ID));
    for(size_t i=0;i<4;++i)put(body,sizeof(P34_ACTOR_BY_ID)+i*8,(uintptr_t)&imports[i]);
    uintptr_t copy_fn=(uintptr_t)&copy_transform;
    memcpy(body+512,P34_BASE_TRANSFORM,sizeof(P34_BASE_TRANSFORM));put(body,512+sizeof(P34_BASE_TRANSFORM),(uintptr_t)&copy_fn);
    memcpy(body+1024,P34_ORIENTATION,sizeof(P34_ORIENTATION));
    memcpy(body+1536,P34_CHARACTER_TRANSFORM,sizeof(P34_CHARACTER_TRANSFORM));
    put(body,1536+sizeof(P34_CHARACTER_TRANSFORM),uintptr_t(0));
    put(body,1536+sizeof(P34_CHARACTER_TRANSFORM)+8,(uintptr_t)&copy_fn);
    memcpy(body+2304,P34_PEER_BY_ID,sizeof(P34_PEER_BY_ID));
    uintptr_t peer_imports[]={(uintptr_t)&iter_begin,(uintptr_t)&peer_end,(uintptr_t)&scene_peer_value,(uintptr_t)&iter_next};
    for(size_t i=0;i<3;++i)put(body,2304+sizeof(P34_PEER_BY_ID)+i*8,(uintptr_t)&peer_imports[i]);
    put(body,2304+sizeof(P34_PEER_BY_ID)+3*8,uintptr_t(8));
    put(body,2304+sizeof(P34_PEER_BY_ID)+4*8,(uintptr_t)&peer_imports[3]);
    memcpy(body+3072,P34_PROP_S32_GET,sizeof(P34_PROP_S32_GET));
    DWORD old{};assert(VirtualProtect(body,4096,PAGE_EXECUTE_READ,&old));assert(FlushInstructionCache(GetCurrentProcess(),body,4096));
    uintptr_t fns[]={(uintptr_t)body,(uintptr_t)body+512,(uintptr_t)body+1024,(uintptr_t)body+2304};
    uintptr_t cells[]={(uintptr_t)&fns[0],(uintptr_t)&fns[1],(uintptr_t)&fns[2],(uintptr_t)&fns[3]},table=(uintptr_t)cells;
    std::array<unsigned char,0x88> klass{};put(klass.data(),0x80,table);
    std::array<unsigned char,0x200> scene{};put(scene.data(),0x158,(uintptr_t)klass.data());
    put(scene.data(),0x40,(uintptr_t)klass.data());uintptr_t property_fn=(uintptr_t)body+3072,property_cell=(uintptr_t)&property_fn;
    uintptr_t property_cells[]={0,property_cell};std::array<unsigned char,0x88> property_class{};put(property_class.data(),0x80,(uintptr_t)property_cells);
    std::array<unsigned char,0xa0> client{};
    const char name[]="Fixture \xE2\x98\x83",id[]="76561198000000000";
    for(size_t i=0;i<65;++i){
        put(actors[i].data(),0,(uintptr_t)klass.data());put(actors[i].data(),0x8e8,std::acos(-1.)/2);
        put(actors[i].data(),8,int32_t(i));double pos[3]={81000.+i,15.,52000.+i};
        memcpy(actors[i].data()+0x1d0,pos,sizeof(pos));double basis[9]={1,0,0,0,1,0,0,0,1};memcpy(actors[i].data()+0x188,basis,sizeof(basis));
        refs[i][1]=(uintptr_t)actors[i].data();put(peers[i].data(),8,int32_t(i));put(peers[i].data(),0x48,int32_t(i));
        put(scene_peers[i].data(),8,(uintptr_t)property_class.data());put(scene_peers[i].data(),16,int32_t(i));
        put(scene_peers[i].data(),0xb90,(uintptr_t)property_class.data());put(scene_peers[i].data(),0xb98,int32_t(i));
        put(peers[i].data(),0x10,GeoString16{(uintptr_t)name,sizeof(name)-1,sizeof(name)-1});
        put(peers[i].data(),0x20,GeoString16{(uintptr_t)id,sizeof(id)-1,sizeof(id)-1});
    }
    put(scene.data(),0x188,(uintptr_t)actors[0].data());
    g_p34_begin=&iter_begin;g_p34_end=&peer_end;g_p34_value=&peer_value;g_p34_next=&iter_next;g_p34_actor_slot=0;g_p34_transform_slot=8;g_p34_orientation_slot=16;
    AnySessionPlayersV1 sample{};assert(p34_native_sample(client.data(),scene.data(),&sample));
    assert(sample.count==64&&sample.dropped==1);assert(sample.players[0].valid_fields==29);
    assert(!strcmp(sample.players[0].steam_name,name));assert(!(sample.players[0].valid_fields&PLAYER_FACING)); // Unknown view getter cannot invent direction.
    // The same world heading must survive native transfer from look yaw to body rotation.
    const double pi=std::acos(-1.);double heading{};
    for(double body_yaw:{-pi,-pi*.5,-.3,0.,.4,pi*.5,pi})for(double look:{-.9,0.,.8}){
     double m[12]={std::cos(body_yaw),0,-std::sin(body_yaw),0,1,0,std::sin(body_yaw),0,std::cos(body_yaw)};
     assert(player_heading::world_yaw(m,look,heading,g_p34_math));assert(std::abs(std::remainder(heading-body_yaw-look,2*pi))<1e-10);
     double moved=.6;m[0]=std::cos(body_yaw+moved);m[2]=-std::sin(body_yaw+moved);m[6]=std::sin(body_yaw+moved);m[8]=std::cos(body_yaw+moved);
     double transferred{};assert(player_heading::world_yaw(m,look-moved,transferred,g_p34_math));assert(std::abs(std::remainder(transferred-heading,2*pi))<1e-10);
    }
    double rotated[9]={0,0,-1,0,1,0,1,0,0};memcpy(actors[0].data()+0x188,rotated,sizeof(rotated));put(actors[0].data(),0x8e8,0.);
    sample={};assert(p34_native_sample(client.data(),scene.data(),&sample));assert(!(sample.players[0].valid_fields&PLAYER_FACING));
    put(actors[0].data(),0x8e8,-pi*.5);sample={};assert(p34_native_sample(client.data(),scene.data(),&sample));assert(!(sample.players[0].valid_fields&PLAYER_FACING));
    double identity[9]={1,0,0,0,1,0,0,0,1};memcpy(actors[0].data()+0x188,identity,sizeof(identity));put(actors[0].data(),0x8e8,pi*.5);
    double bad[12]{};assert(!player_heading::world_yaw(bad,0.,heading,g_p34_math));bad[0]=std::numeric_limits<double>::quiet_NaN();assert(!player_heading::world_yaw(bad,0.,heading,g_p34_math));
    // The producer delegates layout to native math: both conventions give the same world direction.
    player_heading::NativeMath row_math{row_rotation,row_transform};double row_body[9];double body_angle=pi*.5;row_rotation(row_body,&body_angle);
    assert(player_heading::world_yaw(row_body,0.,heading,row_math)&&std::abs(heading-pi*.5)<1e-10);
    assert(!player_heading::world_yaw(row_body,0.,heading,{}));
    // The native view can face opposite the body. Heading must follow the view,
    // including its zero/+Z ray and row/column math supplied by the engine.
    for(double angle:{-pi,-pi*.5,0.,pi*.25,pi*.5,pi}){
     double view[12]{};column_rotation(view,&angle);double got{};
     assert(player_heading::view_yaw(view,copy_view,got,g_p34_math));assert(std::abs(std::remainder(got-angle,2*pi))<1e-10);
     row_rotation(view,&angle);assert(player_heading::view_yaw(view,copy_view,got,row_math));assert(std::abs(std::remainder(got-angle,2*pi))<1e-10);
    }
    assert(view_calls==12);double invalid_view[12]{};assert(!player_heading::view_yaw(invalid_view,copy_view,heading,g_p34_math));
    assert(!player_heading::view_yaw(invalid_view,nullptr,heading,g_p34_math));
    g_p34_peer_lookup_slot=24;g_p34_prop_s32_slot=8;g_p34_peer_contracts_ready=true;
    for(auto& peer:peers)put(peer.data(),0x48,int32_t(-1));sample={};assert(p34_native_sample(client.data(),scene.data(),&sample));
    assert(sample.count==64&&sample.players[10].actor_id==10&&(sample.players[10].valid_fields&PLAYER_POSITION));
    uintptr_t physics_fn=(uintptr_t)&live_transform,physics_cell=(uintptr_t)&physics_fn,physics_table=(uintptr_t)&physics_cell;
    std::array<unsigned char,0x88> physics_class{};put(physics_class.data(),0x80,physics_table);
    put(actors[0].data(),0x818,(uintptr_t)physics_class.data());actors[0][0x1e8]=1;fns[1]=(uintptr_t)body+1536;
    peer_count=1;actor_count=1;sample={};assert(p34_native_sample(client.data(),scene.data(),&sample));
    assert(sample.players[0].position[0]==81234&&sample.players[0].position[2]==52789);fns[1]=(uintptr_t)body+512;
    peer_count=1;actor_count=0;sample={};assert(p34_native_sample(client.data(),scene.data(),&sample));
    assert(sample.count==1&&!(sample.players[0].valid_fields&PLAYER_POSITION)&&(sample.players[0].valid_fields&PLAYER_NAME));
    peer_count=0;put(client.data(),0x70,GeoString16{(uintptr_t)name,sizeof(name)-1,sizeof(name)-1});
    put(client.data(),0x80,GeoString16{(uintptr_t)id,sizeof(id)-1,sizeof(id)-1});sample={};
    assert(p34_native_sample(client.data(),scene.data(),&sample));assert(sample.count==1&&sample.players[0].valid_fields==29);
    peer_count=1;
    const char invalid[]={char(0xc3),char(0x28),0};put(peers[0].data(),0x10,GeoString16{(uintptr_t)invalid,2,2});
    sample={};assert(p34_native_sample(client.data(),scene.data(),&sample));assert(!(sample.players[0].valid_fields&PLAYER_NAME));
    put(peers[0].data(),0x10,GeoString16{(uintptr_t)name,300,300});sample={};
    assert(p34_native_sample(client.data(),scene.data(),&sample));assert(!(sample.players[0].valid_fields&PLAYER_NAME));
    g_p34_next=&stalled_next;sample={};assert(!p34_native_sample(client.data(),scene.data(),&sample));
    AnyPlayersChannelV1 channel{};g_p34_channel=&channel;g_p34_original=&original;
    std::array<unsigned char,0x840> frontend{};g_p34_frontend=(uintptr_t)frontend.data();
    put(frontend.data(),0x828,int32_t(2));p34_publish(sample);assert(channel.reserved==3);
    put(frontend.data(),0x828,int32_t(1));p34_publish(sample);assert(channel.reserved==2);channel.sequence=0;
    p34_player_hook(nullptr,client.data(),scene.data(),nullptr,nullptr,nullptr);
    assert(forwards==1&&g_p34_faulted&&channel.sequence==2&&channel.snapshot.count==0);
    p34_player_hook(nullptr,client.data(),scene.data(),nullptr,nullptr,nullptr);assert(forwards==2&&channel.sequence==2);
    put(scene.data(),0x158,uintptr_t(1));sample={};assert(!p34_native_sample(client.data(),scene.data(),&sample));
    VirtualFree(body,0,MEM_RELEASE);puts("PASS: exact native lookup, Unicode names, 64-player cap, missing actors, bad names, stalled iterator, original forwarding, invalid owners");
}
