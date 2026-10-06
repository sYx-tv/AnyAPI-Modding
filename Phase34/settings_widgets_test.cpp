#include "dinput8_proxy.cpp"
#include <cassert>
#include <iostream>
static int depth{},captures{},releases{},constructs{},destroys{};static bool hover=true,enter{},back{},slider_event{};static double slider_fraction=.7;static std::string replacement;
static unsigned char element[256]{},klass[256]{};static uintptr_t vtable[16]{},event_cell;
static std::string str(const GeoString16* s){return {(const char*)s->data,s->length};}
static void noop(){}
static void push(uintptr_t* out,void*,const GeoString16*){memset(element,0,sizeof(element));*(uintptr_t*)element=(uintptr_t)klass;element[0x40]=hover;*(double*)(element+0x48)=slider_fraction;out[0]=0;out[1]=(uintptr_t)element;}
static void vec(Vec2S32* out,const int32_t* a,const int32_t* b){*out={*a,*b};}
static void copy_color(void* out,const void* in){memcpy(out,in,4);}
static void copy_ref(void* out,const void* in){memcpy(out,in,16);}
static void event(uint8_t* out,void*){*out=slider_event;}
static void round_step(double* out,const double* value,const double* step){*out=std::round(*value / *step) * *step;}
static void clamp_value(double* out,const double* value,const double* min,const double* max){*out=std::clamp(*value,*min,*max);}
static void format(GeoString16* out,const double*,const double*){*out=make_const_geostring("NUMBER");}
static void table(void*,const GeoString16*,const int32_t* columns,const Vec2S32* size,const int32_t*){assert(*columns==2&&size->x==24&&size->y==1);++depth;}
static void label(void*,const GeoString16*,const GeoString16*,const bool* bg){assert(!*bg&&depth==1);}
static void end(void*){assert(depth>0);--depth;}
static void enter_input(uint8_t* out,void*){*out=enter;}
static void back_input(uint8_t* out,void*){*out=back;}
static void capture(void*){++captures;}
static void release(void*){++releases;}
static void ctor(GeoString16* out,const char* value){++constructs;auto text=new char[strlen(value)+1];strcpy(text,value);*out=make_const_geostring(text);}
static void dtor(GeoString16* value){++destroys;delete[] (char*)value->data;*value={};}
static void(*results_ctor)(uint8_t*,const bool*,const bool*);
static void text_fixture(uint8_t* out,void*,const GeoString16*,GeoString16* value,const GeoString16*,const bool*,const GeoString16*,const bool*,const int32_t* limit){assert(*limit==48);bool changed=!replacement.empty();if(changed){dtor(value);ctor(value,replacement.c_str());}results_ctor(out,&changed,&hover);}
struct Code {unsigned char* p;uintptr_t cells[32]{};size_t count{};
 Code(const unsigned char* bytes,size_t size){p=(unsigned char*)VirtualAlloc(nullptr,65536,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(p);memcpy(p,bytes,size);}
 void fn(size_t offset,uintptr_t function){cells[count]=function;*(uintptr_t*)(p+offset)=(uintptr_t)&cells[count++];}
 void ptr(size_t offset,void* value){*(uintptr_t*)(p+offset)=(uintptr_t)value;}
 void seal(){DWORD old{};assert(VirtualProtect(p,65536,PAGE_EXECUTE_READ,&old));FlushInstructionCache(GetCurrentProcess(),p,65536);}
 ~Code(){VirtualFree(p,0,MEM_RELEASE);}
};
int main(){*(uint32_t*)(klass+4)=7;*(uintptr_t*)(klass+0x80)=(uintptr_t)vtable;event_cell=(uintptr_t)event;vtable[0]=(uintptr_t)&event_cell;
 int32_t height=1,fill=-1;uint32_t color=0;Code toggle(MENU_TOGGLE,sizeof(MENU_TOGGLE));toggle.fn(0x2c0,(uintptr_t)push);toggle.ptr(0x2c8,&height);toggle.fn(0x2d0,(uintptr_t)noop);toggle.fn(0x2d8,(uintptr_t)vec);toggle.ptr(0x2e0,&color);toggle.fn(0x2e8,(uintptr_t)copy_color);toggle.ptr(0x2f0,klass);toggle.fn(0x300,(uintptr_t)copy_ref);toggle.fn(0x308,(uintptr_t)noop);toggle.fn(0x310,(uintptr_t)noop);toggle.seal();
 Code slider(MENU_SLIDER_FLOAT,sizeof(MENU_SLIDER_FLOAT));slider.fn(0x498,(uintptr_t)push);slider.ptr(0x4a0,&fill);slider.ptr(0x4a8,&height);slider.fn(0x4b0,(uintptr_t)noop);slider.fn(0x4b8,(uintptr_t)vec);slider.ptr(0x4c0,klass);slider.fn(0x4d0,(uintptr_t)copy_ref);slider.fn(0x4d8,(uintptr_t)noop);slider.fn(0x4e0,(uintptr_t)noop);*(uintptr_t*)(slider.p+0x4e8)=0;slider.fn(0x4f0,(uintptr_t)round_step);slider.fn(0x4f8,(uintptr_t)clamp_value);slider.fn(0x500,(uintptr_t)format);slider.seal();
 Code result(MENU_TEXT_RESULTS,sizeof(MENU_TEXT_RESULTS));result.seal();results_ctor=(decltype(results_ctor))result.p;
 unsigned char ui[4096]{};menus::Section section;section.owner=0;section.id="fixture";menus::ready=true;menus::drawing=&section;menus::native_ui=ui;menus::frontend=(uintptr_t)ui;menus::column_width=24;menus::compact_height=1;menus::native_table=table;menus::native_label=label;menus::native_end=end;menus::native_toggle=(menus::ToggleFn)toggle.p;menus::native_float=(menus::FloatFn)slider.p;menus::native_enter=enter_input;menus::native_back=back_input;menus::native_capture=capture;menus::native_release=release;menus::native_string_ctor=ctor;menus::native_string_dtor=dtor;menus::native_text=text_fixture;
 uint32_t enabled=1;for(int i=0;i<600;++i)assert(!menus::api_v3.toggle_row("enabled","ENABLED",&enabled)&&enabled==1&&depth==0);enter=true;assert(menus::api_v3.toggle_row("enabled","ENABLED",&enabled)&&enabled==0);enter=false;
 double value=50;assert(!menus::api_v3.number_row("range","RANGE",&value,0,100,10,0)&&value==50);slider_event=true;assert(menus::api_v3.number_row("range","RANGE",&value,0,100,10,0)&&value==70);slider_event=false;enter=true;menus::api_v3.number_row("range","RANGE",&value,0,100,10,0);assert(captures==1);enter=false;back=true;menus::api_v3.number_row("range","RANGE",&value,0,100,10,0);assert(releases==1);back=false;
 char text[49]="Waypoint";assert(!menus::api_v3.text_row("text","TEXT",text,sizeof(text))&&constructs==destroys);replacement="Destination";assert(menus::api_v3.text_row("text","TEXT",text,sizeof(text))&&!strcmp(text,"Destination")&&constructs==destroys);replacement=std::string(100,'x');assert(!menus::api_v3.text_row("text","TEXT",text,sizeof(text))&&!strcmp(text,"Destination")&&constructs==destroys);replacement="\xc0\xaf";assert(!menus::api_v3.text_row("text","TEXT",text,sizeof(text))&&constructs==destroys);
 menus::tabs_close();assert(releases==2&&depth==0);menus::drawing=nullptr;assert(!menus::api_v3.toggle_row("enabled","ENABLED",&enabled));
 std::cout<<"PASS: exact native toggle and float slider bodies, hover versus activation, draft pointers, captured edit release, exact text-results ABI, game-string replacement/destruction and UTF-8 bounds\n";}
