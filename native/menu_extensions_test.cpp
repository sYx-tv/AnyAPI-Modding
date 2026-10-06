#include "dinput8_proxy.cpp"
#include <cassert>
#include <iostream>
static int events[5]{},headings{},begins{},ends{},draws{},button_calls{},equal_forwards{},apply_forwards{},reset_forwards{},close_forwards{},filter_calls{},ordinary_calls{};
static bool dirty_flag=true,default_flag=false;
static bool hovered_flag=true,enter_flag=true;static int enter_queries{},long_rows{},container_depth{};static bool track_scroll{};
static void heading_fixture(void*,const GeoString16* id,const GeoString16*){std::string value((char*)id->data,id->length);assert(value.starts_with("anyapi."));++headings;}
static void table_fixture(void*,const GeoString16*,const int32_t* columns,const Vec2S32* size,const int32_t*){assert(*columns==1&&size->x==24&&size->y==1);++begins;if(track_scroll)++container_depth;}
static void end_fixture(void*){++ends;if(track_scroll){assert(container_depth>0);--container_depth;}}
static void button_fixture(uint8_t* out,void*,const GeoString16*,const GeoString16*,const Vec2S32*,const uintptr_t*,const bool*,const bool* hovered){assert(!*hovered);*out=hovered_flag;++button_calls;if(track_scroll){assert(container_depth>=2);++long_rows;}}
static void enter_fixture(uint8_t* out,void*){*out=enter_flag;++enter_queries;}
static void event_fixture(uint32_t e,void*){++events[e];}
static uint32_t dirty_fixture(void*){return dirty_flag;}
static uint32_t defaults_fixture(void*){return default_flag;}
static void draw_fixture(const AnyMenuFrameV1*,void*){assert(platform::current_plugin>=0);assert(menus::api.begin_table("rows",1,0));assert(menus::api.button("action","Fixture",0));++draws;} // deliberate imbalance must be recovered
static void long_draw(const AnyMenuFrameV1*,void*){assert(menus::api.begin_table("long_rows",1,0));for(int i=0;i<256;++i)menus::api.button(("action."+std::to_string(i)).c_str(),"Long list row",0);menus::api.end_table();}
static void equal_fixture(uint8_t* out,void*,void*){*out=1;++equal_forwards;}
static void apply_fixture(void*,void*,void*,const bool*,const bool*){++apply_forwards;}
static void reset_fixture(void*,void*,const bool*){++reset_forwards;}
static void close_fixture(void*,void*){++close_forwards;}
static uint32_t filter(const AnyInputV1*,void*){++filter_calls;return 1;}
static uint32_t ordinary(const AnyInputV1*,void*){++ordinary_calls;return 0;}
// Emit an actual x64 call whose return address matches the current native comparison site.
static void call_equal(uint8_t* out,size_t offset){auto memory=(uint8_t*)VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(memory);memset(memory,0x90,4096);memory[0]=0x48;memory[1]=0x83;memory[2]=0xec;memory[3]=0x28;
 memory[offset-12]=0x48;memory[offset-11]=0xb8;auto target=(uintptr_t)menus::equal_hook;memcpy(memory+offset-10,&target,8);memory[offset-2]=0xff;memory[offset-1]=0xd0;
 unsigned char epilogue[]={0x48,0x83,0xc4,0x28,0xc3};memcpy(memory+offset,epilogue,5);DWORD old{};assert(VirtualProtect(memory,4096,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),memory,4096);
 menus::builder=(uintptr_t)memory;((void(*)(uint8_t*,void*,void*))memory)(out,nullptr,nullptr);VirtualFree(memory,0,MEM_RELEASE);}
// Execute the exact native is_input_enter body against a controlled hovered-element virtual table.
static void activation_regression(){alignas(8) unsigned char ui[0xe0]{},element[8]{},klass[0x88]{};uintptr_t table[1]{},cell=(uintptr_t)enter_fixture;
 *(uintptr_t*)element=(uintptr_t)klass;*(uintptr_t*)(klass+0x80)=(uintptr_t)table;table[0]=(uintptr_t)&cell;*(uintptr_t*)(ui+0xd8)=(uintptr_t)element;
 auto code=(unsigned char*)VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(code);memcpy(code,MENU_ENTER,sizeof(MENU_ENTER));*(uintptr_t*)(code+sizeof(MENU_ENTER))=0;DWORD old{};assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),code,4096);
 menus::native_enter=(menus::EnterFn)code;menus::drawing=menus::sections.front().get();menus::native_ui=ui;enter_flag=false;hovered_flag=true;int queries=enter_queries;
 for(int i=0;i<300;++i)assert(!menus::api.button("held_hover","Hovered row",0));assert(enter_queries==queries+300); // hover alone never starts capture
 enter_flag=true;assert(menus::api.button("pressed","Pressed row",0));queries=enter_queries;assert(!menus::api.button("disabled","Disabled row",1));assert(enter_queries==queries);
 hovered_flag=false;assert(!menus::api.button("elsewhere","Not hovered",0));assert(enter_queries==queries);
 hovered_flag=true;*(uintptr_t*)(ui+0xd8)=0;assert(!menus::api.button("no_target","No hovered element",0));
 menus::drawing=nullptr;menus::native_ui=nullptr;menus::native_enter=enter_fixture;VirtualFree(code,0,MEM_RELEASE);}
