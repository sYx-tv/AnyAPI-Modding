#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include "anyapi_mod_v1.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>
#include <fstream>
static uint32_t flags=2;static bool capture{},available=true;static uint64_t epoch=1;
static bool copy(AnySessionPlayersV1* out,uint32_t* ui){*out={};out->sampled_tick=GetTickCount64();out->world_epoch=epoch;out->count=2;*ui=flags;
 for(int i=0;i<2;++i){auto& p=out->players[i];p.valid_fields=PLAYER_POSITION|PLAYER_FACING|PLAYER_NAME|(i==0?PLAYER_LOCAL:0);p.position[0]=83955.394+i*20;p.position[2]=55845.817+i*20;p.yaw_radians=.6+i;strcpy_s(p.steam_name,i?"Remote fixture":"Local fixture");}return available;}
static void log(uint32_t,const char*,const char* msg){std::cout<<msg<<'\n';}
static void input_capture(uint32_t active){capture=active!=0;}
static void png(const AnyCanvasV1& c,const std::filesystem::path& path){
 Gdiplus::GdiplusStartupInput in;ULONG_PTR token{};assert(Gdiplus::GdiplusStartup(&token,&in,nullptr)==Gdiplus::Ok);
 {Gdiplus::Bitmap image(c.width,c.height,c.pitch,PixelFormat32bppPARGB,(BYTE*)c.pixels);UINT n{},size{};Gdiplus::GetImageEncodersSize(&n,&size);std::vector<uint8_t> memory(size);auto codecs=(Gdiplus::ImageCodecInfo*)memory.data();Gdiplus::GetImageEncoders(n,size,codecs);bool saved=false;
 for(UINT i=0;i<n;++i)if(!wcscmp(codecs[i].MimeType,L"image/png")){saved=image.Save(path.c_str(),&codecs[i].Clsid)==Gdiplus::Ok;break;}assert(saved);}
 Gdiplus::GdiplusShutdown(token);
}
int wmain(int argc,wchar_t** argv){assert(argc==4);HMODULE dll=LoadLibraryW(argv[1]);assert(dll);auto init=(AnyModInitV1)GetProcAddress(dll,"AnyAPI_ModInit");assert(init);
 std::filesystem::path output=argv[3];std::filesystem::create_directories(output);std::filesystem::create_directories(output/L"AnyMap");{std::ofstream f(output/L"AnyMap"/L"minimap-position.dat");f<<"ANYMAP_POSITION 1 1 0.25 0.5";}auto folder=output.wstring();AnyModHostV1 host{};host.game_directory=argv[2];host.plugin_directory=folder.c_str();host.log=log;host.copy_players=copy;host.capture_input=input_capture;AnyModCallbacksV1 callbacks;
 auto invalid=host;invalid.abi=99;assert(!init(&invalid,&callbacks));assert(init(&host,&callbacks));assert(callbacks.render&&callbacks.input&&callbacks.shutdown);
 AnyFrameV1 frame{};frame.width=1200;frame.height=1040;frame.focused=1;frame.tick=GetTickCount64();AnyCanvasV1 canvas{};callbacks.render(&frame,&canvas,nullptr);assert(canvas.pixels&&canvas.width==320&&canvas.height>=300);assert(canvas.x==230&&canvas.y==25+(1040-int(canvas.height)-50)/2);png(canvas,output/L"DLL_Minimap.png");
 AnyInputV1 e;e.kind=ANY_KEY_DOWN;e.key='M';assert(callbacks.input(&e,nullptr)&&capture);assert(callbacks.input(&e,nullptr)&&capture); // repeat must not close
 frame.tick+=40;canvas={};callbacks.render(&frame,&canvas,nullptr);assert(canvas.width==1200&&canvas.height==1040);png(canvas,output/L"DLL_Atlas.png");
 e.kind=ANY_KEY_UP;callbacks.input(&e,nullptr);e.kind=ANY_KEY_DOWN;callbacks.input(&e,nullptr);assert(!capture);
 flags=3;canvas={};callbacks.render(&frame,&canvas,nullptr);assert(!canvas.pixels); // actual inventory state hides minimap
 flags=2;available=false;canvas={};callbacks.render(&frame,&canvas,nullptr);assert(!canvas.pixels); // unavailable players hide minimap
 available=true;epoch=2;frame.tick+=40;canvas={};callbacks.render(&frame,&canvas,nullptr);assert(canvas.pixels);
 e.kind=ANY_KEY_UP;callbacks.input(&e,nullptr);e.kind=ANY_KEY_DOWN;callbacks.input(&e,nullptr);assert(capture);
 e.kind=ANY_FOCUS_LOST;callbacks.input(&e,nullptr);assert(!capture);frame.focused=0;canvas={};callbacks.render(&frame,&canvas,nullptr);assert(!canvas.pixels);
 callbacks.shutdown(nullptr);FreeLibrary(dll);std::cout<<"PASS: actual DLL initialization, embedded road data, map/minimap rendering, M repeat suppression, inventory/stale/focus hiding\n";
}
