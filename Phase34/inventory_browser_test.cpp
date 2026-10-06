#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include "anyapi_services_v1.h"
#include "anyapi_item_catalog_v1.h"
#include "anyapi_item_images_v1.h"
#include "anyapi_session_v1.h"
#include "anyapi_menu_v1.h"
#include "map_markers.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
static bool captured{},available=true;static uint32_t ui=3;
static void capture(uint32_t value){captured=value!=0;}
static void log(uint32_t,const char*,const char* value){std::cout<<value<<'\n';}
static bool players(AnySessionPlayersV1* out,uint32_t* flags){*out={};out->sampled_tick=GetTickCount64();*flags=ui;return available;}
static void png(const AnyCanvasV1& c,const std::filesystem::path& p){Gdiplus::Bitmap b(c.width,c.height,c.pitch,PixelFormat32bppPARGB,(BYTE*)c.pixels);UINT n,size;Gdiplus::GetImageEncodersSize(&n,&size);std::vector<uint8_t> bytes(size);auto codecs=(Gdiplus::ImageCodecInfo*)bytes.data();Gdiplus::GetImageEncoders(n,size,codecs);for(UINT i=0;i<n;++i)if(!wcscmp(codecs[i].MimeType,L"image/png")){assert(b.Save(p.c_str(),&codecs[i].Clsid)==Gdiplus::Ok);return;}assert(false);}
int wmain(int argc,wchar_t** argv){assert(argc==6);auto fixture=LoadLibraryW(argv[1]);assert(fixture);auto load=(uint32_t(*)(const wchar_t*))GetProcAddress(fixture,"FixtureCatalogLoad");assert(load&&load(argv[3])==970);auto api=(const AnyItemCatalogV1*)AnyAPI_Services()->query("anyapi.item_catalog",1);assert(api&&api->count()==970);AnyItemDefinitionV1 metadata;assert(api->copy(0,&metadata)&&!strcmp(metadata.id,"overalls"));auto bad=metadata;bad.struct_size=0;assert(!api->copy(0,&bad));assert(!api->copy(970,&metadata));uint32_t required;assert(!api->definition_json(0,nullptr,0,&required)&&required>100);std::vector<char> bytes(required,42);assert(!api->definition_json(0,bytes.data(),required-1,&required)&&bytes[0]==42);assert(api->definition_json(0,bytes.data(),required,&required)&&strstr(bytes.data(),"mesh_file"));
 auto module=LoadLibraryW(argv[2]);assert(module);auto init=(AnyModInitV1)GetProcAddress(module,"AnyAPI_ModInit");assert(init);std::filesystem::path folder=argv[4];std::filesystem::create_directories(folder/L"AnyInventory");std::filesystem::remove(folder/L"AnyInventory"/L"favorites.txt");std::filesystem::remove(folder/L"AnyHelpers"/L"settings.tsv");std::filesystem::remove(folder/L"AnyHelpers"/L"bindings.tsv");{std::ofstream seeded(folder/L"AnyInventory"/L"favorites.txt");seeded<<"ANYINVENTORY_FAVORITES 1\noveralls\n";}std::wstring path=folder.wstring();AnyModHostV1 host;host.game_directory=argv[3];host.plugin_directory=path.c_str();host.log=log;host.copy_players=players;host.capture_input=capture;auto helpers=LoadLibraryW(argv[5]);assert(helpers);AnyModCallbacksV1 helpers_callbacks;assert(((AnyModInitV1)GetProcAddress(helpers,"AnyAPI_ModInit"))(&host,&helpers_callbacks));AnyModCallbacksV1 callbacks;assert(init(&host,&callbacks));auto ready=(void(*)())GetProcAddress(module,"AnyAPI_ModReady");assert(ready);ready();Gdiplus::GdiplusStartupInput in;ULONG_PTR token;assert(Gdiplus::GdiplusStartup(&token,&in,nullptr)==Gdiplus::Ok);

 auto add_calls=(uint32_t(*)())GetProcAddress(fixture,"FixtureAddCalls");auto add_id=(const char*(*)())GetProcAddress(fixture,"FixtureAddId");auto add_available=(void(*)(uint32_t))GetProcAddress(fixture,"FixtureAddAvailable");auto layout_offset=(double(*)())GetProcAddress(fixture,"FixtureLayoutOffset");assert(add_calls&&add_id&&add_available&&layout_offset);
 auto images=(const AnyItemImagesV1*)AnyAPI_Services()->query("anyapi.item_images",1);assert(images);std::vector<uint8_t> image(65536,42);assert(!images->copy(0,image.data(),65535,&required)&&required==65536&&image[0]==42);assert(images->copy(0,image.data(),65536,&required));size_t opaque=0;for(size_t i=3;i<image.size();i+=4)opaque+=image[i]!=0;assert(opaque>100);
 AnyFrameV1 frame;frame.width=1920;frame.height=1080;frame.focused=1;frame.tick=GetTickCount64();AnyCanvasV1 canvas;
 auto render=[&](){frame.tick+=150;canvas={};callbacks.render(&frame,&canvas,nullptr);};
 auto mouse=[&](uint32_t kind,int x,int y){AnyInputV1 e;e.kind=kind;e.button=1;e.x=canvas.x+x;e.y=canvas.y+y;return callbacks.input(&e,nullptr);};
 auto click=[&](int x,int y){assert(mouse(ANY_MOUSE_DOWN,x,y));assert(mouse(ANY_MOUSE_UP,x,y));};
 auto hash=[&](){uint64_t value=1;for(size_t i=0;i<size_t(canvas.pitch)*canvas.height;++i)value=value*33+canvas.pixels[i];return value;};
 render();assert(canvas.pixels&&canvas.width==480&&canvas.x==1416&&canvas.y==56&&!captured&&layout_offset()<0);png(canvas,folder/L"AnyInventory_Catalog.png");auto baseline=hash();
 AnyInputV1 e;e.kind=ANY_MOUSE_DOWN;e.button=1;e.x=500;e.y=300;assert(!callbacks.input(&e,nullptr));click(60,76);assert(captured);e.kind=ANY_TEXT_INPUT;for(char c:std::string("bandage")){e.key=c;assert(callbacks.input(&e,nullptr));}render();assert(hash()!=baseline);click(100,224);assert(!captured);render();png(canvas,folder/L"AnyInventory_Bandage_Details.png");
 auto mode_set=(void(*)(uint32_t))GetProcAddress(fixture,"FixtureMode");assert(mode_set);
 auto sandbox_hash=hash();mode_set(ANY_MODE_SURVIVAL);render();assert(hash()!=sandbox_hash);click(60,902);assert(add_calls()==0);mode_set(ANY_MODE_CAREER);render();click(60,902);assert(add_calls()==0);mode_set(ANY_MODE_UNKNOWN);render();click(60,902);assert(add_calls()==0);png(canvas,folder/L"AnyInventory_Survival.png");mode_set(ANY_MODE_CREATIVE);render();click(60,902);assert(add_calls()==1);mode_set(ANY_MODE_SANDBOX);render();
 // One complete Add click queues exactly one selected item; a stray release does not.
 assert(mouse(ANY_MOUSE_UP,60,902));assert(add_calls()==1);click(60,902);assert(add_calls()==2&&!strcmp(add_id(),"bandage"));render();render();assert(add_calls()==2);add_available(0);click(60,902);assert(add_calls()==2);add_available(1);
 click(446,658);render();std::ifstream favorites(folder/L"AnyInventory"/L"favorites.txt");std::string favorite_text((std::istreambuf_iterator<char>(favorites)),{});assert(favorite_text.find("bandage")!=favorite_text.npos&&favorite_text.find("overalls")!=favorite_text.npos);
 click(60,76);assert(captured);e.kind=ANY_KEY_DOWN;e.key=VK_ESCAPE;assert(callbacks.input(&e,nullptr)&&!captured);e.kind=ANY_KEY_UP;callbacks.input(&e,nullptr);
 assert(mouse(ANY_MOUSE_DOWN,100,224)&&captured);e.kind=ANY_MOUSE_UP;e.button=1;e.x=500;e.y=300;assert(!callbacks.input(&e,nullptr)&&!captured);
 click(425,168);render();auto all_hash=hash();e.kind=ANY_MOUSE_WHEEL;e.x=canvas.x+100;e.y=canvas.y+400;e.wheel=-120;assert(callbacks.input(&e,nullptr));render();assert(hash()!=all_hash);
 click(60,125);render();auto dropdown_hash=hash();click(60,205);render();assert(hash()!=dropdown_hash);click(425,168);render();
 auto ui_set=(void(*)(int32_t))GetProcAddress(fixture,"FixtureUiState");assert(ui_set);
 // Cached players keep reporting inventory while native UI changes immediately.
 for(int state:{1,13,11,-1}){ui_set(state);render();assert(!canvas.pixels&&!captured&&layout_offset()==0);}
 ui_set(2);frame.focused=0;render();assert(!canvas.pixels);frame.focused=1;available=false;render();assert(canvas.pixels);available=true;
 click(60,76);assert(captured);ui_set(13);e.kind=ANY_TEXT_INPUT;e.key='q';assert(!callbacks.input(&e,nullptr)&&!captured&&layout_offset()==0);render();assert(!canvas.pixels);ui_set(2);render();assert(canvas.pixels);
 e.kind=ANY_KEY_DOWN;e.key=VK_F8;assert(callbacks.input(&e,nullptr));assert(callbacks.input(&e,nullptr));render();assert(!canvas.pixels&&layout_offset()==0);e.kind=ANY_KEY_UP;callbacks.input(&e,nullptr);e.kind=ANY_KEY_DOWN;callbacks.input(&e,nullptr);e.kind=ANY_KEY_UP;callbacks.input(&e,nullptr);render();assert(canvas.pixels);
 auto settings_event=(void(*)(uint32_t))GetProcAddress(fixture,"FixtureSettingsEvent");auto stage=(void(*)(uint64_t,double,const char*))GetProcAddress(fixture,"FixtureStage");settings_event(ANY_MENU_OPEN);stage(2,420,nullptr);stage(3,1,nullptr);stage(5,1,nullptr);settings_event(ANY_MENU_APPLY);render();assert(!canvas.pixels&&!captured&&layout_offset()==0);settings_event(ANY_MENU_CANCEL);render();assert(canvas.pixels&&canvas.width==420&&canvas.x==24&&layout_offset()>0);png(canvas,folder/L"AnyInventory_Custom_Settings.png");
 frame.width=2560;frame.height=1440;render();assert(canvas.pixels&&canvas.width==560&&canvas.x==32);png(canvas,folder/L"AnyInventory_1440p.png");
 // Component rows are present and searchable by real localized name.
 frame.width=1920;frame.height=1080;render();click(60,76);e.kind=ANY_TEXT_INPUT;for(char c:std::string("gas port")){e.key=c;assert(callbacks.input(&e,nullptr));}render();click(100,374);render();png(canvas,folder/L"AnyInventory_Gas_Port.png");click(60,902);std::cout<<"COMPONENT_ADD calls="<<add_calls()<<" id="<<add_id()<<std::endl;assert(add_calls()==3&&!strncmp(add_id(),"component:gas_port",18));
 callbacks.shutdown(nullptr);assert(!captured&&layout_offset()==0);FreeLibrary(module);Gdiplus::GdiplusShutdown(token);std::cout<<"PASS: real DLL, current mesh previews, larger layout, native offset, search/categories/favorites, single Add request, unavailable Add, capture release, scale, visibility and duplicate input guard\n";}