// Rows must be inserted before the existing native scroll is popped, even with a full action list.
static void scroll_regression(){menus::sections.front()->callbacks.draw=long_draw;auto second=menus::sections.back();second->faulted=true;auto memory=(unsigned char*)VirtualAlloc(nullptr,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(memory);memset(memory,0x90,0x10000);
 memory[0]=0x48;memory[1]=0x83;memory[2]=0xec;memory[3]=0x28;size_t offset=0x7726;memory[offset-12]=0x48;memory[offset-11]=0xb8;auto target=(uintptr_t)menus::end_hook;memcpy(memory+offset-10,&target,8);memory[offset-2]=0xff;memory[offset-1]=0xd0;unsigned char epilogue[]={0x48,0x83,0xc4,0x28,0xc3};memcpy(memory+offset,epilogue,5);DWORD old{};assert(VirtualProtect(memory,0x10000,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),memory,0x10000);
 menus::builder=(uintptr_t)memory;track_scroll=true;container_depth=1;long_rows=0;enter_flag=false;((void(*)(void*))memory)((void*)1);assert(long_rows==256&&container_depth==0);track_scroll=false;second->faulted=false;VirtualFree(memory,0,MEM_RELEASE);}
int main(){assert(!services::api.query("bad/name",1));assert(!services::api.publish("fixture",1,&events));assert(!menus::api.add_section(nullptr));
 platform::plugin_count=2;for(int i=0;i<2;++i){platform::plugins[i].path=i?L"B.dll":L"A.dll";platform::plugins[i].callbacks.id=i?"b":"a";platform::plugins[i].callbacks.input=ordinary;}
 AnyMenuSectionV1 s;s.id="custom";s.title="CUSTOM";s.draw=draw_fixture;s.event=event_fixture;s.dirty=dirty_fixture;s.defaults=defaults_fixture;
 platform::current_plugin=0;assert(menus::api.add_section(&s));assert(!menus::api.add_section(&s));assert(!services::api.publish("anyapi.fake",1,&events));assert(services::api.publish("fixture",1,&events));assert(!services::api.publish("fixture",1,&events));assert(!services::api.query("fixture",2));assert(services::api.query("fixture",1)==&events);
 platform::current_plugin=1;assert(menus::api.add_section(&s));platform::current_plugin=-1;
 menus::native_heading=heading_fixture;menus::native_table=table_fixture;menus::native_button=button_fixture;menus::native_end=end_fixture;menus::native_enter=enter_fixture;menus::column_width=24;menus::row_height=1;menus::ready=true;
 menus::draw((void*)1);assert(draws==2&&headings==2&&begins==2&&ends==2&&button_calls==2);assert(!menus::drawing&&platform::current_plugin==-1);assert(!menus::api.begin_table("outside",1,0));
 menus::event(ANY_MENU_OPEN);assert(events[ANY_MENU_OPEN]==2);assert(menus::aggregate(false)&&!menus::aggregate(true));
 menus::original_equal=equal_fixture;menus::inside=true;uint8_t equal{};call_equal(&equal,0x68);assert(!equal&&equal_forwards==1);call_equal(&equal,0x12a);assert(!equal&&equal_forwards==2);dirty_flag=false;default_flag=true;call_equal(&equal,0x68);assert(equal);
 menus::original_apply=apply_fixture;menus::original_reset=reset_fixture;menus::original_close=close_fixture;bool yes=true,no=false;
 menus::apply_hook(nullptr,nullptr,nullptr,&yes,&no);assert(!events[ANY_MENU_APPLY]);menus::apply_hook(nullptr,nullptr,nullptr,&yes,&yes);assert(events[ANY_MENU_APPLY]==2&&apply_forwards==2);
 menus::reset_hook(nullptr,nullptr,&yes);assert(!events[ANY_MENU_RESET]);menus::reset_hook(nullptr,nullptr,&no);assert(events[ANY_MENU_RESET]==2&&reset_forwards==2);
 menus::session=true;menus::close_hook(nullptr,nullptr);menus::close_hook(nullptr,nullptr);assert(events[ANY_MENU_CANCEL]==2&&close_forwards==2);
 activation_regression();scroll_regression();
 platform::current_plugin=0;assert(services::api.input_filter(filter,nullptr,100));platform::current_plugin=-1;AnyInputV1 e;e.kind=ANY_KEY_DOWN;e.key='K';assert(platform::input(e));assert(filter_calls==1&&!ordinary_calls);
 e.kind=ANY_FOCUS_LOST;platform::input(e);assert(filter_calls==2&&ordinary_calls==2);platform::plugins[0].faulted=true;assert(!services::api.query("fixture",1));
 std::cout<<"PASS: multiple menu owners, ID namespaces, table recovery, native equality call sites, original forwarding, Apply/Reset/Cancel, optional service versions, filter ordering/focus, real native hover-vs-activation regression, 256 rows inside scroll-close boundary\n";}
