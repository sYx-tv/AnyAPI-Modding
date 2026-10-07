#include "anyapi_hud_hint_provider_v1.h"
#include "hud_hint_contract.h"
namespace hud_hints {
using namespace hud_hint_contract;
using UpdateFn=void(*)(void*,void*,void*);
using ScreenFn=void(*)(void*,const GeoString16*,const Vec2S32*,const int32_t*,const int32_t*,const int32_t*,const int32_t*,const int32_t*,const bool*);
using LayoutFn=void(*)(void*,const GeoString16*,const Vec2S32*,const int32_t*,const int32_t*,const int32_t*,const Vec2S32*,const int32_t*);
using TextFn=void(*)(void*,const GeoString16*,const GeoString16*,const Vec2S32*);
struct Icon {uintptr_t region{};GeoString16 text{};uint32_t icon_color{},text_color{};Vec2S32 offset{};};
static_assert(sizeof(Icon)==40);
using KeyFn=void(*)(Icon*,void*,const int32_t*);
using IconFn=void(*)(void*,const GeoString16*,const Icon*,const Vec2S32*);
using IconDtor=void(*)(Icon*);
static UpdateFn original_update;static ScreenFn original_screen;static LayoutFn layout;static TextFn text;
static KeyFn key_icon;static IconFn icon;static IconDtor icon_dtor;static P26EndContainerFn original_end;
static uintptr_t update{};static int32_t row_height{};static bool ready{};
static thread_local bool inside{};static thread_local Vec2S32 panel{};
static thread_local std::vector<AnyHudHintV1> rows;
static bool invoke(const AnyHudHintProviderV1* provider,AnyHudHintV1* row){
 __try{return provider&&provider->struct_size==sizeof(*provider)&&provider->version==1&&provider->copy&&provider->copy(row);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static std::vector<AnyHudHintV1> collect(){std::vector<services::Published> providers;{std::lock_guard lock(services::mutex);providers=services::published;}
 std::vector<AnyHudHintV1> result;for(auto& p:providers){if(p.version!=1||p.id.rfind("hud.hints.",0)!=0||!anyapi_owner_active(p.owner))continue;AnyHudHintV1 row;
 int previous=platform::current_plugin;platform::current_plugin=p.owner;bool valid=invoke((const AnyHudHintProviderV1*)p.value,&row);platform::current_plugin=previous;
 if(valid&&row.struct_size==sizeof(row)&&row.version==1&&row.key>0&&row.key<256&&row.label[0]&&memchr(row.label,0,sizeof(row.label))&&result.size()<8)result.push_back(row);
 }return result;
}
// The engine uses GLFW key IDs; the public input API uses Windows virtual keys.
static int32_t native_key(uint32_t vk){if(vk>=VK_NUMPAD0&&vk<=VK_NUMPAD9)return 320+vk-VK_NUMPAD0;if(vk>=VK_F1&&vk<=VK_F24)return 290+vk-VK_F1;
 switch(vk){case VK_DECIMAL:return 330;case VK_DIVIDE:return 331;case VK_MULTIPLY:return 332;case VK_SUBTRACT:return 333;case VK_ADD:return 334;case VK_CAPITAL:return 280;case VK_SCROLL:return 281;case VK_NUMLOCK:return 282;case VK_SNAPSHOT:return 283;case VK_PAUSE:return 284;case VK_LWIN:return 343;case VK_RWIN:return 347;case VK_ESCAPE:return 256;case VK_RETURN:return 257;case VK_TAB:return 258;case VK_BACK:return 259;case VK_INSERT:return 260;case VK_DELETE:return 261;case VK_RIGHT:return 262;case VK_LEFT:return 263;case VK_DOWN:return 264;case VK_UP:return 265;case VK_PRIOR:return 266;case VK_NEXT:return 267;case VK_HOME:return 268;case VK_END:return 269;case VK_SHIFT:case VK_LSHIFT:return 340;case VK_CONTROL:case VK_LCONTROL:return 341;case VK_MENU:case VK_LMENU:return 342;case VK_RSHIFT:return 344;case VK_RCONTROL:return 345;case VK_RMENU:return 346;case VK_OEM_MINUS:return '-';case VK_OEM_PLUS:return '=';case VK_OEM_COMMA:return ',';case VK_OEM_PERIOD:return '.';case VK_OEM_1:return ';';case VK_OEM_2:return '/';case VK_OEM_3:return '`';case VK_OEM_4:return '[';case VK_OEM_5:return '\\';case VK_OEM_6:return ']';case VK_OEM_7:return '\'';default:return int32_t(vk);}
}
static void screen_hook(void* ui,const GeoString16* id,const Vec2S32* size,const int32_t* a,const int32_t* b,const int32_t* c,const int32_t* d,const int32_t* mode,const bool* focus){
 Vec2S32 adjusted=*size;if(inside&&ready){panel=*size;adjusted.y+=int32_t(rows.size())*row_height;}original_screen(ui,id,&adjusted,a,b,c,d,mode,focus);
}
static void append(void* ui){int32_t left=0,top=0,horizontal=0,spacing=0;Vec2S32 zero{0,0};
 for(size_t i=0;i<rows.size();++i){auto name="anyapi.hud_hint."+std::to_string(i);auto id=make_const_geostring(name.c_str());Vec2S32 size{panel.x,row_height};
 layout(ui,&id,&size,&left,&top,&horizontal,&zero,&spacing);
 auto icon_name=name+".key",text_name=name+".label";auto icon_id=make_const_geostring(icon_name.c_str()),text_id=make_const_geostring(text_name.c_str()),label=make_const_geostring(rows[i].label);
 Icon descriptor{};int32_t key=native_key(rows[i].key);key_icon(&descriptor,ui,&key);Vec2S32 key_size{row_height,row_height},text_size{std::max(1,panel.x-row_height),row_height};
 icon(ui,&icon_id,&descriptor,&key_size);icon_dtor(&descriptor);text(ui,&text_id,&label,&text_size);original_end(ui);
 }
}
static void end_hook(void* ui){if(inside&&ready&&(uintptr_t)_ReturnAddress()==update+0x15ff)append(ui);original_end(ui);}
static void update_hook(void* ui,void* settings,void* hints){bool previous=inside;auto previous_rows=std::move(rows);rows=collect();inside=true;original_update(ui,settings,hints);inside=previous;rows=std::move(previous_rows);}
static menus::Replacement replacements[3];
static bool prepare(){auto hits=scan_exact(HUD_UPDATE,sizeof(HUD_UPDATE)),parents=scan_exact(HUD_PARENT,sizeof(HUD_PARENT)),keys=scan_exact(HUD_KEY,sizeof(HUD_KEY));if(hits.size()!=1||parents.size()!=1||keys.size()!=1)return false;
 uintptr_t fn{},screen{},end{},row{},txt{},ico{},dtor{},height_pointer{};
 if(!p34_cell(parents[0],PARENT_OFFSET,fn)||fn!=hits[0]||!p34_cell(hits[0],UPDATE_END+17*8,screen)||!p34_body(screen,HUD_SCREEN)||!p34_cell(hits[0],UPDATE_END+37*8,end)||!p34_body(end,HUD_END)||!p34_cell(hits[0],UPDATE_END+21*8,row)||!p34_body(row,HUD_LAYOUT)||!p34_cell(hits[0],UPDATE_END+36*8,txt)||!p34_body(txt,HUD_TEXT)||!p34_cell(hits[0],UPDATE_END+31*8,ico)||!p34_body(ico,HUD_ICON)||!p34_cell(keys[0],KEY_END+4*8,dtor)||!safe_read_memory(hits[0]+UPDATE_END+2*8,&height_pointer,8)||!safe_read_memory(height_pointer,&row_height,4)||row_height<1||row_height>16)return false;
 update=hits[0];original_update=(UpdateFn)fn;original_screen=(ScreenFn)screen;original_end=(P26EndContainerFn)end;layout=(LayoutFn)row;text=(TextFn)txt;key_icon=(KeyFn)keys[0];icon=(IconFn)ico;icon_dtor=(IconDtor)dtor;
 replacements[0]={parents[0],PARENT_OFFSET,0,(uintptr_t)update_hook};replacements[1]={update,UPDATE_END+17*8,0,(uintptr_t)screen_hook};replacements[2]={update,UPDATE_END+37*8,0,(uintptr_t)end_hook};
 size_t count=0;for(auto& r:replacements){if(!safe_read_memory(r.owner+r.offset,&r.previous,8)||!menus::replace(r)){while(count)menus::restore(replacements[--count]);return false;}++count;}ready=true;log_line(ANY_LOG_INFO,"framework","HUD_HINTS ready=1 native_control_panel=1 native_key_icons=1");return true;
}
static DWORD WINAPI worker(void*){for(unsigned i=0;i<120;++i){if(prepare())return 0;Sleep(1000);}log_line(ANY_LOG_WARN,"framework","HUD_HINTS unavailable=native_contract_unmatched");return 0;}
}
