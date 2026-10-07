#include "anyapi_menu_patterns.h"
#include "anyapi_graphics_menu_patterns.h"
#include <intrin.h>
// Shared native integration. This file has no Mod Controls titles, actions or binding policy.
namespace menus {
struct Section {AnyMenuSectionV1 callbacks;std::string id,title;int owner;bool faulted{};};
static std::mutex registry_mutex;static std::vector<std::shared_ptr<Section>> sections;
static std::atomic<bool> ready{},session{};static std::atomic<uintptr_t> frontend{};
static uintptr_t builder{},options{};
static thread_local bool inside{};static thread_local Section* drawing{};static thread_local void* native_ui{};static thread_local int tables{};
static ControlsBuilderFn original_builder{};
using EqualFn=void(*)(uint8_t*,void*,void*);
using ApplyFn=void(*)(void*,void*,void*,const bool*,const bool*);
using ResetFn=void(*)(void*,void*,const bool*);
using CloseFn=void(*)(void*,void*);
static EqualFn original_equal{};static ApplyFn original_apply{};static ResetFn original_reset{};static CloseFn original_close{};
static ClientUiHeadingFn native_heading{};static ClientUiButtonFn native_button{};static P26TableFn native_table{};static P26EndContainerFn native_end{};
using EnterFn=void(*)(uint8_t*,void*);
static EnterFn native_enter{};
static int32_t column_width{},row_height{};
static void tabs_open();static void tabs_close();
static std::vector<std::shared_ptr<Section>> snapshot(){std::lock_guard<std::mutex> lock(registry_mutex);return sections;}
static bool enabled(Section* s){return !s->faulted&&s->owner>=0&&s->owner<int(platform::plugin_count.load())&&!platform::plugins[s->owner].faulted;}
// Isolate plugin failures and restore the callback owner for capture_input.
static void invoke_event(Section* s,uint32_t event){__try{if(s->callbacks.event)s->callbacks.event(event,s->callbacks.user);}__except(EXCEPTION_EXECUTE_HANDLER){s->faulted=true;anyapi_owner_fault(s->owner);log_line(ANY_LOG_ERROR,"framework","MENU_EVENT disabled_after_exception=1");}}
static uint32_t invoke_flag(Section* s,bool defaults){__try{auto fn=defaults?s->callbacks.defaults:s->callbacks.dirty;return fn?fn(s->callbacks.user):(defaults?1:0);}__except(EXCEPTION_EXECUTE_HANDLER){s->faulted=true;anyapi_owner_fault(s->owner);return defaults?1:0;}}
static void invoke_draw(Section* s,const AnyMenuFrameV1* frame){__try{s->callbacks.draw(frame,s->callbacks.user);}__except(EXCEPTION_EXECUTE_HANDLER){s->faulted=true;anyapi_owner_fault(s->owner);log_line(ANY_LOG_ERROR,"framework","MENU_SECTION disabled_after_exception=1");}}
static void event(uint32_t type,uint32_t location=0){for(auto& s:snapshot()){if((location&&s->callbacks.location!=location)||!enabled(s.get()))continue;int previous=platform::current_plugin;platform::current_plugin=s->owner;invoke_event(s.get(),type);platform::current_plugin=previous;}}
static bool aggregate(bool defaults,uint32_t location=ANY_MENU_SETTINGS_CONTROLS){bool result=defaults;for(auto& s:snapshot()){if(s->callbacks.location!=location||!enabled(s.get()))continue;int previous=platform::current_plugin;platform::current_plugin=s->owner;bool value=invoke_flag(s.get(),defaults)!=0;platform::current_plugin=previous;result=defaults?result&&value:result||value;}return result;}
static bool add(const AnyMenuSectionV1* c){if(platform::current_plugin<0||!c||c->struct_size!=sizeof(*c)||(c->location!=ANY_MENU_SETTINGS_CONTROLS&&c->location!=ANY_MENU_SETTINGS_GRAPHICS)||!services::identifier(c->id)||!c->title||!*c->title||strlen(c->title)>127||!c->draw)return false;
 auto s=std::make_shared<Section>();s->callbacks=*c;s->id=c->id;s->title=c->title;s->owner=platform::current_plugin;s->callbacks.id=s->id.c_str();s->callbacks.title=s->title.c_str();
 std::lock_guard<std::mutex> lock(registry_mutex);if(sections.size()>=32)return false;for(auto& existing:sections)if(existing->owner==s->owner&&existing->id==s->id)return false;
 sections.push_back(s);std::sort(sections.begin(),sections.end(),[](auto& a,auto& b){if(a->callbacks.order!=b->callbacks.order)return a->callbacks.order<b->callbacks.order;auto aid=platform::plugins[a->owner].path.filename().wstring(),bid=platform::plugins[b->owner].path.filename().wstring();return aid==bid?a->id<b->id:aid<bid;});
 log_line(ANY_LOG_INFO,"framework",("MENU_SECTION_REGISTER location=settings.controls id="+s->id+" title="+s->title).c_str());return true;}
static uint32_t available(){return ready.load();}
// Read the current native state rather than estimating visibility from frame timing.
static uint32_t settings_open(){uintptr_t ui=frontend.load();if(!ui)ui=g_p34_frontend;int state{};return ui&&safe_read_memory(ui+0x828,&state,4)&&state==MENU_OPTIONS_STATE;}
static std::string widget_id(const char* id){return "anyapi."+std::to_string(drawing->owner)+"."+drawing->id+"."+id;}
static bool widget(const char* id){return ready&&drawing&&native_ui&&services::identifier(id);}
static void heading(const char* id,const char* text){if(!widget(id)||!text||strlen(text)>512)return;auto name=widget_id(id);auto a=make_const_geostring(name.c_str()),b=make_const_geostring(text);native_heading(native_ui,&a,&b);}
static bool begin_table(const char* id,int32_t columns,int32_t width){if(!widget(id)||columns<1||columns>8||width<0||width>256||tables>=8)return false;auto name=widget_id(id);auto a=make_const_geostring(name.c_str());Vec2S32 size{width?width:column_width,row_height};int32_t spacing=0;native_table(native_ui,&a,&columns,&size,&spacing);++tables;return true;}
static void end_table(){if(!drawing||!native_ui||tables<=0)return;native_end(native_ui);--tables;}
static uint32_t button(const char* id,const char* text,uint32_t disabled){if(!widget(id)||!text||strlen(text)>512)return 0;auto name=widget_id(id);auto a=make_const_geostring(name.c_str()),b=make_const_geostring(text);Vec2S32 size{column_width,row_height};uintptr_t icon=0;bool off=disabled!=0,hovered=false;uint8_t clicked=0;native_button(&clicked,native_ui,&a,&b,&size,&icon,&off,&hovered);
 // The native result is hover/focus, not activation; vanilla UI queries the enter event separately.
 if(off||!clicked)return 0;uint8_t entered=0;native_enter(&entered,native_ui);return entered!=0;}
static const AnyMenuV1 api{sizeof(AnyMenuV1),1,add,available,settings_open,heading,begin_table,end_table,button};
static void draw(void* ui,uint32_t location=ANY_MENU_SETTINGS_CONTROLS){AnyMenuFrameV1 frame;frame.location=location;frame.tick=GetTickCount64();native_ui=ui;
 for(auto& s:snapshot()){if(s->callbacks.location!=location||!enabled(s.get()))continue;drawing=s.get();tables=0;int previous=platform::current_plugin;platform::current_plugin=s->owner;
  heading("section_heading",s->title.c_str());invoke_draw(s.get(),&frame);
  // Unbalanced tables in one mod never leak into another mod's native container stack.
  if(tables)log_line(ANY_LOG_WARN,"framework","MENU_SECTION unclosed_tables=RECOVERED");while(tables)end_table();drawing=nullptr;platform::current_plugin=previous;
 }native_ui=nullptr;}
// Only the exact third closing call after Flight Stick is the scroll-content extension slot.
static void end_hook(void* ui){auto caller=(uintptr_t)_ReturnAddress();if(ready&&inside&&caller==builder+0x7726)draw(ui);native_end(ui);}
// Preserve vanilla equality; include extension state only in the two validated comparisons.
static void equal_hook(uint8_t* out,void* a,void* b){auto caller=(uintptr_t)_ReturnAddress();original_equal(out,a,b);if(!ready||!inside)return;
 if(caller==builder+0x68&&aggregate(false))*out=0;if(caller==builder+0x12a&&!aggregate(true))*out=0;}
static void apply_hook(void* ui,void* settings,void* data,const bool* dirty,const bool* valid){original_apply(ui,settings,data,dirty,valid);if(ready&&inside&&*dirty&&*valid){event(ANY_MENU_APPLY,ANY_MENU_SETTINGS_CONTROLS);log_line(ANY_LOG_INFO,"framework","MENU_EVENT apply=1");}}
static void reset_hook(void* ui,void* data,const bool* defaults){original_reset(ui,data,defaults);if(ready&&inside&&!*defaults){event(ANY_MENU_RESET,ANY_MENU_SETTINGS_CONTROLS);log_line(ANY_LOG_INFO,"framework","MENU_EVENT reset=1");}}
static void close_hook(void* ui,void* data){if(ready&&session.exchange(false)){event(ANY_MENU_CANCEL);tabs_close();log_line(ANY_LOG_INFO,"framework","MENU_EVENT close_discard_pending=1");}original_close(ui,data);}
static void open_session(void* ui,void* data){frontend.store((uintptr_t)ui);if(!session||options!=(uintptr_t)data){if(session){event(ANY_MENU_CANCEL);tabs_close();}options=(uintptr_t)data;session=true;event(ANY_MENU_OPEN);tabs_open();log_line(ANY_LOG_INFO,"framework","MENU_EVENT open=1");}}
static void build_hook(void* ui,void* settings,void* data){if(ready)open_session(ui,data);
 bool previous=inside;inside=true;original_builder(ui,settings,data);inside=previous;}
static thread_local bool inside_graphics{};
#include "anyapi_settings_tabs.inc"
static uintptr_t graphics_builder{};
static EqualFn original_graphics_equal{};static ResetFn original_graphics_reset{};
using GraphicsApplyFn=void(*)(void*,void*,void*,const bool*);static GraphicsApplyFn original_graphics_apply{};
static void graphics_event(uint32_t type){for(auto& s:snapshot()){if(s->callbacks.location!=ANY_MENU_SETTINGS_GRAPHICS||!enabled(s.get()))continue;int prior=platform::current_plugin;platform::current_plugin=s->owner;invoke_event(s.get(),type);platform::current_plugin=prior;}}
static void graphics_end_hook(void* ui){if(ready&&inside_graphics&&(uintptr_t)_ReturnAddress()==graphics_builder+0x3c59)draw(ui,ANY_MENU_SETTINGS_GRAPHICS);native_end(ui);}
static void graphics_equal_hook(uint8_t* out,void* a,void* b){auto caller=(uintptr_t)_ReturnAddress();original_graphics_equal(out,a,b);if(!ready||!inside_graphics)return;if(caller==graphics_builder+0x92&&aggregate(false,ANY_MENU_SETTINGS_GRAPHICS))*out=0;if(caller==graphics_builder+0x123&&!aggregate(true,ANY_MENU_SETTINGS_GRAPHICS))*out=0;}
static void graphics_apply_hook(void* ui,void* settings,void* data,const bool* updated){original_graphics_apply(ui,settings,data,updated);if(ready&&inside_graphics&&*updated)graphics_event(ANY_MENU_APPLY);}
static void graphics_reset_hook(void* ui,void* data,const bool* defaults){original_graphics_reset(ui,data,defaults);if(ready&&inside_graphics&&!*defaults)graphics_event(ANY_MENU_RESET);}
struct Replacement {uintptr_t owner;size_t offset;uintptr_t previous,cell;};
static Replacement replacements[24];
static bool replace(Replacement& r){DWORD old{};auto slot=(void**)(r.owner+r.offset);if(!VirtualProtect(slot,8,PAGE_READWRITE,&old))return false;bool ok=InterlockedCompareExchangePointer(slot,&r.cell,(void*)r.previous)==(void*)r.previous;DWORD ignored{};VirtualProtect(slot,8,old,&ignored);return ok;}
static void restore(Replacement& r){DWORD old{};auto slot=(void**)(r.owner+r.offset);if(VirtualProtect(slot,8,PAGE_READWRITE,&old)){InterlockedCompareExchangePointer(slot,(void*)r.previous,&r.cell);DWORD ignored{};VirtualProtect(slot,8,old,&ignored);}}
static std::string last_diagnostic;
static bool diagnostic(const std::string& message){if(message!=last_diagnostic){last_diagnostic=message;log_line(ANY_LOG_WARN,"framework",("MENU_BRIDGE waiting="+message).c_str());}return false;}
template<size_t N> static bool contract(uintptr_t owner,size_t offset,const unsigned char(&body)[N],uintptr_t& fn){if(p34_cell(owner,offset,fn)&&p34_body(fn,body))return true;return diagnostic("dependency_contract offset="+std::to_string(offset));}
static bool dimension(uintptr_t owner,size_t offset,int32_t& out,int32_t low,int32_t high){uintptr_t pointer{};return safe_read_memory(owner+offset,&pointer,8)&&safe_read_memory(pointer,&out,4)&&out>=low&&out<=high;}
static bool prepare(){auto hits=scan_exact(MENU_BUILDER,sizeof(MENU_BUILDER));if(hits.size()!=1)return diagnostic("builder_matches_"+std::to_string(hits.size()));builder=hits[0];uintptr_t eq{},head{},table{},end{},button{},apply{},reset{},footer{},close{},enter{};
 if(!contract(builder,0x85c8,MENU_EQUAL,eq)||!contract(builder,0x8690,MENU_HEADING,head)||!contract(builder,0x8698,MENU_END,end)||!contract(builder,0x86a0,MENU_TABLE,table)||!contract(builder,0x86e8,MENU_BUTTON,button)||!contract(builder,0x86f0,MENU_ENTER,enter)||!contract(builder,0x86f8,MENU_RESET,reset)||!contract(builder,0x8700,MENU_APPLY,apply)||!contract(builder,0x8710,MENU_FOOTER,footer))return false;
 auto updates=scan_exact(MENU_UPDATE,sizeof(MENU_UPDATE));if(updates.size()!=1)return diagnostic("options_update_matches_"+std::to_string(updates.size()));uintptr_t close2{};
 if(!contract(footer,0x680,MENU_CLOSE,close)||!contract(updates[0],0xfd8,MENU_CLOSE,close2)||close!=close2)return false;
 update=updates[0];uintptr_t tab{},label_fn{},layout_fn{},scroll_fn{},digital{},general{},graphics{},audio{},update_enter{},update_end{},controls{};
 if(!contract(update,0xfa0,MENU_TAB,tab)||!contract(update,0xfa8,MENU_ENTER,update_enter)||!contract(update,0xfb0,MENU_END,update_end)||update_enter!=enter||update_end!=end||!contract(update,0xfb8,MENU_GENERAL,general)||!contract(update,0xfc0,MENU_GRAPHICS,graphics)||!contract(update,0xfc8,MENU_AUDIO,audio)||!contract(update,0xfd0,MENU_BUILDER,controls)||controls!=builder||!contract(builder,0x8638,MENU_LAYOUT,layout_fn)||!contract(builder,0x8650,MENU_SCROLL,scroll_fn)||!contract(builder,0x86d8,MENU_DIGITAL,digital)||!contract(digital,0x23b0,MENU_LABEL,label_fn))return false;
 int32_t screen_width{},padding{};
 if(!dimension(builder,0x8610,fill_cells,INT32_MIN,INT32_MAX)||!dimension(builder,0x8618,body_height,1,256)||!dimension(builder,0x8630,content_spacing,0,16)||!dimension(builder,0x8660,compact_height,1,16)||!dimension(builder,0x8658,footer_height,1,16)||!dimension(builder,0x86e0,footer_width,4,64)||!dimension(update,0xf98,tab_width,4,64)||!dimension(update,0xf40,screen_width,32,512)||!dimension(update,0xf50,padding,0,32))return diagnostic("native_tab_dimensions");header_width=screen_width-2*padding;
 native_tab=(TabFn)tab;native_label=(LabelFn)label_fn;native_layout=(LayoutFn)layout_fn;native_scroll=(ScrollFn)scroll_fn;native_footer=(ControlsFooterFn)footer;vanilla_builders[0]=(ControlsBuilderFn)general;vanilla_builders[1]=(ControlsBuilderFn)graphics;vanilla_builders[2]=(ControlsBuilderFn)audio;vanilla_builders[3]=(ControlsBuilderFn)builder;original_builder=(ControlsBuilderFn)builder;
 uintptr_t bool_owner{},float_owner{},int_owner{},toggle{},slider_float{},slider_int{},capture{},release{},back{},string_ctor{},string_dtor{},text_results{};
 if(!contract(builder,0x86a8,MENU_OPTIONS_BOOL,bool_owner)||!contract(builder,0x86b0,MENU_OPTIONS_FLOAT,float_owner)||!contract(audio,0x3058,MENU_OPTIONS_INT,int_owner)||!contract(bool_owner,0x570,MENU_TOGGLE,toggle)||!contract(float_owner,0x608,MENU_SLIDER_FLOAT,slider_float)||!contract(int_owner,0x5a0,MENU_SLIDER_INT,slider_int)||!contract(float_owner,0x620,MENU_CAPTURE,capture)||!contract(float_owner,0x630,MENU_RELEASE,release)||!contract(float_owner,0x628,MENU_BACK,back))return false;
 auto texts=scan_exact(MENU_INPUT_TEXT,sizeof(MENU_INPUT_TEXT));if(texts.size()!=1)return diagnostic("input_text_matches_"+std::to_string(texts.size()));if(!contract(texts[0],0x568,MENU_TEXT_RESULTS,text_results)||!p34_cell(builder,0x8608,string_ctor)||!p34_cell(builder,0x8648,string_dtor))return diagnostic("native_text_compiler_helpers");
 native_toggle=(ToggleFn)toggle;native_float=(FloatFn)slider_float;native_int=(IntFn)slider_int;native_text=(TextFn)texts[0];native_capture=(UiFn)capture;native_release=(UiFn)release;native_back=(EnterFn)back;native_string_ctor=(StringCtorFn)string_ctor;native_string_dtor=(StringDtorFn)string_dtor;
 uintptr_t width{},height{};if(!safe_read_memory(builder+0x8670,&width,8)||!safe_read_memory(width,&column_width,4)||column_width<8||column_width>128||!safe_read_memory(builder+0x8658,&height,8)||!safe_read_memory(height,&row_height,4)||row_height<1||row_height>16)return diagnostic("native_dimensions");
 native_enter=(EnterFn)enter;original_equal=(EqualFn)eq;native_heading=(ClientUiHeadingFn)head;native_table=(P26TableFn)table;native_end=(P26EndContainerFn)end;native_button=(ClientUiButtonFn)button;original_apply=(ApplyFn)apply;original_reset=(ResetFn)reset;original_close=(CloseFn)close;
 uintptr_t geq{},greset{},gapply{},gend{};if(!contract(graphics,0x4678,MENU_GRAPHICS_EQUAL,geq)||!contract(graphics,0x4730,MENU_END,gend)||gend!=end||!contract(graphics,0x4810,MENU_GRAPHICS_RESET,greset)||!contract(graphics,0x4818,MENU_GRAPHICS_APPLY,gapply))return false;graphics_builder=graphics;original_graphics_equal=(EqualFn)geq;original_graphics_reset=(ResetFn)greset;original_graphics_apply=(GraphicsApplyFn)gapply;
 struct Spec {uintptr_t owner;size_t offset;uintptr_t fn;};Spec specs[]={{builder,0x85c8,(uintptr_t)equal_hook},{builder,0x8698,(uintptr_t)end_hook},{builder,0x86f8,(uintptr_t)reset_hook},{builder,0x8700,(uintptr_t)apply_hook},{footer,0x680,(uintptr_t)close_hook},{update,0xfd8,(uintptr_t)close_hook},{update,0xfa0,(uintptr_t)vanilla_tab_hook},{update,0xfa8,(uintptr_t)vanilla_enter_hook},{update,0xfb0,(uintptr_t)update_end_hook},{update,0xfb8,(uintptr_t)route_general},{update,0xfc0,(uintptr_t)route_graphics},{update,0xfc8,(uintptr_t)route_audio},{update,0xfd0,(uintptr_t)route_controls},{graphics,0x4678,(uintptr_t)graphics_equal_hook},{graphics,0x4730,(uintptr_t)graphics_end_hook},{graphics,0x4810,(uintptr_t)graphics_reset_hook},{graphics,0x4818,(uintptr_t)graphics_apply_hook}};
 size_t count=0;for(auto& s:specs){auto& r=replacements[count];r.owner=s.owner;r.offset=s.offset;r.cell=s.fn;if(!safe_read_memory(s.owner+s.offset,&r.previous,8)||!replace(r)){while(count)restore(replacements[--count]);return false;}++count;}
 if(!install_detour("settings_update_current",update,BODY_SETTINGS_UPDATE_PREFIX,19,(uintptr_t)update_hook,(void**)&original_update)){while(count)restore(replacements[--count]);return false;}
 ready=true;log_line(ANY_LOG_INFO,"framework","MENU_BRIDGE ready=1 locations=settings.tabs,settings.controls native_widgets=tab,heading,label,key_row,toggle,number,choice,text,table,button,scroll lifecycle=open,apply,reset,cancel,deactivate shared_cells_modified=0 native_tab_enum_modified=0");return true;}
// Hook and cell transactions run on a worker, never on a plugin or game callback stack.
static DWORD WINAPI worker(void*){if(snapshot().empty()&&tab_snapshot().empty())return 0;log_line(ANY_LOG_INFO,"framework","MENU_BRIDGE resolving_current_contracts=1");while(!ready){if(prepare())break;Sleep(5000);}return 0;}
}
static const AnyMenuV1* anyapi_menu_service(){return &menus::api;}
static const AnyMenuV2* anyapi_menu_service_v2(){return &menus::api_v2;}
static const AnyMenuV3* anyapi_menu_service_v3(){return &menus::api_v3;}
