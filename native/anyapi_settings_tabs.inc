// Generic top-level settings tabs. Mod Controls policy lives in its own DLL.
struct Tab {std::shared_ptr<Section> section;uint32_t icon{};int32_t scroll{};};
static std::vector<std::shared_ptr<Tab>> tabs;static std::shared_ptr<Tab> selected;
static uintptr_t update{};static thread_local bool updating{};static thread_local void* update_data{};
using UpdateFn=void(*)(void*,const double*,void*,void*);
using TabFn=void(*)(uint8_t*,void*,const GeoString16*,const GeoString16*,const int32_t*,const uintptr_t*,const bool*);
using LabelFn=void(*)(void*,const GeoString16*,const GeoString16*,const bool*);
using LayoutFn=void(*)(void*,const GeoString16*,const Vec2S32*,const int32_t*,const int32_t*,const int32_t*,const Vec2S32*,const int32_t*);
using ScrollFn=void(*)(void*,const GeoString16*,const Vec2S32*,const int32_t*,const bool*,int32_t*);
static UpdateFn original_update{};static TabFn native_tab{};static LabelFn native_label{};
static LayoutFn native_layout{};static ScrollFn native_scroll{};static ControlsFooterFn native_footer{};
static ControlsBuilderFn vanilla_builders[4]{};
static int32_t fill_cells{},body_height{},content_spacing{},compact_height{},footer_height{},footer_width{},tab_width{},header_width{};
static void release_editing();
static std::vector<std::shared_ptr<Tab>> tab_snapshot(){std::lock_guard<std::mutex> lock(registry_mutex);return tabs;}
static void tab_event(Tab* t,uint32_t type){auto s=t->section.get();if(!enabled(s))return;int previous=platform::current_plugin;platform::current_plugin=s->owner;invoke_event(s,type);platform::current_plugin=previous;const char* names[]={"invalid","open","apply","reset","cancel","deactivate"};if(type>=1&&type<=5)log_line(ANY_LOG_INFO,"framework",("MENU_TAB_EVENT id="+s->id+" event="+names[type]).c_str());}
static bool tab_flag(Tab* t,bool defaults){auto s=t->section.get();if(!enabled(s))return defaults;int previous=platform::current_plugin;platform::current_plugin=s->owner;bool result=invoke_flag(s,defaults)!=0;platform::current_plugin=previous;return result;}
static void tabs_open(){selected.reset();for(auto& t:tab_snapshot()){t->scroll=0;tab_event(t.get(),ANY_MENU_OPEN);}}
static void tabs_close(){release_editing();for(auto& t:tab_snapshot())tab_event(t.get(),ANY_MENU_CANCEL);selected.reset();}
static void select_tab(std::shared_ptr<Tab> next){if(next==selected)return;release_editing();if(selected)tab_event(selected.get(),ANY_MENU_DEACTIVATE);selected=std::move(next);log_line(ANY_LOG_INFO,"framework",("MENU_TAB_SELECT id="+(selected?selected->section->id:std::string("vanilla"))).c_str());}
static bool add_tab(const AnyMenuTabV2* c){if(platform::current_plugin<0||!c||c->struct_size!=sizeof(*c)||!services::identifier(c->id)||!c->title||!*c->title||strlen(c->title)>31||!c->draw||c->icon>ANY_MENU_ICON_KEYBOARD)return false;
 auto t=std::make_shared<Tab>();auto s=std::make_shared<Section>();s->owner=platform::current_plugin;s->id=c->id;s->title=c->title;s->callbacks.location=ANY_MENU_SETTINGS_TAB;s->callbacks.order=c->order;s->callbacks.draw=c->draw;s->callbacks.event=c->event;s->callbacks.dirty=c->dirty;s->callbacks.defaults=c->defaults;s->callbacks.user=c->user;s->callbacks.id=s->id.c_str();s->callbacks.title=s->title.c_str();t->section=s;t->icon=c->icon;
 std::lock_guard<std::mutex> lock(registry_mutex);if(tabs.size()>=4||session)return false;for(auto& old:tabs)if(old->section->owner==s->owner&&old->section->id==s->id)return false;
 tabs.push_back(t);std::sort(tabs.begin(),tabs.end(),[](auto& a,auto& b){auto x=a->section,y=b->section;if(x->callbacks.order!=y->callbacks.order)return x->callbacks.order<y->callbacks.order;auto xp=platform::plugins[x->owner].path.filename().wstring(),yp=platform::plugins[y->owner].path.filename().wstring();return xp==yp?x->id<y->id:xp<yp;});
 log_line(ANY_LOG_INFO,"framework",("MENU_TAB_REGISTER id="+s->id+" title="+s->title).c_str());return true;}
