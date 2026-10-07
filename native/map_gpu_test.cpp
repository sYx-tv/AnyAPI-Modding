#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <cassert>
#include <iostream>
#include "anymaker_mod_api.h"
#include "anyapi_mod_v1.h"
static std::filesystem::path g_game_dir;
static void log_line(AnymakerLogLevel,const char*,const char* msg){std::cout<<msg<<'\n';}
static std::string utf8(const std::filesystem::path& p){return p.filename().string();}
static bool p34_copy_snapshot(AnySessionPlayersV1*,uint32_t*);
#include "anyapi_platform.inc"
#include "anyapi_creation_balance_v1.h"
static const AnyCreationBalanceV1* anyapi_creation_balance_service(){return nullptr;}
static const AnyEquipmentV1* anyapi_equipment_service(){return nullptr;}
static const AnyEquipmentV2* anyapi_equipment_service_v2(){return nullptr;}
static const AnyWorldTimeV1* anyapi_world_time_service(){return nullptr;}
static const AnyBuildV1* anyapi_build_service(){return nullptr;}
#include "anyapi_client_tasks.inc"
static const AnyMenuV1* anyapi_menu_service(){return nullptr;}
static const AnyMenuV2* anyapi_menu_service_v2(){return nullptr;}
static const AnyMenuV3* anyapi_menu_service_v3(){return nullptr;}
static bool p34_copy_snapshot(AnySessionPlayersV1*,uint32_t*){return false;}
static const AnyInventoryActionsV1* anyapi_inventory_actions_service(){return nullptr;}
static const AnyScreenLayoutV1* anyapi_screen_layout_service(){return nullptr;}
static const AnySessionV1* anyapi_session_service(){return nullptr;}
static const AnyUiStateV1* anyapi_ui_state_service(){return nullptr;}
static const AnyInventoryUiV1* anyapi_inventory_ui_service(){return nullptr;}
static const AnyInventoryUiV2* anyapi_inventory_ui_service_v2(){return nullptr;}
static const AnyInventoryUiV3* anyapi_inventory_ui_service_v3(){return nullptr;}
static const AnyInventoryActionsV2* anyapi_inventory_actions_service_v2(){return nullptr;}
using Microsoft::WRL::ComPtr;

