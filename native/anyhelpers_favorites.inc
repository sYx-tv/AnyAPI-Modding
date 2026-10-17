// Favorites for the Duplicate tool's load (paste) screen, through the experimental SDK.
// A third native tab, FAVORITES, sits after LOCAL and WORKSHOP. It reuses the game's own Local list builder on the
// local saves, temporarily reordered so the starred ones come first and the list count cut to them; the order and
// count are put back as soon as the builder returns. Starred saves show "* " before their name in Local, and the
// Favorite key (Mod Controls, default F) stars or unstars the hovered save. Names are saved in favorites.tsv.
#include "anymaker_sdk_runtime.hpp"
#include "route_cell.h"
#include "anyhelpers_bindings.h"
#include <algorithm>
#include <atomic>
#include <set>
#include <thread>
namespace favorites {
using anymaker::gc_string_view;using anymaker::gc_vector_raw;
struct Vec2 {int32_t x,y;};
struct LocalFile {gc_string_view file_name;void* thumbnail;};   // client.ui_data.local_vehicle_file, 24 bytes
static_assert(sizeof(LocalFile)==24,"local_vehicle_file");
// client.ui_data+0x380 is duplicate_paste: +0x00 active tab (0 local, 1 workshop), +0x04 local scroll,
// +0x08 local hovered grid position, +0x38 local vehicle files.
constexpr size_t kPaste=0x380,kTab=0x00,kScroll=0x04,kHover=0x08,kLocalFiles=0x38;
using UpdateFn=void(*)(void*,const double*,void*,void*,void*,void*);
using BuildFn=void(*)(void*,void*,void*,void*,void*);
using DeleteFn=void(*)(void*,gc_vector_raw*,const int32_t*);
using TabFn=void(*)(uint8_t*,void*,const gc_string_view*,const gc_string_view*,const int32_t*,const uintptr_t*,const bool*);
using EntryFn=void(*)(void*,void*,const int32_t*,const gc_string_view*);
using SaveEntryFn=void(*)(uint8_t*,void*,const gc_string_view*,const Vec2*,const bool*);
using EnterFn=void(*)(uint8_t*,void*);
static anymaker::cell_hook h_update,h_build,h_delete,h_tab,h_entry,h_save_entry;
static EnterFn native_enter;
std::atomic<bool> toggle_pending{};static std::atomic<bool> ready{},stop{};static std::atomic<uint64_t> last_frame{};
static std::mutex mutex;static std::set<std::string> names;static std::filesystem::path file;
// Main thread only (the frontend UI runs there).
static bool active{};                         // FAVORITES is the selected tab
static void* paste_ui_data{};static int tab_calls{};static bool in_update{},in_build{},row_hovered{};
static std::string hovered_name;static std::vector<int32_t> view;  // view[k] = original index of row k in FAVORITES
static int32_t fav_scroll{};static Vec2 fav_hover{};static bool logged_rows{},logged_tabs{};
static gc_string_view text(const std::string& s){return gc_string_view{s.c_str(),int32_t(s.size()),uint32_t(s.size())};}
static std::string stem(std::string_view path){auto slash=path.find_last_of("/\\");if(slash!=std::string_view::npos)path=path.substr(slash+1);auto dot=path.find_last_of('.');if(dot!=std::string_view::npos&&dot>0)path=path.substr(0,dot);return std::string(path);}
static bool starred(const std::string& name){std::lock_guard<std::mutex> lock(mutex);return names.count(name)!=0;}
static void load(){std::ifstream in(file);std::string line;std::lock_guard<std::mutex> lock(mutex);while(std::getline(in,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(!line.empty()&&line.size()<=260&&names.size()<4096)names.insert(line);}}
// Same write-then-rename as bindings.tsv, so a crash never leaves half a file.
static bool save(){std::string data;{std::lock_guard<std::mutex> lock(mutex);for(auto& n:names)data+=n+"\n";}
 std::error_code ec;std::filesystem::create_directories(file.parent_path(),ec);auto temporary=file;temporary+=L".tmp";
 HANDLE h=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)return false;
 DWORD written{};bool ok=WriteFile(h,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(h);CloseHandle(h);
 if(ok)ok=MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;if(!ok)DeleteFileW(temporary.c_str());return ok;}
static void toggle(const std::string& name){bool now;{std::lock_guard<std::mutex> lock(mutex);now=names.insert(name).second;if(!now)names.erase(name);}
 bool ok=save();log("FAVORITE_"+std::string(now?"ADD":"REMOVE")+" name="+name+" saved="+std::to_string(ok),ok?0:2);}
static uint8_t* paste(){return paste_ui_data?(uint8_t*)paste_ui_data+kPaste:nullptr;}
static gc_vector_raw* local_files(){auto p=paste();return p?(gc_vector_raw*)(p+kLocalFiles):nullptr;}
// True while the load screen was drawn in the last half second; the input filter asks from its own thread.
static bool screen_open(){return ready&&GetTickCount64()-last_frame.load()<500;}

static void hk_update(void* self,const double* dt,void* fui,void* client,void* ui_data,void* scene){
 if(!ui_data){((UpdateFn)h_update.original)(self,dt,fui,client,ui_data,scene);return;}
 paste_ui_data=ui_data;tab_calls=0;in_update=true;last_frame=GetTickCount64();
 if(active&&*(int32_t*)(paste()+kTab)!=0)active=false;   // the game moved to another tab on its own
 ((UpdateFn)h_update.original)(self,dt,fui,client,ui_data,scene);in_update=false;}

// LOCAL and WORKSHOP are the first two tabs this screen draws; FAVORITES follows them at the same width.
static void hk_tab(uint8_t* out,void* ui,const gc_string_view* id,const gc_string_view* label,const int32_t* width,const uintptr_t* icon,const bool* focus){
 auto original=(TabFn)h_tab.original;
 if(!in_update||tab_calls>=2||!out||!ui||!width||!icon||!focus){original(out,ui,id,label,width,icon,focus);return;}
 int index=tab_calls++;int32_t narrow=std::max(1,*width*2/3);bool selected=*focus&&!(index==0&&active);
 original(out,ui,id,label,&narrow,icon,&selected);
 uint8_t enter{};if(*out&&native_enter){native_enter(&enter,ui);if(enter)active=false;}
 if(!logged_tabs&&label){logged_tabs=true;log("FAVORITES_TAB_SEEN index="+std::to_string(index)+" label="+std::string(label->view())+" width="+std::to_string(*width)+" drawn="+std::to_string(narrow));}
 if(index!=1)return;
 // Native tab.render dereferences its atlas region, so FAVORITES borrows the workshop tab's icon.
 std::string tab_label="FAVORITES";if(label)for(char c:label->view())if(c>='a'&&c<='z'){tab_label="Favorites";break;}
 static const std::string fav_id="anyhelpers.duplicate.favorites";auto a=text(fav_id),b=text(tab_label);uint8_t hover{};bool focus_fav=active;
 original(&hover,ui,&a,&b,&narrow,icon,&focus_fav);
 if(hover&&native_enter){enter=0;native_enter(&enter,ui);if(enter&&!active){active=true;*(int32_t*)(paste()+kTab)=0;log("FAVORITES_TAB_SELECT");}}}

// Ring-buffer slot i of the local list, valid for any i below capacity (gc_vector_raw::at stops at count).
static LocalFile* slot(gc_vector_raw* v,int32_t i){return (LocalFile*)(v->buffer+size_t((int64_t(v->offset)+i)%v->capacity)*v->element_size);}
static bool file_starred(const LocalFile* f){auto path=f->file_name.view();auto slash=path.find_last_of("/\\");auto base=std::string(slash==std::string_view::npos?path:path.substr(slash+1));return starred(stem(path))||starred(base);}
// FAVORITES runs the native Local builder over a reordered list: starred saves first, count cut to them. The rows
// are moved as raw bytes (a permutation, so nothing is copied or freed) and moved back right after.
static void hk_build(void* self,void* fui,void* client,void* ui_data,void* scene){
 auto original=(BuildFn)h_build.original;paste_ui_data=ui_data;auto v=local_files();hovered_name.clear();
 bool usable=v&&v->buffer&&v->capacity>0&&v->count>=0&&v->count<=v->capacity&&v->offset>=0&&v->offset<v->capacity&&v->element_size==sizeof(LocalFile);
 if(usable&&!logged_rows){logged_rows=true;std::string sample;for(int32_t i=0;i<std::min<int32_t>(v->count,3);++i)sample+=" file="+std::string(slot(v,i)->file_name.view());log("FAVORITES_LOCAL_FILES count="+std::to_string(v->count)+sample);}
 if(!active||!usable){in_build=true;original(self,fui,client,ui_data,scene);in_build=false;}
 else {
  int32_t total=v->count;view.clear();std::vector<int32_t> rest;
  for(int32_t i=0;i<total;++i)(file_starred(slot(v,i))?view:rest).push_back(i);
  int32_t shown=int32_t(view.size());auto original_index=[&](int32_t k){return k<shown?view[k]:rest[k-shown];};
  std::vector<LocalFile> moved(total);for(int32_t k=0;k<total;++k)moved[k]=*slot(v,original_index(k));
  for(int32_t k=0;k<total;++k)*slot(v,k)=moved[k];
  v->count=shown;
  auto p=paste();int32_t local_scroll=*(int32_t*)(p+kScroll);Vec2 local_hover=*(Vec2*)(p+kHover);*(int32_t*)(p+kScroll)=fav_scroll;*(Vec2*)(p+kHover)=fav_hover;
  in_build=true;original(self,fui,client,ui_data,scene);in_build=false;
  fav_scroll=*(int32_t*)(p+kScroll);fav_hover=*(Vec2*)(p+kHover);*(int32_t*)(p+kScroll)=local_scroll;*(Vec2*)(p+kHover)=local_hover;
  int32_t after=v->count;
  if(after==shown){for(int32_t k=0;k<total;++k)moved[k]=*slot(v,k);for(int32_t k=0;k<total;++k)*slot(v,original_index(k))=moved[k];v->count=total;}
  else if(after>=0&&after<shown){
   // The builder erased a row (a save that failed to load). Slide the hidden rows down behind what is left and
   // clear the freed tail slots; the game re-sorts the list by date the next time it loads it.
   int32_t hidden=total-shown;for(int32_t j=0;j<hidden;++j)*slot(v,after+j)=*slot(v,shown+j);
   for(int32_t k=after+hidden;k<total;++k)memset(slot(v,k),0,sizeof(LocalFile));
   v->count=after+hidden;view.clear();log("FAVORITES_ROW_ERASED shown="+std::to_string(shown)+" left="+std::to_string(after),1);}
  else {for(int32_t k=0;k<total;++k)moved[k]=*slot(v,k);for(int32_t k=0;k<total;++k)*slot(v,original_index(k))=moved[k];v->count=total;log("FAVORITES_UNEXPECTED_COUNT shown="+std::to_string(shown)+" after="+std::to_string(after),2);}
 }
 if(toggle_pending.exchange(false)&&!hovered_name.empty())toggle(hovered_name);}

static void hk_save_entry(uint8_t* out,void* ui,const gc_string_view* id,const Vec2* size,const bool* hovered){
 ((SaveEntryFn)h_save_entry.original)(out,ui,id,size,hovered);if(in_build)row_hovered=(out&&*out)||(hovered&&*hovered);}

// Each save row's name passes through here; starred ones get "* " in Local, and the hovered one is remembered.
static void hk_entry(void* self,void* fui,const int32_t* n,const gc_string_view* name){
 auto original=(EntryFn)h_entry.original;
 if(!in_build||!name){original(self,fui,n,name);return;}
 std::string plain(name->view());if(row_hovered)hovered_name=plain;row_hovered=false;
 static bool logged{};if(!logged){logged=true;log("FAVORITES_ROW_SEEN name="+plain+" n="+std::to_string(n?*n:-1));}
 if(active||!starred(plain)){original(self,fui,n,name);return;}
 std::string marked="* "+plain;auto shown=text(marked);original(self,fui,n,&shown);}

// The delete prompt passes the row index it was opened on; in FAVORITES that is a row of the reordered view.
static void hk_delete(void* self,gc_vector_raw* files,const int32_t* index){
 auto original=(DeleteFn)h_delete.original;
 if(active&&files&&files==local_files()&&index&&*index>=0&&*index<int32_t(view.size())){int32_t mapped=view[*index];log("FAVORITES_DELETE row="+std::to_string(*index)+" file_index="+std::to_string(mapped));original(self,files,&mapped);return;}
 original(self,files,index);}

static void remove(){h_save_entry.remove((void*)&hk_save_entry);h_entry.remove((void*)&hk_entry);h_delete.remove((void*)&hk_delete);h_build.remove((void*)&hk_build);h_tab.remove((void*)&hk_tab);h_update.remove((void*)&hk_update);}
static void install(){using namespace anymaker::experimental;
 for(int attempt=0;attempt<180&&!stop;++attempt){if(attempt)Sleep(1000);
  if(!matching_build()){if(!attempt)log("FAVORITES_OFF reason=game_build_mismatch",1);return;}
  void** cells[]={route::cell(anyhelpers::bind::paste_update),route::cell(anyhelpers::bind::ui_tab),route::cell(anyhelpers::bind::paste_build_local),
   route::cell(anyhelpers::bind::delete_vehicle_file),route::cell(anyhelpers::bind::vehicle_entry),route::cell(anyhelpers::bind::save_entry)};
  void* enter=route::function(anyhelpers::bind::is_input_enter);bool all=enter!=nullptr;for(auto c:cells)all=all&&c;
  if(!all){if(attempt==20||attempt==179){std::string found;for(auto c:cells)found+=c?"1":"0";log("FAVORITES_LOOKUP found="+found+" enter="+std::to_string(enter!=nullptr),attempt==179?2:1);}continue;}
  native_enter=(EnterFn)enter;
  bool ok=h_save_entry.install(cells[5],(void*)&hk_save_entry)&&h_entry.install(cells[4],(void*)&hk_entry)&&h_delete.install(cells[3],(void*)&hk_delete)&&
   h_build.install(cells[2],(void*)&hk_build)&&h_tab.install(cells[1],(void*)&hk_tab)&&h_update.install(cells[0],(void*)&hk_update);
  if(!ok){log("FAVORITES_OFF reason=hook_cell_not_writable",2);remove();return;}
  // Registered only once the screen can be hooked, so the key never shows in Mod Controls when the feature is off.
  ModControlActionV1 action;action.default_key='F';action.mod_id="anyhelpers";action.mod_name="AnyHelpers";action.action_id="duplicate.favorite";action.label="Favorite saved creation";
  favorite_action=register_action(&action);
  ready=true;log("FAVORITES_READY after_s="+std::to_string(attempt)+" key_token="+std::to_string(favorite_action));return;}}
static void init(){file=std::filesystem::path(host.plugin_directory)/L"AnyHelpers"/L"favorites.tsv";load();}
static void start(){std::thread(install).detach();}   // resolving scans game memory; keep it off the loader thread
static void shutdown(){stop=true;if(ready)remove();}
} // namespace favorites
