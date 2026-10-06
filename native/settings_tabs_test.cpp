#include "dinput8_proxy.cpp"
#include <cassert>
#include <iostream>
// Execute the exact current native Settings update and tab bodies. Substitute
// their dependencies with deterministic UI fixtures, retaining real call sites.
static int depth{},native_calls{},vanilla_draws{},custom_draws{},key_rows{},footer_calls{},owner_events[2][6]{};
static bool dirty_flags[2]{},default_flags[2]{true,true},enter_event{};
static int click_native=-1;static std::string click_widget;static bool native_focus[4]{};
static std::string str(const GeoString16* s){return {(const char*)s->data,s->length};}
static void nothing(){}
static void begin_state(uint8_t* out,void*,const int32_t* state,const double*){assert(*state==11);*out=1;}
static void cstr(GeoString16* out,const char* s){*out=make_const_geostring(s);}
static void string_ctor(GeoString16* out){*out=make_const_geostring("");}
static void localize(GeoString16* out,void*,const int32_t*){*out=make_const_geostring("NATIVE TAB");}
static void vec_ctor(Vec2S32* out,const int32_t* x,const int32_t* y){*out={*x,*y};}
static void vec_default(Vec2S32* out){*out={};}
static void layout_fixture(void*,const GeoString16*,const Vec2S32*,const int32_t*,const int32_t*,const int32_t*,const Vec2S32*,const int32_t*){++depth;}
static void end_fixture(void*){assert(depth>0);--depth;}
static void (*render_tab)(void*,void*);static int rendered_icons{},custom_tabs{};
static void render_icon_fixture(void*,const Vec2S32*,const Vec2S32*,void* region,const void*){assert(region);assert(*(uint64_t*)((uint8_t*)region+0x28)==0x12345678);++rendered_icons;}
static void color_copy(uint32_t* out,const uint32_t* value){*out=*value;}
static void color_ctor(uint32_t* out,const int32_t*,const int32_t*,const int32_t*,const int32_t*){*out=0xffffffff;}
static void vec_add(Vec2S32* out,const Vec2S32* a,const Vec2S32* b){*out={a->x+b->x,a->y+b->y};}
static void tab_helper(uint8_t* out,void*,const GeoString16* id,const GeoString16*,const int32_t* width,const uintptr_t* icon,const bool* focus,const bool*){assert(*width>0&&*icon!=0);alignas(8) uint8_t element[0x78]{};*(uintptr_t*)(element+0x68)=*icon;*(int32_t*)(element+0x30)=1;render_tab(element,nullptr);auto name=str(id);if(name.starts_with("anyapi.")){++custom_tabs;*out=enter_event&&name==click_widget;}else{int index=native_calls++;assert(index<4);native_focus[index]=*focus;*out=enter_event&&index==click_native;}}
static void enter_fixture(uint8_t* out,void*){*out=enter_event;}
static void vanilla_fixture(void*,void*,void* data){assert(*(int32_t*)data>=0&&*(int32_t*)data<4);++vanilla_draws;}
static void close_fixture(void*,void*){}
static void scroll_fixture(void*,const GeoString16*,const Vec2S32*,const int32_t*,const bool*,int32_t* scroll){assert(scroll);++depth;}
static void table_fixture(void*,const GeoString16*,const int32_t* columns,const Vec2S32* size,const int32_t*){assert(depth==3&&*columns==2&&size->x==24&&size->y==1);++key_rows;++depth;}
static void label_fixture(void*,const GeoString16* id,const GeoString16*,const bool* background){assert(str(id).starts_with("anyapi.")&&!*background);}
static void heading_fixture(void*,const GeoString16*,const GeoString16*){assert(depth==3);}
static void button_fixture(uint8_t* out,void*,const GeoString16* id,const GeoString16*,const Vec2S32* size,const uintptr_t*,const bool* disabled,const bool*){auto name=str(id);if(name.ends_with(".key"))assert(depth==5&&size->x==22&&size->y==1);else if(name.ends_with(".clear"))assert(depth==5&&size->x==1&&size->y==1);else assert(depth==2&&size->x==14&&size->y==3);*out=enter_event&&! *disabled&&name==click_widget;}
static void footer_fixture(void*,void*,void*,void*){assert(depth==2);++footer_calls;}
static void event_fixture(uint32_t event,void* user){int owner=int((uintptr_t)user);++owner_events[owner][event];if(event==ANY_MENU_RESET){dirty_flags[owner]=true;default_flags[owner]=true;}if(event==ANY_MENU_APPLY)dirty_flags[owner]=false;}
static uint32_t dirty_fixture(void* user){return dirty_flags[(uintptr_t)user];}
static uint32_t defaults_fixture(void* user){return default_flags[(uintptr_t)user];}
static void draw_fixture(const AnyMenuFrameV1* frame,void*){assert(frame->location==ANY_MENU_SETTINGS_TAB);++custom_draws;menus::api_v2.heading("group","NATIVE ROWS");for(int i=0;i<256;++i)menus::api_v2.key_row(("action."+std::to_string(i)).c_str(),"OPEN / CLOSE MAP","N",0);menus::api_v2.label("help","Select a binding to change it.");}
static unsigned char* executable(const unsigned char* body,size_t size){auto p=(unsigned char*)VirtualAlloc(nullptr,65536,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(p);memcpy(p,body,size);return p;}
static void seal(unsigned char* p){DWORD old{};assert(VirtualProtect(p,65536,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),p,65536);}
int main(){platform::plugin_count=2;for(int i=0;i<2;++i){platform::plugins[i].path=i?L"B.dll":L"A.dll";platform::plugins[i].callbacks.id=i?"b":"a";platform::current_plugin=i;AnyMenuTabV2 t;t.id="settings";t.title=i?"OTHER MOD":"MOD CONTROLS";t.order=i;t.icon=i?ANY_MENU_ICON_NONE:ANY_MENU_ICON_KEYBOARD;t.user=(void*)(uintptr_t)i;t.draw=draw_fixture;t.event=event_fixture;t.dirty=dirty_fixture;t.defaults=defaults_fixture;assert(menus::api_v2.add_tab(&t));assert(!menus::api_v2.add_tab(&t));}platform::current_plugin=-1;
 assert(services::api.query("anyapi.menu",1)==&menus::api&&services::api.query("anyapi.menu",2)==&menus::api_v2&&services::api.query("anyapi.menu",3)==&menus::api_v3&&!services::api.query("anyapi.menu",4));
 menus::native_enter=enter_fixture;menus::native_label=label_fixture;menus::native_heading=heading_fixture;menus::native_layout=layout_fixture;menus::native_scroll=scroll_fixture;menus::native_table=table_fixture;menus::native_end=end_fixture;menus::native_button=button_fixture;menus::native_footer=footer_fixture;menus::original_close=close_fixture;menus::original_builder=vanilla_fixture;for(auto& f:menus::vanilla_builders)f=vanilla_fixture;
 menus::column_width=24;menus::compact_height=1;menus::fill_cells=-1;menus::body_height=48;menus::footer_height=3;menus::footer_width=14;menus::tab_width=12;menus::header_width=76;menus::ready=true;
 auto render=executable(MENU_TAB_RENDER,sizeof(MENU_TAB_RENDER));uintptr_t render_cells[16]{};uint32_t color=0xffffffff;
 auto rfn=[&](size_t offset,uintptr_t value){size_t i=(offset-sizeof(MENU_TAB_RENDER))/8;render_cells[i]=value;*(uintptr_t*)(render+offset)=(uintptr_t)&render_cells[i];};
 for(size_t offset=0x320;offset<=0x390;offset+=8)rfn(offset,(uintptr_t)nothing);
 for(size_t offset:{size_t(0x320),size_t(0x330),size_t(0x338)})*(uintptr_t*)(render+offset)=(uintptr_t)&color;
 rfn(0x328,(uintptr_t)color_copy);rfn(0x348,(uintptr_t)color_ctor);rfn(0x358,(uintptr_t)vec_ctor);rfn(0x360,(uintptr_t)render_icon_fixture);rfn(0x370,(uintptr_t)vec_default);rfn(0x378,(uintptr_t)vec_add);seal(render);render_tab=(void(*)(void*,void*))render;
 auto tab=executable(MENU_TAB,sizeof(MENU_TAB));uintptr_t tab_cell=(uintptr_t)tab_helper;*(uintptr_t*)(tab+0x98)=(uintptr_t)&tab_cell;seal(tab);menus::native_tab=(menus::TabFn)tab;
 auto code=executable(MENU_UPDATE,sizeof(MENU_UPDATE));uintptr_t cells[40]{};auto fn=[&](size_t offset,uintptr_t value){size_t index=(offset-sizeof(MENU_UPDATE))/8;cells[index]=value;*(uintptr_t*)(code+offset)=(uintptr_t)&cells[index];};auto global=[&](size_t offset,void* value){*(uintptr_t*)(code+offset)=(uintptr_t)value;};
 for(size_t offset=0xef8;offset<=0xfe8;offset+=8)fn(offset,(uintptr_t)nothing);
 int32_t fill=-1,width=80,height=60,padding=2,spacing=1,icon_height=3,tab_width=12;
 global(0xf00,&fill);global(0xf18,&fill);global(0xf40,&width);global(0xf48,&height);global(0xf50,&padding);global(0xf58,&padding);global(0xf60,&spacing);global(0xf70,&icon_height);global(0xf80,&fill);global(0xf98,&tab_width);
 fn(0xef8,(uintptr_t)begin_state);fn(0xf10,(uintptr_t)cstr);fn(0xf20,(uintptr_t)vec_ctor);fn(0xf68,(uintptr_t)layout_fixture);fn(0xf78,(uintptr_t)vec_default);fn(0xf88,(uintptr_t)string_ctor);fn(0xf90,(uintptr_t)localize);fn(0xfa0,(uintptr_t)menus::vanilla_tab_hook);fn(0xfa8,(uintptr_t)menus::vanilla_enter_hook);fn(0xfb0,(uintptr_t)menus::update_end_hook);fn(0xfb8,(uintptr_t)menus::route_general);fn(0xfc0,(uintptr_t)menus::route_graphics);fn(0xfc8,(uintptr_t)menus::route_audio);fn(0xfd0,(uintptr_t)menus::route_controls);fn(0xfd8,(uintptr_t)menus::close_hook);seal(code);menus::update=(uintptr_t)code;menus::original_update=(menus::UpdateFn)code;
 alignas(8) unsigned char ui[0x900]{},data[0x100]{};alignas(8) uint8_t region[0x40]{};*(uint64_t*)(region+0x28)=0x12345678;for(size_t offset=0x200;offset<0x800;offset+=8)*(uintptr_t*)(ui+offset)=(uintptr_t)region;
 *(int32_t*)(ui+0x828)=11;double delta=0.016;
 auto frame=[&](std::string target="",int native=-1){click_widget=target;click_native=native;enter_event=!target.empty()||native>=0;native_calls=0;key_rows=0;menus::update_hook(ui,&delta,nullptr,data);assert(native_calls==4&&depth==0&&!menus::drawing&&platform::current_plugin==-1);};
 frame();assert(rendered_icons==6&&custom_tabs==2);
 // Missing keyboard uses the gear, including default NONE. Missing both never builds a null-icon tab.
 menus::update_data=data;*(uintptr_t*)(ui+0x4a0)=0;custom_tabs=0;enter_event=false;menus::header(ui);assert(custom_tabs==2);
 *(uintptr_t*)(ui+0x2b8)=0;custom_tabs=0;menus::header(ui);assert(custom_tabs==0);
 *(uintptr_t*)(ui+0x2b8)=*(uintptr_t*)(ui+0x4a0)=(uintptr_t)region;menus::update_data=nullptr;
 assert(!menus::selected&&vanilla_draws==1&&custom_draws==0&&owner_events[0][ANY_MENU_OPEN]==1&&owner_events[1][ANY_MENU_OPEN]==1);
 frame("anyapi.0.settings.tab");assert(menus::selected==menus::tabs[0]&&*(int32_t*)data==0&&custom_draws==1&&key_rows==256&&footer_calls==1);
 for(int i=0;i<4;++i){*(int32_t*)data=i;frame();assert(key_rows==256&&*(int32_t*)data==i);for(auto active:native_focus)assert(!active);} // every vanilla dispatch slot routes to the selected external tab
 dirty_flags[0]=true;default_flags[0]=false;menus::tabs[0]->scroll=37;frame("anyapi.1.settings.tab");assert(menus::selected==menus::tabs[1]&&owner_events[0][ANY_MENU_DEACTIVATE]==1&&dirty_flags[0]&&menus::tabs[0]->scroll==37);
 dirty_flags[1]=true;frame("anyapi.1.settings.apply");assert(owner_events[1][ANY_MENU_APPLY]==1&&!dirty_flags[1]&&dirty_flags[0]);default_flags[1]=false;frame("anyapi.1.settings.reset");assert(owner_events[1][ANY_MENU_RESET]==1&&dirty_flags[1]);
 frame("",2);assert(!menus::selected&&*(int32_t*)data==2&&owner_events[1][ANY_MENU_DEACTIVATE]==1&&vanilla_draws==2);
 frame("anyapi.0.settings.tab");menus::tabs[0]->section->faulted=true;frame();assert(!menus::selected&&vanilla_draws==3);menus::tabs[0]->section->faulted=false;
 ui[0x818]=1;frame();assert(!menus::session&&owner_events[0][ANY_MENU_CANCEL]==1&&owner_events[1][ANY_MENU_CANCEL]==1);ui[0x818]=0;frame();assert(menus::session&&owner_events[0][ANY_MENU_OPEN]==2);
 menus::ready=false;VirtualFree(code,0,MEM_RELEASE);VirtualFree(tab,0,MEM_RELEASE);VirtualFree(render,0,MEM_RELEASE);
 std::cout<<"PASS: exact native Settings update/tab/render execution with mandatory icons, NONE default and missing-icon fallback/suppression, fifth/sixth tabs, all four vanilla dispatch slots, untouched native enums, independent Apply/Reset/drafts/scroll, deactivate, fault fallback, close/reopen, 256 compact rows within scroll, footer in native parent\n";
}