#include <gdiplus.h>
#include <chrono>
static uint32_t flags=2;static bool available=true;static double yaw=.6;
static bool players(AnySessionPlayersV1* out,uint32_t* ui){*out={};out->sampled_tick=GetTickCount64();out->world_epoch=1;out->count=2;*ui=flags;for(int i=0;i<2;++i){auto& p=out->players[i];p.peer_id=p.actor_id=i;p.valid_fields=PLAYER_POSITION|PLAYER_FACING|PLAYER_NAME|(i==0?PLAYER_LOCAL:0);p.position[0]=83955.394+i*20;p.position[2]=55845.817+i*20;p.yaw_radians=yaw+i;strcpy_s(p.steam_name,i?"Remote fixture":"Local fixture");}return available;}
static void log(uint32_t,const char*,const char* m){std::cout<<m<<'\n';}
static void capture(uint32_t){}
static void png(uint32_t w,uint32_t h,uint32_t pitch,const uint8_t* pixels,const std::filesystem::path& path){Gdiplus::Bitmap image(w,h,pitch,PixelFormat32bppPARGB,(BYTE*)pixels);UINT count{},size{};Gdiplus::GetImageEncodersSize(&count,&size);std::vector<uint8_t> mem(size);auto codecs=(Gdiplus::ImageCodecInfo*)mem.data();Gdiplus::GetImageEncoders(count,size,codecs);for(UINT i=0;i<count;++i)if(!wcscmp(codecs[i].MimeType,L"image/png")){assert(image.Save(path.c_str(),&codecs[i].Clsid)==Gdiplus::Ok);return;}assert(false);}
int wmain(int argc,wchar_t** argv){assert(argc==5);Gdiplus::GdiplusStartupInput in;ULONG_PTR token{};assert(Gdiplus::GdiplusStartup(&token,&in,nullptr)==Gdiplus::Ok);
 HMODULE fixture=LoadLibraryW(argv[2]);assert(fixture);auto services=((AnyGetServicesV1)GetProcAddress(fixture,"AnyAPI_GetServices"))(1);assert(services->publish("anyapi.gpu_draw",1,&gpu_draw::api));((void(*)(int32_t))GetProcAddress(fixture,"FixtureUiState"))(1);
 HMODULE mod=LoadLibraryW(argv[1]);assert(mod);auto init=(AnyModInitV1)GetProcAddress(mod,"AnyAPI_ModInit");auto ready=(void(*)())GetProcAddress(mod,"AnyAPI_ModReady");assert(init&&ready);std::filesystem::path output=argv[4];std::filesystem::create_directories(output);auto folder=output.wstring();AnyModHostV1 host;host.game_directory=argv[3];host.plugin_directory=folder.c_str();host.log=log;host.copy_players=players;host.capture_input=capture;AnyModCallbacksV1 callbacks;assert(init(&host,&callbacks));platform::plugins[0].callbacks=callbacks;platform::plugin_count=1;
 AnyFrameV1 frame;frame.width=2560;frame.height=1440;frame.focused=1;frame.tick=1000;AnyInputV1 key;key.kind=ANY_KEY_DOWN;key.key='M';AnyCanvasV1 warm;callbacks.render(&frame,&warm,nullptr);callbacks.input(&key,nullptr);double cpu_ms{};AnyCanvasV1 canvas;for(int i=0;i<8;++i){frame.tick+=17;auto start=std::chrono::steady_clock::now();callbacks.render(&frame,&canvas,nullptr);cpu_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}assert(canvas.pixels);png(canvas.width,canvas.height,canvas.pitch,canvas.pixels,output/L"Map_CPU.png");
 platform::current_plugin=0;ready();platform::current_plugin=-1;assert(gpu_draw::renderers[0].callback);
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> immediate;assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&immediate)));ComPtr<IDXGIDevice> dxgi;assert(SUCCEEDED(device.As(&dxgi)));assert(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,IID_PPV_ARGS(&platform::factory2d))));assert(SUCCEEDED(platform::factory2d->CreateDevice(dxgi.Get(),&platform::device2d)));assert(SUCCEEDED(platform::device2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&platform::context2d)));
 auto collect=[&](){platform::current_plugin=0;bool result=gpu_draw::collect(0,frame);platform::current_plugin=-1;return result;};frame.tick+=17;assert(collect());
 auto save=[&](const wchar_t* name){auto ctx=platform::context2d.Get();ComPtr<ID2D1Bitmap1> target,readback;auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);assert(SUCCEEDED(ctx->CreateBitmap(D2D1::SizeU(frame.width,frame.height),nullptr,0,&props,&target)));ctx->SetTarget(target.Get());ctx->BeginDraw();ctx->Clear(D2D1::ColorF(0,0,0,1));gpu_draw::render(0);assert(SUCCEEDED(ctx->EndDraw()));ctx->SetTarget(nullptr);props.bitmapOptions=D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW;assert(SUCCEEDED(ctx->CreateBitmap(D2D1::SizeU(frame.width,frame.height),nullptr,0,&props,&readback)));assert(SUCCEEDED(readback->CopyFromBitmap(nullptr,target.Get(),nullptr)));D2D1_MAPPED_RECT map;assert(SUCCEEDED(readback->Map(D2D1_MAP_OPTIONS_READ,&map)));png(frame.width,frame.height,map.pitch,map.bits,output/name);readback->Unmap();};save(L"Map_GPU.png");assert(gpu_draw::renderers[0].uploads==1);
 double gpu_ms{};for(int i=0;i<120;++i){frame.tick+=7;auto start=std::chrono::steady_clock::now();assert(collect());gpu_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}save(L"Map_GPU_cached.png");assert(gpu_draw::renderers[0].uploads==1);canvas={};callbacks.render(&frame,&canvas,nullptr);assert(!canvas.pixels);std::cout<<"BENCHMARK full_map_cpu_raster_ms="<<cpu_ms/8<<" gpu_command_collect_ms="<<gpu_ms/120<<" GPU execution not included; game FPS unmeasured\n";
 key.kind=ANY_KEY_UP;callbacks.input(&key,nullptr);key.kind=ANY_KEY_DOWN;callbacks.input(&key,nullptr);frame.tick+=17;assert(collect());save(L"Minimap_GPU.png");for(auto& command:gpu_draw::renderers[0].commands)if(command.value.kind==ANY_GPU_IMAGE){assert(command.value.rect[2]==384&&std::isfinite(command.value.matrix[0])&&std::abs(command.value.matrix[0]*command.value.matrix[3]-command.value.matrix[1]*command.value.matrix[2])>.1f);}
 yaw=1.57;frame.tick+=500;assert(collect());save(L"Minimap_GPU_turn.png");assert(gpu_draw::renderers[0].uploads==1);
 ((void(*)(int32_t))GetProcAddress(fixture,"FixtureUiState"))(2);assert(!collect());((void(*)(int32_t))GetProcAddress(fixture,"FixtureUiState"))(1);available=false;assert(!collect());available=true;frame.focused=0;assert(!collect());callbacks.shutdown(nullptr);gpu_draw::reset();FreeLibrary(mod);FreeLibrary(fixture);Gdiplus::GdiplusShutdown(token);std::cout<<"PASS: actual map DLL GPU rendering, texture residency, every-frame batches, UI/focus/data hiding and CPU fallback\n";
}