static void label(const char* id,const char* text){if(!widget(id)||!text||strlen(text)>512)return;auto name=widget_id(id);auto a=make_const_geostring(name.c_str()),b=make_const_geostring(text);bool background=false;native_label(native_ui,&a,&b,&background);}
static bool sized_button(const char* id,const char* text,const Vec2S32& size,size_t icon_offset,bool disabled){auto name=widget_id(id);auto a=make_const_geostring(name.c_str()),b=make_const_geostring(text);uintptr_t icon{};if(icon_offset)safe_read_memory((uintptr_t)native_ui+icon_offset,&icon,8);bool focus=false;uint8_t hover{};native_button(&hover,native_ui,&a,&b,&size,&icon,&disabled,&focus);if(disabled||!hover)return false;uint8_t enter{};native_enter(&enter,native_ui);return enter!=0;}
static void layout(const char* id,Vec2S32 size,int32_t horizontal,int32_t vertical,int32_t mode,int32_t spacing_height){auto name=widget_id(id);auto a=make_const_geostring(name.c_str());Vec2S32 spacing{};native_layout(native_ui,&a,&size,&horizontal,&vertical,&mode,&spacing,&spacing_height);}
static uint32_t key_row(const char* id,const char* text,const char* key,uint32_t disabled){if(!widget(id)||!text||!key||strlen(text)>512||strlen(key)>128)return 0;
 auto base=std::string(id),name=widget_id(id);auto a=make_const_geostring(name.c_str());int32_t columns=2,spacing=0;Vec2S32 size{column_width,compact_height};native_table(native_ui,&a,&columns,&size,&spacing);
 auto label_name=widget_id((base+".label").c_str());auto label_id=make_const_geostring(label_name.c_str()),label_text=make_const_geostring(text);bool background=false;native_label(native_ui,&label_id,&label_text,&background);layout((base+".field").c_str(),{column_width,compact_height},0,0,0,0);
 // Native keyboard icon and trash atlas regions; no game-owned binding objects are borrowed.
 uint32_t result=sized_button((base+".key").c_str(),key,{column_width-2,compact_height},0x4a0,disabled!=0)?ANY_MENU_ROW_REBIND:0;
 if(sized_button((base+".clear").c_str(),"",{compact_height,compact_height},0x468,disabled!=0))result=ANY_MENU_ROW_CLEAR;
 native_end(native_ui);native_end(native_ui);return result;}
