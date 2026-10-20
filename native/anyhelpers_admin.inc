// Host admin actions (heal, teleport, time of day, weather, clear zombies and creatures), through the experimental SDK.
// Only the host runs server_scene, so every action is queued here and carried out at the start of the next server
// tick, on the server thread. The panel only reads the copied player list and posts commands.
// Not reviewed as AnyAPI services: offsets are from the 0.1.25 reference (types.json) and stay off on another build.
#include "anymaker_sdk_runtime.hpp"
#include "route_cell.h"
#include "anyhelpers_bindings.h"
#include <atomic>
#include <cmath>
#include <deque>
#include <vector>
namespace admin {
// server_scene: +0x40 tick, +0xa8 peers, +0x250 actors, +0xb10 time_of_day (+0x18 override bool, +0x38 override f64).
constexpr size_t kTick=0x40,kPeers=0xa8,kActors=0x250,kTimeOfDay=0xb10,kTodOverride=0x18,kTodValue=0x38;
constexpr size_t kWorldSeed=0x48+0x18;                       // server_scene.m_world_seed (master property_s32) value
constexpr size_t kActorElements=0x18;                        // server_scene.actor.container _m_elements: vector<ref<actor>>
constexpr size_t kActorId=0x40,kActorTransform=0xb8,kActorPeerAuthority=0x1b0;  // server_scene.actor
constexpr size_t kPeerData=0x19d0,kPeerActor=0x19d8,kPeerPosition=0x19f8;       // server_scene.peer
constexpr int kSlotIsCharacter=53,kSlotIsCreature=54,kSlotIsZombie=55;   // server_scene.actor method table (get_is_*)
constexpr int kMaxPeers=64;
struct Vec3 {double x,y,z;};
struct WeatherData {double cloud,precipitation,fog,temperature,wind_x,wind_y;};
struct TeleportEvent {uint8_t header[0x10];Vec3 position;};  // client_peer.data.event.peer_teleport, 40 bytes
static_assert(sizeof(TeleportEvent)==40,"peer_teleport");
enum Weather {kNatural=-1,kClear,kCloudy,kRain,kStorm,kFog,kSnow,kWeatherCount};
static const char* const kWeatherNames[kWeatherCount]={"Clear","Cloudy","Rain","Storm","Fog","Snow"};
enum Kind {kHeal,kTeleportTo,kBring,kBringAll,kSetTime,kNaturalTime,kFreeze,kClearActors};
// peer/target are peer ids; -1 means everyone. kClearActors: value 1 zombies, 2 creatures, 3 both.
struct Command {Kind kind;int32_t peer{-1},target{-1};double value{};uint32_t world{};};   // world: the world it was posted for
struct Player {int32_t peer,actor;Vec3 position;bool host;};

using TickFn=void(*)(uint8_t*,uint8_t*);
using TeleportFn=void(*)(uint8_t*,void*,const TeleportEvent*);
using GetByIdFn=void(*)(void**,void*,const int32_t*);
using DestroyFn=void(*)(void*,const int32_t*,uint8_t*);
using SetF64Fn=void(*)(void*,const double*);
using SetBoolFn=void(*)(void*,const bool*);
using IsFn=void(*)(uint8_t*,const void*);
using WeatherFn=void(*)(WeatherData*,void*,const double*,const double*);
static anymaker::cell_hook h_tick,h_weather;
static TeleportFn teleport_event;static GetByIdFn actor_by_id,peer_by_id;static DestroyFn destroy;
static SetF64Fn set_blood,set_hunger,set_thirst,set_oxygen,set_hot,set_cold,set_infection,set_wetness,prop_f64;
static SetBoolFn bleeding,broken_leg,prop_bool;static int32_t* day_ticks;
static std::atomic<bool> ready{},stop{};static std::atomic<uint64_t> last_tick{};static std::atomic<int> weather{kNatural};
static std::mutex mutex;static std::deque<Command> queue;static std::vector<Player> players;static std::string status;
// Server thread only.
static bool time_set{},time_frozen{},time_release{};static double time_base{};static int32_t time_tick{};static bool logged_peers{};

// Which world the host runs, bumped whenever another one starts. Commands carry it, so none reaches another world.
static std::atomic<uint32_t> world_id{};
static bool hosting(uint64_t window=3000){return ready&&GetTickCount64()-last_tick.load()<window;}
static void post(Command c){c.world=world_id.load();std::lock_guard<std::mutex> lock(mutex);if(queue.size()<32)queue.push_back(c);}
static std::vector<Player> player_list(){std::lock_guard<std::mutex> lock(mutex);return players;}
static std::string last_status(){std::lock_guard<std::mutex> lock(mutex);return status;}
static void report(const std::string& text,uint32_t level=0){log("ADMIN "+text,level);std::lock_guard<std::mutex> lock(mutex);status=text;}

template<class T> static T read(const uint8_t* p,size_t offset){T v;memcpy(&v,p+offset,sizeof(T));return v;}
static bool virtual_is(const uint8_t* actor,int slot){auto fn=(IsFn)anymaker::typeinfo_of(actor).method(slot);if(!fn)return false;uint8_t r{};fn(&r,actor);return r!=0;}
static uint8_t* peer(uint8_t* scene,int32_t id){void* p{};peer_by_id(&p,scene+kPeers,&id);return (uint8_t*)p;}

// Copies who is connected for the panel. The host is assumed to be the lowest peer id (it joins its own world first).
static void refresh_players(uint8_t* scene){std::vector<Player> list;
 for(int32_t id=0;id<kMaxPeers;++id){auto p=peer(scene,id);if(!p)continue;auto a=*(uint8_t**)(p+kPeerActor);
  list.push_back({id,a?read<int32_t>(a,kActorId):-1,read<Vec3>(p,kPeerPosition),list.empty()});}
 if(!logged_peers&&!list.empty()){logged_peers=true;std::string ids;for(auto& p:list)ids+=" "+std::to_string(p.peer)+"/"+std::to_string(p.actor);log("ADMIN_PEERS peer/actor="+ids);}
 std::lock_guard<std::mutex> lock(mutex);players=std::move(list);}

static void heal(uint8_t* character){double one=1,zero=0;bool no=false;
 // Inferred directions: hunger, thirst, heat, cold, infection and wetness build up from 0; oxygen and blood are full at 1.
 set_blood(character,&one);set_hunger(character,&zero);set_thirst(character,&zero);set_oxygen(character,&one);set_hot(character,&zero);
 set_cold(character,&zero);set_infection(character,&zero);set_wetness(character,&zero);bleeding(character,&no);broken_leg(character,&no);}

// The game's own debug teleport: it moves the peer's authority actor and makes that client reload into place.
static bool teleport(uint8_t* server,uint8_t* who,Vec3 to){auto data=*(void**)(who+kPeerData);if(!data)return false;TeleportEvent e{};e.position=to;teleport_event(server,data,&e);return true;}

static void run(uint8_t* scene,uint8_t* server,const Command& c){
 auto targets=[&](int32_t id){std::vector<uint8_t*> out;if(id>=0){if(auto p=peer(scene,id))out.push_back(p);}else for(int32_t i=0;i<kMaxPeers;++i)if(auto p=peer(scene,i))out.push_back(p);return out;};
 switch(c.kind){
 case kHeal:{int n=0;for(auto p:targets(c.peer))if(auto a=*(uint8_t**)(p+kPeerActor);a&&virtual_is(a,kSlotIsCharacter)){heal(a);++n;}report("healed "+std::to_string(n)+(n==1?" player":" players"));break;}
 case kTeleportTo:case kBring:case kBringAll:{
  // kTeleportTo moves peer to target; kBring moves target to peer; kBringAll moves everyone else to peer.
  int32_t mover=c.kind==kTeleportTo?c.peer:c.target,anchor=c.kind==kTeleportTo?c.target:c.peer;auto to=peer(scene,anchor);
  if(!to){report("teleport: that player is gone",1);break;}Vec3 base=read<Vec3>(to,kPeerPosition);int n=0;
  for(auto p:c.kind==kBringAll?targets(-1):targets(mover)){if(p==to)continue;Vec3 spot{base.x+1.5*(n%4+1),base.y+0.5,base.z};if(teleport(server,p,spot))++n;}
  report("teleported "+std::to_string(n)+(n==1?" player":" players"));break;}
 case kSetTime:time_set=true;time_release=false;time_base=c.value-std::floor(c.value);time_tick=read<int32_t>(scene,kTick);
  report("time set to "+std::to_string(int(time_base*24))+":"+(int(time_base*1440)%60<10?"0":"")+std::to_string(int(time_base*1440)%60));break;
 case kFreeze:if(time_set){double now=time_base+(time_frozen?0:double(read<int32_t>(scene,kTick)-time_tick)/std::max(1,*day_ticks));time_base=now-std::floor(now);time_tick=read<int32_t>(scene,kTick);}
  else{report("set a time first, then freeze it",1);break;}time_frozen=c.value!=0;report(time_frozen?"time frozen":"time running");break;
 case kNaturalTime:if(time_set){time_set=false;time_frozen=false;time_release=true;}report("time back to the world clock");break;
 case kClearActors:{
  // One pass over the container's own list. (Looking every id up to the id counter by get_actor_by_id scanned the
  // whole list per id, which in a long-played world stalled the server for seconds.)
  auto container=scene+kActors;anymaker::gc_vector_raw list;memcpy(&list,container+kActorElements,sizeof list);std::vector<int32_t> doomed;
  if(list.element_size==16&&list.count>0&&list.count<=1000000&&list.capacity>=list.count&&list.buffer&&anymaker::readable(list.buffer,size_t(list.capacity)*16))
   for(int32_t i=0;i<list.count;++i){auto ref=list.at<anymaker::gc_ref_raw<uint8_t>>(i);auto a=ref?ref->ptr:nullptr;
    if(!a||!anymaker::readable(a,kActorPeerAuthority+8)||*(void**)(a+kActorPeerAuthority))continue;
    if(((c.value==1||c.value==3)&&virtual_is(a,kSlotIsZombie))||((c.value==2||c.value==3)&&virtual_is(a,kSlotIsCreature)))doomed.push_back(read<int32_t>(a,kActorId));}
  for(int32_t id:doomed)destroy(container,&id,scene);
  report("cleared "+std::to_string(doomed.size())+(c.value==1?" zombies":c.value==2?" creatures":" zombies and creatures"));break;}
 }}

static void drive_time(uint8_t* scene){auto tod=scene+kTimeOfDay;
 if(time_release){bool off=false;prop_bool(tod+kTodOverride,&off);time_release=false;return;}
 if(!time_set)return;double value=time_base+(time_frozen?0:double(read<int32_t>(scene,kTick)-time_tick)/std::max(1,*day_ticks));value-=std::floor(value);
 bool on=true;prop_bool(tod+kTodOverride,&on);prop_f64(tod+kTodValue,&value);}

static void hk_tick(uint8_t* scene,uint8_t* server){
 if(scene&&server){last_tick=GetTickCount64();
  // A new world starts clean. Its scene can sit where the last world's was, so a changed world seed counts as well.
  static uint8_t* world_scene{};static int32_t world_seed{};int32_t seed=read<int32_t>(scene,kWorldSeed);
  if(scene!=world_scene||seed!=world_seed){world_scene=scene;world_seed=seed;++world_id;time_set=time_frozen=time_release=false;weather=kNatural;logged_peers=false;}
 std::deque<Command> work;{std::lock_guard<std::mutex> lock(mutex);work.swap(queue);}
  static uint64_t refreshed{};if(GetTickCount64()-refreshed>1000||!work.empty()){refreshed=GetTickCount64();refresh_players(scene);}
  for(auto& c:work)if(c.world==world_id.load())run(scene,server,c);else log("ADMIN_DROPPED reason=other_world",1);
  drive_time(scene);}   // commands are never carried into another world
 ((TickFn)h_tick.original)(scene,server);}

// Runs on the main and server threads, so the host's sky and the server's wetness and cold change together. Players
// without AnyHelpers keep their own weather.
static void hk_weather(WeatherData* out,void* manager,const double* position,const double* time){
 ((WeatherFn)h_weather.original)(out,manager,position,time);int w=weather.load();if(!out||w<0||!hosting())return;auto& d=*out;
 switch(w){
 case kClear:d.cloud=0;d.precipitation=0;d.fog=0;break;
 case kCloudy:d.cloud=0.85;d.precipitation=0;d.fog=std::min(d.fog,0.15);break;
 case kRain:d.cloud=1;d.precipitation=0.7;d.fog=std::max(d.fog,0.2);break;
 case kStorm:d.cloud=1;d.precipitation=1;d.fog=std::max(d.fog,0.3);d.wind_x*=2;d.wind_y*=2;break;
 case kFog:d.cloud=std::max(d.cloud,0.5);d.precipitation=0;d.fog=1;break;
 case kSnow:d.cloud=1;d.precipitation=0.7;d.temperature=0;break;   // unproven: snow is assumed to follow a low temperature
 }}
static void set_weather(int w){weather=w;report(w<0?"weather back to natural":std::string("weather locked to ")+kWeatherNames[w]);}

static void remove(){h_weather.remove((void*)&hk_weather);h_tick.remove((void*)&hk_tick);}
static void install(){using namespace anymaker::experimental;namespace b=anyhelpers::bind;
 for(int attempt=0;attempt<180&&!stop;++attempt){if(attempt)Sleep(1000);
  if(!matching_build()){if(!attempt)log("ADMIN_OFF reason=game_build_mismatch",1);return;}
  auto f=[](const anymaker::func_desc& d){return route::function(d);};
  void* fns[]={f(b::server_on_teleport),f(b::get_actor_by_id),f(b::get_peer_by_id),f(b::destroy_actor),f(b::set_blood_factor),f(b::set_hunger),f(b::set_thirst),
   f(b::set_oxygen),f(b::set_temperature_hot),f(b::set_temperature_cold),f(b::set_infection),f(b::set_wetness),f(b::modify_bleeding),f(b::modify_broken_leg),
   f(b::property_bool_set),f(b::property_f64_set)};
  void** tick=route::cell(b::server_scene_tick);void** weather_cell=route::cell(b::get_weather_data);auto ticks=(int32_t*)global(b::g_day_ticks);
  bool all=tick&&weather_cell&&ticks;std::string found;for(void* p:fns){all=all&&p;found+=p?"1":"0";}
  if(!all){if(attempt==20||attempt==179)log("ADMIN_LOOKUP found="+found+" tick="+std::to_string(!!tick)+" weather="+std::to_string(!!weather_cell)+" day_ticks="+std::to_string(!!ticks),attempt==179?2:1);continue;}
  teleport_event=(TeleportFn)fns[0];actor_by_id=(GetByIdFn)fns[1];peer_by_id=(GetByIdFn)fns[2];destroy=(DestroyFn)fns[3];set_blood=(SetF64Fn)fns[4];
  set_hunger=(SetF64Fn)fns[5];set_thirst=(SetF64Fn)fns[6];set_oxygen=(SetF64Fn)fns[7];set_hot=(SetF64Fn)fns[8];set_cold=(SetF64Fn)fns[9];
  set_infection=(SetF64Fn)fns[10];set_wetness=(SetF64Fn)fns[11];bleeding=(SetBoolFn)fns[12];broken_leg=(SetBoolFn)fns[13];prop_bool=(SetBoolFn)fns[14];prop_f64=(SetF64Fn)fns[15];day_ticks=ticks;
  if(!h_weather.install(weather_cell,(void*)&hk_weather)||!h_tick.install(tick,(void*)&hk_tick)){log("ADMIN_OFF reason=hook_cell_not_writable",2);remove();return;}
  ready=true;log("ADMIN_READY after_s="+std::to_string(attempt));return;}}
static void start(){std::thread(install).detach();}
static void shutdown(){stop=true;if(ready)remove();}
} // namespace admin
// The ADMIN settings tab (anyapi.menu v4 button rows). Every button posts a command for the next server tick; weather
// applies as soon as it changes. There is nothing to Apply, so the tab never reports pending edits.
namespace adminui {
static const AnyMenuV4* widgets;static uint32_t chosen{};static double hour{12};
// A solo world pauses the server while Settings is open, so "host" is decided when Settings opens: the server was
// ticking just before. Commands posted while paused run as soon as Settings closes.
static bool host_at_open{};
static void event(uint32_t type,void*){if(type==ANY_MENU_OPEN)host_at_open=admin::hosting(60000);else if(type==ANY_MENU_CANCEL)host_at_open=false;}
static uint32_t never_dirty(void*){return 0;}
static uint32_t always_default(void*){return 1;}
static void draw(const AnyMenuFrameV1*,void*){
if(!admin::hosting()&&!host_at_open){menu->label("admin.off",admin::ready?"Only the host can use these. Host or load a world, then open Settings again.":"The admin tools are off: this game build doesn't match AnyHelpers.");return;}
 auto players=admin::player_list();std::vector<std::string> names{"Everyone"};for(auto& p:players)names.push_back("Player "+std::to_string(p.peer+1)+(p.host?" (you)":""));
 if(chosen>=names.size())chosen=0;std::vector<const char*> choices;for(auto& n:names)choices.push_back(n.c_str());
 int32_t me=players.empty()?-1:players.front().peer,who=chosen?players[chosen-1].peer:-1;
 menu->heading("admin.players","PLAYERS");
 widgets->rows->choice_row("admin.player","PLAYER",choices.data(),uint32_t(choices.size()),&chosen);
 const char* heal[]{"Heal"};if(widgets->button_row("admin.heal","HEAL",heal,1))admin::post({admin::kHeal,who});
 const char* move[]{"Go to them","Bring here","Bring everyone"};
 switch(widgets->button_row("admin.teleport","TELEPORT",move,3)){
 case 1:if(who<0||who==me)admin::report("pick one other player to go to",1);else admin::post({admin::kTeleportTo,me,who});break;
 case 2:if(who<0||who==me)admin::report("pick one other player to bring",1);else admin::post({admin::kBring,me,who});break;
 case 3:admin::post({admin::kBringAll,me});break;}
 menu->heading("admin.world","WORLD");
 widgets->rows->number_row("admin.hour","HOUR",&hour,0,23,1,1);
 const char* time[]{"Set","Freeze","Run","World clock"};
 switch(widgets->button_row("admin.time","TIME",time,4)){
 case 1:admin::post({admin::kSetTime,-1,-1,hour/24.0});break;
 case 2:admin::post({admin::kFreeze,-1,-1,1});break;
 case 3:admin::post({admin::kFreeze,-1,-1,0});break;
 case 4:admin::post({admin::kNaturalTime});break;}
 const char* weathers[]{"Natural","Clear","Cloudy","Rain","Storm","Fog","Snow"};
 uint32_t weather_index=uint32_t(admin::weather.load()+1);   // what is in force: a new world starts natural
 if(widgets->rows->choice_row("admin.weather","WEATHER",weathers,7,&weather_index))admin::set_weather(int(weather_index)-1);
 const char* clear[]{"Zombies","Creatures","Both"};
 if(auto c=widgets->button_row("admin.clear","CLEAR",clear,3))admin::post({admin::kClearActors,-1,-1,double(c)});
 auto status=admin::last_status();if(status.empty())status=std::string(admin::hosting()?"Actions happen on the next game tick.":"Actions run when you close Settings (the game is paused).")+" Weather changes show only for players running AnyHelpers.";menu->label("admin.status",status.c_str());
}
static bool init(const AnyServicesV1* services){widgets=(const AnyMenuV4*)services->query("anyapi.menu",4);
 if(!widgets||widgets->version!=4||widgets->struct_size!=sizeof(*widgets)||!widgets->rows||!widgets->button_row){log("ADMIN_TAB_OFF reason=anyapi_menu_v4_missing",1);return false;}
 AnyMenuTabV2 tab;tab.id="admin";tab.title="ADMIN";tab.order=120;tab.icon=ANY_MENU_ICON_SETTINGS;tab.draw=draw;tab.event=event;tab.dirty=never_dirty;tab.defaults=always_default;
 return menu->add_tab(&tab);}
} // namespace adminui
