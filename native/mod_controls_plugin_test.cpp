#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_menu_v2.h"
#include "mod_controls_v1.h"
#include "anyhelpers_settings_v1.h"
#include "map_markers.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <gdiplus.h>
#include <vector>
static bool capture;static uint32_t flags=2;static int capture_starts;
static void input_capture(uint32_t active){capture=active!=0;}
static void log(uint32_t,const char* id,const char* message){if(strstr(message,"CAPTURE_BEGIN"))++capture_starts;std::cout<<id<<": "<<message<<'\n';}
static bool players(AnySessionPlayersV1* out,uint32_t* ui){*out={};out->count=1;out->world_epoch=1;out->sampled_tick=GetTickCount64();out->players[0].valid_fields=PLAYER_LOCAL|PLAYER_POSITION|PLAYER_FACING|PLAYER_NAME;strcpy_s(out->players[0].steam_name,"Settings fixture");out->players[0].yaw_radians=.6;out->players[0].position[0]=83955;out->players[0].position[2]=55845;*ui=flags;return true;}
// Render artifacts come from the actual installed-game assets and DLL callback.
static void png(const AnyCanvasV1& c,const wchar_t* name){auto dir=std::filesystem::current_path()/L"fixture-results";std::filesystem::create_directories(dir);auto path=dir/name;Gdiplus::Bitmap image(c.width,c.height,c.pitch,PixelFormat32bppPARGB,(BYTE*)c.pixels);UINT count{},size{};Gdiplus::GetImageEncodersSize(&count,&size);std::vector<uint8_t> memory(size);auto codecs=(Gdiplus::ImageCodecInfo*)memory.data();Gdiplus::GetImageEncoders(count,size,codecs);for(UINT i=0;i<count;++i)if(!wcscmp(codecs[i].MimeType,L"image/png")){assert(image.Save(path.c_str(),&codecs[i].Clsid)==Gdiplus::Ok);return;}assert(false);}
int wmain(int argc,wchar_t** argv){assert(argc==5);HMODULE fixture=LoadLibraryW(argv[1]);assert(fixture);auto services=AnyAPI_Services();assert(services);auto ui_set=(void(*)(int32_t))GetProcAddress(fixture,"FixtureUiState");assert(ui_set);ui_set(1);HMODULE controls=LoadLibraryW(argv[2]),map=LoadLibraryW(argv[3]);assert(controls&&map);
 auto directory=std::filesystem::temp_directory_path()/(L"AnyAPI.PluginTest."+std::to_wstring(GetCurrentProcessId()));auto path=directory.wstring();AnyModHostV1 host;host.game_directory=argv[4];host.plugin_directory=path.c_str();host.log=log;host.capture_input=input_capture;host.copy_players=players;
 AnyModCallbacksV1 c,m;assert(((AnyModInitV1)GetProcAddress(controls,"AnyAPI_ModInit"))(&host,&c));assert(!c.render&&!c.input);assert(((AnyModInitV1)GetProcAddress(map,"AnyAPI_ModInit"))(&host,&m));auto ready=(void(*)())GetProcAddress(map,"AnyAPI_ModReady");assert(ready);ready();
 auto setting_service=(const AnyHelpersSettingsV1*)services->query("anyhelpers.settings",1);assert(setting_service);
 auto settings_event=(void(*)(uint32_t))GetProcAddress(fixture,"FixtureSettingsEvent");auto stage=(void(*)(uint64_t,double,const char*))GetProcAddress(fixture,"FixtureStage");auto settings_dirty=(uint32_t(*)())GetProcAddress(fixture,"FixtureSettingsDirty");
 assert(services->query("anyhelpers.controls",1)==services->query("modcontrols.bindings",1));AnySettingValueV1 setting;assert(setting_service->get(8,&setting)&&!strcmp(setting.text,"Waypoint"));assert(!setting_service->get(16,&setting));
 auto bindings=(const ModControlsV1*)services->query("modcontrols.bindings",1);assert(bindings&&bindings->key(1)=='M');char name[128]{};assert(bindings->key_name(1,name,sizeof(name))&&!strcmp(name,"M"));
 auto event=(void(*)(uint32_t))GetProcAddress(fixture,"FixtureEvent");auto draw=(void(*)(uint32_t))GetProcAddress(fixture,"FixtureDraw");auto input=(uint32_t(*)(const AnyInputV1*))GetProcAddress(fixture,"FixtureInput");auto dirty=(uint32_t(*)())GetProcAddress(fixture,"FixtureDirty");
 event(ANY_MENU_OPEN);draw(1);assert(capture);int starts=capture_starts;for(int i=0;i<300;++i)draw(1);assert(capture&&capture_starts==starts);AnyInputV1 e;e.kind=ANY_KEY_DOWN;e.key='K';assert(input(&e));draw(0);assert(!capture&&dirty()&&bindings->key(1)=='M');assert(input(&e)); // duplicate raw/legacy down remains quarantined
 e.kind=ANY_KEY_UP;assert(input(&e));event(ANY_MENU_CANCEL);assert(!dirty()&&bindings->key(1)=='M');
 event(ANY_MENU_OPEN);draw(1);e.kind=ANY_KEY_DOWN;e.key='K';assert(input(&e));e.kind=ANY_KEY_UP;assert(input(&e));event(ANY_MENU_APPLY);assert(!dirty()&&bindings->key(1)=='K');event(ANY_MENU_CANCEL);
 AnyFrameV1 frame;frame.width=1200;frame.height=1040;frame.focused=1;frame.tick=GetTickCount64();AnyCanvasV1 canvas;m.render(&frame,&canvas,nullptr);assert(canvas.pixels&&canvas.width==320);
 settings_event(ANY_MENU_OPEN);stage(2,120,nullptr);assert(settings_dirty()&&setting_service->get(2,&setting)&&setting.number==100);
 settings_event(ANY_MENU_DEACTIVATE);assert(settings_dirty());settings_event(ANY_MENU_APPLY);assert(!settings_dirty());settings_event(ANY_MENU_CANCEL);frame.tick+=40;canvas={};m.render(&frame,&canvas,nullptr);assert(canvas.pixels&&canvas.width==384&&canvas.x==796);
 settings_event(ANY_MENU_OPEN);stage(1,0,nullptr);settings_event(ANY_MENU_APPLY);settings_event(ANY_MENU_CANCEL);frame.tick+=40;canvas={};m.render(&frame,&canvas,nullptr);assert(!canvas.pixels);
 settings_event(ANY_MENU_OPEN);stage(3,2000,nullptr);stage(4,1,nullptr);stage(5,0,nullptr);stage(6,1.4,nullptr);stage(7,0,nullptr);stage(8,0,"Destination");settings_event(ANY_MENU_CANCEL);assert(setting_service->get(8,&setting)&&!strcmp(setting.text,"Waypoint"));
 settings_event(ANY_MENU_OPEN);stage(8,0,"Destination");settings_event(ANY_MENU_APPLY);assert(setting_service->get(8,&setting)&&!strcmp(setting.text,"Destination"));settings_event(ANY_MENU_RESET);assert(settings_dirty());settings_event(ANY_MENU_APPLY);settings_event(ANY_MENU_CANCEL);frame.tick+=40;canvas={};m.render(&frame,&canvas,nullptr);assert(canvas.width==320);

 // Every registered option must reach actual DLL rendering after Apply.
 auto pixels_hash=[](const AnyCanvasV1& canvas){uint64_t h=1469598103934665603ULL;for(size_t i=0;i<size_t(canvas.pitch)*canvas.height;++i)h=(h^canvas.pixels[i])*1099511628211ULL;return h;};
 auto render_hash=[&](){frame.tick+=40;canvas={};m.render(&frame,&canvas,nullptr);assert(canvas.pixels);return pixels_hash(canvas);};
 auto apply_setting=[&](uint64_t token,double number,const char* text=nullptr){settings_event(ANY_MENU_OPEN);stage(token,number,text);settings_event(ANY_MENU_APPLY);frame.tick+=40;AnyCanvasV1 hidden;m.render(&frame,&hidden,nullptr);assert(!hidden.pixels);settings_event(ANY_MENU_CANCEL);};
 apply_setting(9,1);render_hash();assert(canvas.x==20&&canvas.y==25);
 apply_setting(9,3);render_hash();assert(canvas.x==20&&canvas.y==int(frame.height-canvas.height)-25);
 apply_setting(9,4);apply_setting(10,50);apply_setting(11,50);render_hash();assert(canvas.x==440&&canvas.y==370);
 apply_setting(12,50);render_hash();for(size_t offset=3;offset<size_t(canvas.pitch)*canvas.height;offset+=4)assert(canvas.pixels[offset]<=127);
 apply_setting(12,100);apply_setting(9,0);apply_setting(10,100);apply_setting(11,0);render_hash();assert(canvas.x==860&&canvas.y==25);
 auto default_mini=pixels_hash(canvas);apply_setting(13,1);assert(render_hash()!=default_mini);apply_setting(13,0);

 auto original=render_hash();apply_setting(3,2000);assert(render_hash()!=original);apply_setting(3,1200);original=render_hash();apply_setting(4,1);assert(render_hash()!=original);apply_setting(4,0);
 e.kind=ANY_KEY_DOWN;e.key='K';m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);original=render_hash();apply_setting(5,0);e.kind=ANY_KEY_DOWN;m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);assert(render_hash()!=original); // entering Settings closes the map
 original=render_hash();apply_setting(5,1);e.kind=ANY_KEY_DOWN;m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);assert(render_hash()!=original);
 original=render_hash();apply_setting(6,1.4);e.kind=ANY_KEY_DOWN;m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);assert(render_hash()!=original);apply_setting(6,1);e.kind=ANY_KEY_DOWN;m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);render_hash();
 // Place a waypoint; guidance controls its panel height and custom label its pixels.
 e.kind=ANY_MOUSE_DOWN;e.button=1;e.x=838;e.y=269;m.input(&e,nullptr);e.kind=ANY_MOUSE_UP;m.input(&e,nullptr);render_hash();png(canvas,L"Anymap_Connected_Route.png");e.kind=ANY_KEY_DOWN;e.key='K';m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);render_hash();assert(canvas.height==385);original=pixels_hash(canvas);apply_setting(15,0);assert(render_hash()!=original);apply_setting(15,1);original=render_hash();
 apply_setting(8,0,"Destination");assert(render_hash()!=original);apply_setting(7,0);render_hash();assert(canvas.height==300);apply_setting(7,1);render_hash();assert(canvas.height==385);
 e.kind=ANY_KEY_DOWN;e.key='K';m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);render_hash();e.kind=ANY_MOUSE_UP;e.button=2;m.input(&e,nullptr);e.kind=ANY_KEY_DOWN;e.key='K';m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);apply_setting(8,0,"Waypoint");render_hash();assert(canvas.height==300);
 // Shift-click -> type Unicode -> save, rename, cancel, route and delete using the real map DLL.
 e.kind=ANY_KEY_DOWN;e.key='K';m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);render_hash();
 auto click=[&](int x,int y){AnyInputV1 mouse;mouse.kind=ANY_MOUSE_DOWN;mouse.button=1;mouse.x=x;mouse.y=y;m.input(&mouse,nullptr);mouse.kind=ANY_MOUSE_UP;m.input(&mouse,nullptr);};
 auto full_grid=render_hash();apply_setting(14,0);e.kind=ANY_KEY_DOWN;e.key='K';m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);assert(render_hash()!=full_grid);apply_setting(14,1);e.kind=ANY_KEY_DOWN;m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);render_hash();
 e.kind=ANY_KEY_DOWN;e.key=VK_SHIFT;m.input(&e,nullptr);click(610,510);render_hash();png(canvas,L"Named_Marker_Editor.png");e.kind=ANY_KEY_UP;m.input(&e,nullptr);e.kind=ANY_TEXT_INPUT;for(char c:std::string("Home")){e.key=c;assert(m.input(&e,nullptr));}e.key=0x1f3e0;assert(m.input(&e,nullptr));e.kind=ANY_KEY_DOWN;e.key=VK_RETURN;m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);
 render_hash();png(canvas,L"Named_Marker_Atlas.png");mapmarkers::Store marker_file;marker_file.file=directory/L"AnyMap"/L"markers.tsv";marker_file.load();assert(marker_file.records.size()==1&&marker_file.records[0].name=="Home\xf0\x9f\x8f\xa0");
 click(610,510);e.kind=ANY_TEXT_INPUT;for(char c:std::string("Base")){e.key=c;m.input(&e,nullptr);}e.kind=ANY_KEY_DOWN;e.key=VK_RETURN;m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);marker_file.records.clear();marker_file.load();assert(marker_file.records[0].name=="Base");render_hash();png(canvas,L"Named_Marker_Atlas.png");
 click(610,510);e.kind=ANY_TEXT_INPUT;e.key='X';m.input(&e,nullptr);e.kind=ANY_KEY_DOWN;e.key=VK_ESCAPE;m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);assert(capture);marker_file.records.clear();marker_file.load();assert(marker_file.records[0].name=="Base");
 click(610,510);render_hash();click(660,580);marker_file.records.clear();marker_file.load();assert(marker_file.active==1); // Route button inside centered editor
 click(60,115);render_hash();click(80,890);render_hash();marker_file.records.clear();marker_file.load();assert(marker_file.active==1);png(canvas,L"Anymap_Route_Panel.png");
 click(210,890);render_hash();click(760,580);marker_file.records.clear();marker_file.load();assert(marker_file.records.empty()&&marker_file.active==0);
 for(int item=0;item<16;++item){e.kind=ANY_KEY_DOWN;e.key=VK_SHIFT;m.input(&e,nullptr);click(610,510);e.kind=ANY_KEY_UP;m.input(&e,nullptr);e.kind=ANY_TEXT_INPUT;for(char ch:std::string("Place ")+std::to_string(item+1)){e.key=ch;m.input(&e,nullptr);}e.kind=ANY_KEY_DOWN;e.key=VK_RETURN;m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);}
 render_hash();auto list_before=pixels_hash(canvas);e.kind=ANY_MOUSE_WHEEL;e.x=80;e.y=150;e.wheel=-120;m.input(&e,nullptr);assert(render_hash()!=list_before);click(60,115);render_hash();click(80,890);render_hash();marker_file.records.clear();marker_file.load();assert(marker_file.active==3);png(canvas,L"Anymap_Marker_List.png");
 e.kind=ANY_MOUSE_UP;e.button=2;m.input(&e,nullptr);e.kind=ANY_KEY_DOWN;e.key='K';m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);
 e.kind=ANY_KEY_DOWN;e.key='K';m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);render_hash();click(1030,844);render_hash();png(canvas,L"Anymap_Minimap_Placement.png");
 e.kind=ANY_MOUSE_DOWN;e.button=1;e.x=900;e.y=70;m.input(&e,nullptr);e.kind=ANY_MOUSE_MOVE;e.x=200;e.y=260;m.input(&e,nullptr);render_hash();e.kind=ANY_MOUSE_UP;m.input(&e,nullptr);click(600,980);render_hash();
 e.kind=ANY_KEY_DOWN;e.key='K';m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);render_hash();assert(canvas.x<250&&canvas.y>100);assert(std::filesystem::exists(directory/L"AnyMap"/L"minimap-position.dat"));
 e.kind=ANY_KEY_DOWN;e.key='K';m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);render_hash();click(1030,894);render_hash();
 e.kind=ANY_KEY_DOWN;e.key='K';m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);render_hash();assert(canvas.x==860&&canvas.y==25);
 e.kind=ANY_KEY_DOWN;e.key='M';assert(!m.input(&e,nullptr)&&!capture);e.key='K';assert(m.input(&e,nullptr)&&capture);frame.tick+=40;canvas={};m.render(&frame,&canvas,nullptr);assert(canvas.width==1200);e.kind=ANY_KEY_UP;m.input(&e,nullptr);e.kind=ANY_KEY_DOWN;m.input(&e,nullptr);assert(!capture);
 event(ANY_MENU_OPEN);frame.tick+=40;canvas={};m.render(&frame,&canvas,nullptr);assert(!canvas.pixels);e.kind=ANY_KEY_UP;m.input(&e,nullptr);e.kind=ANY_KEY_DOWN;assert(!m.input(&e,nullptr)&&!capture);
 draw(1);assert(capture);e.key=VK_ESCAPE;assert(input(&e)&&!capture&&!dirty());starts=capture_starts;for(int i=0;i<300;++i)draw(0);assert(!capture&&capture_starts==starts);e.kind=ANY_KEY_UP;input(&e);draw(1);e.kind=ANY_FOCUS_LOST;input(&e);assert(!capture);
 draw(1);assert(capture);((void(*)())GetProcAddress(fixture,"FixtureTimeout"))();draw(0);assert(!capture); // timeout releases without restarting
 event(ANY_MENU_OPEN);draw(2);assert(!capture&&dirty()&&bindings->key(1)=='K');event(ANY_MENU_DEACTIVATE);assert(dirty());event(ANY_MENU_CANCEL);assert(!dirty()&&bindings->key(1)=='K');
 event(ANY_MENU_OPEN);draw(1);assert(capture);event(ANY_MENU_DEACTIVATE);assert(!capture);event(ANY_MENU_CANCEL);
 // A fresh cached gameplay snapshot must not keep map UI alive in native menus.
 event(ANY_MENU_CANCEL);
 for(int state:{2,13,11,-1}){ui_set(state);frame.tick+=150;canvas={};m.render(&frame,&canvas,nullptr);assert(!canvas.pixels&&!capture);e.kind=ANY_KEY_DOWN;e.key='K';assert(!m.input(&e,nullptr));e.kind=ANY_KEY_UP;m.input(&e,nullptr);}
 ui_set(1);frame.tick+=150;canvas={};m.render(&frame,&canvas,nullptr);assert(canvas.pixels);
 e.kind=ANY_KEY_DOWN;e.key='K';m.input(&e,nullptr);e.kind=ANY_KEY_UP;m.input(&e,nullptr);assert(capture);ui_set(13);e.kind=ANY_MOUSE_MOVE;assert(!m.input(&e,nullptr)&&!capture);canvas={};m.render(&frame,&canvas,nullptr);assert(!canvas.pixels);ui_set(1);
 event(ANY_MENU_RESET);assert(dirty()&&bindings->key(1)=='K');event(ANY_MENU_APPLY);assert(bindings->key(1)=='M'&&!dirty());event(ANY_MENU_CANCEL);m.shutdown(nullptr);
 for(int i=0;i<255;++i){std::string id="action."+std::to_string(i);ModControlActionV1 action;action.mod_id="long_list";action.mod_name="Long list fixture";action.action_id=id.c_str();action.label="Long list action";assert(bindings->register_action(&action)==uint64_t(i+2));}
 event(ANY_MENU_OPEN);draw(0);assert(((uint32_t(*)())GetProcAddress(fixture,"FixtureRows"))()==257);event(ANY_MENU_CANCEL);
 std::filesystem::remove(directory/L"AnyHelpers"/L"settings.tsv");std::filesystem::remove(directory/L"AnyHelpers"/L"bindings.tsv");std::filesystem::remove(directory/L"AnyHelpers");std::filesystem::remove(directory/L"AnyMap"/L"minimap-position.dat");std::filesystem::remove(directory/L"AnyMap"/L"markers.tsv");std::filesystem::remove(directory/L"AnyMap"/L"waypoint.dat");std::filesystem::remove(directory/L"AnyMap");std::filesystem::remove(directory);
 std::cout<<"PASS: actual AnyHelpers and AnyMap DLLs, two settings tabs, typed settings, committed rendering, legacy service alias, optional service discovery, native-menu callback contract, capture, duplicate input, Apply/Cancel/Reset, rebound map toggle, settings hiding, focus/timeout release, no capture restarts over repeated frames, 256-action list\n";}