static const AnyMenuV2 api_v2{sizeof(AnyMenuV2),2,add_tab,available,settings_open,heading,label,key_row};
#include "anyapi_settings_widgets.inc"
static int32_t effective_tab_width(int32_t preferred){auto count=tab_snapshot().size();return count?std::min(preferred,std::max(1,(header_width-int32_t(count+3))/int32_t(count+4))):preferred;}
// Only the Settings update's private tab calls are redirected. Shared functions are untouched.
static void vanilla_tab_hook(uint8_t* out,void* ui,const GeoString16* id,const GeoString16* text,const int32_t* width,const uintptr_t* icon,const bool* focus){bool active=*focus&&(!ready||!selected);int32_t adjusted=ready?effective_tab_width(*width):*width;native_tab(out,ui,id,text,&adjusted,icon,&active);}
static void vanilla_enter_hook(uint8_t* out,void* ui){native_enter(out,ui);if(ready&&updating&&*out)select_tab(nullptr);}
static void header(void* ui){open_session(ui,update_data);for(auto& t:tab_snapshot()){if(!enabled(t->section.get()))continue;auto s=t->section;auto name="anyapi."+std::to_string(s->owner)+"."+s->id+".tab";auto a=make_const_geostring(name.c_str()),b=make_const_geostring(s->title.c_str());uintptr_t icon{};size_t offset=t->icon==ANY_MENU_ICON_KEYBOARD?0x4a0:0x2b8;safe_read_memory((uintptr_t)ui+offset,&icon,sizeof(icon));if(!icon&&offset!=0x2b8)safe_read_memory((uintptr_t)ui+0x2b8,&icon,sizeof(icon));if(!icon)continue; // Native tab.render unconditionally dereferences its atlas region.
 bool active=selected==t;int32_t width=effective_tab_width(tab_width);uint8_t hover{},enter{};native_tab(&hover,ui,&a,&b,&width,&icon,&active);if(hover){native_enter(&enter,ui);if(enter)select_tab(t);}}}
static void update_end_hook(void* ui){if(ready&&updating&&(uintptr_t)_ReturnAddress()==update+0xd01)header(ui);native_end(ui);}
static void custom_page(void* ui,void* data){auto t=selected;if(!t||!enabled(t->section.get())){select_tab(nullptr);return;}auto s=t->section;native_ui=ui;drawing=s.get();tables=0;int previous=platform::current_plugin;platform::current_plugin=s->owner;
 // Mirror the vanilla Controls body: fixed body bounds, native scroll, then footer.
 layout("body",{fill_cells,body_height},0,0,0,content_spacing);auto id=widget_id("scroll");auto scroll_id=make_const_geostring(id.c_str());Vec2S32 size{fill_cells,fill_cells};int32_t zero=0;bool shrink=false;native_scroll(ui,&scroll_id,&size,&zero,&shrink,&t->scroll);
 AnyMenuFrameV1 frame;frame.location=ANY_MENU_SETTINGS_TAB;frame.tick=GetTickCount64();invoke_draw(s.get(),&frame);while(tables)end_table();native_end(ui);native_end(ui);
 layout("footer",{fill_cells,footer_height},2,2,2,0);
 if(enabled(s.get())){
  bool defaults=tab_flag(t.get(),true);if(sized_button("reset","Reset to Default",{footer_width,footer_height},0x4e0,defaults))tab_event(t.get(),ANY_MENU_RESET);
  bool dirty=tab_flag(t.get(),false);if(sized_button("apply","Apply Changes",{footer_width,footer_height},0x460,!dirty))tab_event(t.get(),ANY_MENU_APPLY);
 }
 // Vanilla Back button handles the actual Settings close and shared navigation.
 bool display=false;auto empty=make_const_geostring("");native_footer(ui,data,&display,&empty);native_end(ui);
 drawing=nullptr;native_ui=nullptr;platform::current_plugin=previous;}
static void route(int index,void* ui,void* settings,void* data){if(ready){open_session(ui,data);if(selected){if(enabled(selected->section.get())){custom_page(ui,data);return;}select_tab(nullptr);}}if(index==3)build_hook(ui,settings,data);else vanilla_builders[index](ui,settings,data);}
static void route_general(void* ui,void* settings,void* data){route(0,ui,settings,data);}
static void route_graphics(void* ui,void* settings,void* data){route(1,ui,settings,data);}
static void route_audio(void* ui,void* settings,void* data){route(2,ui,settings,data);}
static void route_controls(void* ui,void* settings,void* data){route(3,ui,settings,data);}
static void update_hook(void* ui,const double* delta,void* settings,void* data){if(ready){frontend.store((uintptr_t)ui);if(settings_open())open_session(ui,data);else if(session.exchange(false)){event(ANY_MENU_CANCEL);tabs_close();}}
 bool previous=updating;auto prior_data=update_data;update_data=data;updating=true;original_update(ui,delta,settings,data);updating=previous;update_data=prior_data;}
