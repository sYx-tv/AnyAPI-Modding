#include "anyapi_inventory_ui_v1.h"
#include "anyapi_inventory_ui_v3.h"
#include "anyapi_storage_patterns.h"
namespace inventory_ui {
// Native vector: flags at +8, size at +12, reserved capacity at +16.
// Reading +8 as the size silently hides every populated native container.
struct NativeVector {uintptr_t data{};int32_t unknown{},count{},capacity{},stride{};uintptr_t ctor{},dtor{};};
struct Descriptor {int32_t type{};uint32_t pad{};uintptr_t item{};int32_t parent{},peer{};uintptr_t actor{};int32_t floor{};uint32_t pad2{};uintptr_t component{};int32_t vehicle{},part{};uintptr_t icon{};GeoString16 name{};int32_t flags{};uint32_t pad3{};};
struct NativeGrid {int32_t index{},width{},height{},origin_x{},origin_y{};uint32_t fixed{};GeoString16 filter{};int32_t flags{};uint32_t rotate{};};
struct NativeStored {uintptr_t item{};int32_t grid{},x{},y{},rotation{};uint64_t flags{};};
struct NativeId {int32_t type{},peer{},vehicle{},floor{},component{},item{};};
static_assert(sizeof(NativeVector)==40&&sizeof(Descriptor)==88&&sizeof(NativeGrid)==48&&sizeof(NativeStored)==32&&sizeof(NativeId)==24);
static_assert(offsetof(NativeVector,count)==12&&offsetof(NativeVector,capacity)==16&&offsetof(NativeVector,stride)==20);
using ContainerFn=void(*)(void*,void*,void*,void*,void*,const int32_t*,const Descriptor*,void*,const bool*,const int32_t*,void*,void*,void*);
using TitleFn=void(*)(uint8_t*,void*,const GeoString16*,const GeoString16*,const uintptr_t*,const Vec2S32*);
using CollectionFn=void(*)(void*,const Descriptor*,NativeVector*);using CtorFn=void(*)(NativeVector*);using DtorFn=void(*)(NativeVector*);
using InFn=void(*)(void*,const NativeId*);using OutFn=void(*)(void*,const NativeId*,const NativeVector*);
using StackFn=void(*)(void*,const NativeId*,const NativeId*);using MoveFn=void(*)(void*,const NativeId*,const int32_t*,const double*,const double*);
using CompatibleFn=void(*)(uint8_t*,void*,const NativeGrid*,void*,const int32_t*);
struct Item {AnyStoredItemV1 copy;NativeId id;uintptr_t object{},definition{},variant{};Descriptor parent;};
struct Grid {AnyStorageGridV1 copy;Descriptor parent;NativeGrid native;char filter[128]{};};
struct Section {std::string id;int owner;void(*draw)(const AnyInventoryUiFrameV1*,void*);void* user;bool(*visible)(const AnyItemDefinitionV1*,void*){};uint32_t reserved_rows{};};
using ColumnFn=void(*)(void*,void*,void*,void*,const int32_t*,const bool*,const NativeVector*,const int32_t*,void*,const int32_t*,void*,void*,void*,int32_t*,void*);
using ScreenFn=void(*)(void*,const GeoString16*,const Vec2S32*,const int32_t*,const int32_t*,const int32_t*,const int32_t*,const int32_t*,const bool*);
using TipFn=void(*)(void*,const GeoString16*,const uintptr_t*,const GeoString16*,const GeoString16*,const uint32_t*);
using TagFn=void(*)(uint8_t*,void*,const GeoString16*,const GeoString16*,const Vec2S32*,const uint32_t*,const bool*);
static TagFn native_tag;
static void(*tip_position)(void*,const GeoString16*,const Vec2S32*,const Vec2S32*);
static void(*tip_background)(void*,const GeoString16*,const bool*,const uint32_t*);
static void(*tip_text)(void*,const GeoString16*,const GeoString16*,const Vec2S32*,const int32_t*,const uint32_t*);
static int32_t screen_columns{},screen_rows{};
// Native screen grid coordinates start at the bottom left. Cells are square;
// the fixed inventory width is a content width, not the viewport's width.
static bool cursor_grid(POINT pt,RECT rect,Vec2S32* p,Vec2S32* viewport){
 if(!p||!viewport||screen_rows<=0||rect.right<=0||rect.bottom<=0||pt.x<0||pt.y<0||pt.x>=rect.right||pt.y>=rect.bottom)return false;
 viewport->x=int((int64_t(rect.right)*screen_rows)/rect.bottom);viewport->y=screen_rows;
 p->x=int(int64_t(pt.x)*screen_rows/rect.bottom);p->y=int(int64_t(rect.bottom-1-pt.y)*screen_rows/rect.bottom);return true;
}
static bool mouse_cells(Vec2S32* p,Vec2S32* viewport){POINT pt{};RECT rect{};auto window=platform::game_window;if(!window||GetForegroundWindow()!=window||!GetCursorPos(&pt)||!ScreenToClient(window,&pt)||!GetClientRect(window,&rect))return false;return cursor_grid(pt,rect,p,viewport);}
static bool(*tooltip_cursor)(Vec2S32*,Vec2S32*)=mouse_cells;
static ScreenFn tip_screen;static TipFn tip_entry;static void(*tip_end)(void*);
static thread_local std::string hover_tip;
static ColumnFn original_column;
static thread_local const std::vector<std::pair<const Descriptor*,int32_t>>* original_heights;
struct Context {void* inventory{},*client{},*data{},*scene{},*ui{};Descriptor storage;AnyInventoryUiFrameV1 frame;std::vector<Item> items;std::vector<Grid> grids;};
static std::mutex registry_mutex;static std::vector<Section> sections;static std::atomic<bool> ready{},faulted{};
static ContainerFn original_container;static TitleFn original_title;static CollectionFn get_items,get_grids;static CtorFn items_ctor,grids_ctor;static DtorFn items_dtor,grids_dtor;
static InFn quick_in;static OutFn quick_out;static StackFn stack_event;static MoveFn move_event;static CompatibleFn compatible;
static uintptr_t quantity_slot=UINTPTR_MAX,capacity_slot=UINTPTR_MAX,merge_slot=UINTPTR_MAX,id_slot=UINTPTR_MAX,drop_slot=UINTPTR_MAX,column_owner{},container_owner{};
static thread_local Context* active;static thread_local Section* drawing;static thread_local int rows{},row_widths[4]{};
static uint64_t epoch=1;static uintptr_t last_scene{};static uint64_t last_event_frame=UINT64_MAX;
// Report each native integration failure once, without per-frame log spam.
static bool rejected(const std::string& reason){static std::mutex lock;static std::vector<std::string> reported;std::lock_guard guard(lock);if(std::find(reported.begin(),reported.end(),reason)==reported.end()&&reported.size()<32){reported.push_back(reason);log_line(ANY_LOG_WARN,"framework",("INVENTORY_UI_DIAGNOSTIC "+reason).c_str());}return false;}
static bool rejected_collections(const NativeVector& a,const NativeVector& b){return rejected("collections invalid_or_exception="+std::to_string(faulted.load())+" items="+std::to_string(a.count)+" item_stride="+std::to_string(a.stride)+" grids="+std::to_string(b.count)+" grid_stride="+std::to_string(b.stride));}
static uint64_t hash(const void* bytes,size_t count){uint64_t value=1469598103934665603ULL;auto p=(const uint8_t*)bytes;for(size_t i=0;i<count;++i)value=(value^p[i])*1099511628211ULL;return value?value:1;}
static uint64_t item_token(int32_t id){uint64_t value=uint32_t(id)|(epoch<<32);return hash(&value,sizeof(value));}
static NativeId parent_id(const Descriptor& d,int32_t id){return {d.type,d.peer,d.vehicle,d.floor,d.part,id};}
static uint64_t grid_token(const Descriptor& d,int32_t index){int32_t key[]={d.type,d.peer,d.vehicle,d.floor,d.part,d.parent,index};return hash(key,sizeof(key))^(epoch<<32);}
static bool descriptor(uintptr_t address,Descriptor& d){return safe_read_memory(address,&d,sizeof(d))&&d.type>=0&&d.type<16;}
static bool vector_view(uintptr_t address,NativeVector& out,int stride){return safe_read_memory(address,&out,sizeof(out))&&out.count>=0&&out.count<=1024&&out.capacity>=out.count&&out.stride==stride&&(!out.count||out.data);}
// POD-only exception leaves around reviewed game calls; collection lifetime remains native.
static bool collections(void* inventory,const Descriptor* d,NativeStored* items,int* item_count,NativeGrid* grids,char filters[][128],int* grid_count){
 NativeVector a{},b{};bool ca=false,cb=false,ok=false;
 __try {
  items_ctor(&a);ca=true;grids_ctor(&b);cb=true;get_items(inventory,d,&a);get_grids(inventory,d,&b);
  if(a.stride==32&&b.stride==48&&a.count>=0&&a.count<=1024&&b.count>=0&&b.count<=64&&a.capacity>=a.count&&b.capacity>=b.count&&(!a.count||a.data)&&(!b.count||b.data)){
   ok=(!a.count||safe_read_memory(a.data,items,size_t(a.count)*32))&&(!b.count||safe_read_memory(b.data,grids,size_t(b.count)*48));
   if(ok){for(int i=0;i<b.count;++i)if(!p34_copy_string(b.data+i*48+24,filters[i],128)){ok=false;break;}*item_count=a.count;*grid_count=b.count;}
  }
 }__except(EXCEPTION_EXECUTE_HANDLER){faulted=true;}
 __try {if(cb)grids_dtor(&b);if(ca)items_dtor(&a);}__except(EXCEPTION_EXECUTE_HANDLER){faulted=true;ok=false;}
 if(!ok||faulted)return rejected_collections(a,b);return true;
}
static bool numbers(uintptr_t object,int32_t* id,int32_t* quantity,int32_t* capacity){__try {
 uintptr_t fn{},value{};
 if(!p34_virtual(object+8,id_slot,fn))return false;((void(*)(uintptr_t*,void*))fn)(&value,(void*)(object+8));if(!safe_read_memory(value,id,4)||*id<=0)return false;
 if(!p34_virtual(object,quantity_slot,fn)||!st_verified_quantity(fn))return false;((void(*)(int32_t*,void*))fn)(quantity,(void*)object);
 if(!p34_virtual(object,capacity_slot,fn)||!st_verified_capacity(fn))return false;((void(*)(int32_t*,void*))fn)(capacity,(void*)object);
 return *quantity>=0&&*quantity<=1000000&&*capacity>=0&&*capacity<=1000000;
 }__except(EXCEPTION_EXECUTE_HANDLER){faulted=true;return false;}}
static bool collect(Context& c,const Descriptor& d,uint32_t side){
 std::vector<NativeStored> stored(1024);std::vector<NativeGrid> grids(64);char filters[64][128]{};int count=0,grid_count=0;
 if(!collections(c.inventory,&d,stored.data(),&count,grids.data(),filters,&grid_count))return false;
 for(int i=0;i<grid_count;++i){auto& n=grids[i];if(n.width<=0||n.height<=0||n.width>128||n.height>128||n.index<0||n.index>1024)return rejected("grid_dimensions side="+std::to_string(side));Grid g;g.parent=d;g.native=n;g.native.filter={};strcpy_s(g.filter,filters[i]);g.copy.token=grid_token(d,n.index);g.copy.side=side;g.copy.width=n.width;g.copy.height=n.height;g.copy.fixed_slots=uint8_t(n.fixed);g.copy.allow_rotation=uint8_t(n.rotate);if(std::none_of(c.grids.begin(),c.grids.end(),[&](auto& old){return old.copy.token==g.copy.token;}))c.grids.push_back(g);}
 for(int i=0;i<count;++i){auto& n=stored[i];if(!n.item)continue;Item a;int32_t id{};if(!numbers(n.item,&id,&a.copy.quantity,&a.copy.capacity))return rejected("item_numbers side="+std::to_string(side)+" faulted="+std::to_string(faulted.load()));if(!safe_read_memory(n.item+0x28,&a.definition,8)||!a.definition)return rejected("item_definition side="+std::to_string(side));
  a.object=n.item;a.parent=d;a.id=parent_id(d,id);a.copy.token=item_token(id);a.copy.grid=grid_token(d,n.grid);a.copy.side=side;a.copy.x=n.x;a.copy.y=n.y;a.copy.rotation=n.rotation;
  if(n.rotation<0||n.rotation>3||n.x<0||n.y<0||!p34_copy_string(a.definition+0x10,a.copy.definition,sizeof(a.copy.definition)))return rejected("item_coordinates_or_name side="+std::to_string(side)+" rotation="+std::to_string(n.rotation)+" x="+std::to_string(n.x)+" y="+std::to_string(n.y));
  const itemcatalog::Entry* entry=nullptr;for(auto& e:catalog_runtime::entries)if(!strcmp(e.metadata.id,a.copy.definition)||!strcmp(e.metadata.name,a.copy.definition)){entry=&e;break;}
  if(entry){strncpy_s(a.copy.name,entry->metadata.name,_TRUNCATE);strncpy_s(a.copy.definition,entry->metadata.id,_TRUNCATE);}else{strncpy_s(a.copy.name,a.copy.definition,_TRUNCATE);}
  // The reviewed compatibility helper reads inventory_definition.inventory.size at +0x170.
  int32_t size[2]{};if(!safe_read_memory(a.definition+0x170,size,sizeof(size))||size[0]<=0||size[1]<=0||size[0]>128||size[1]>128)return rejected("item_dimensions side="+std::to_string(side)+" width="+std::to_string(size[0])+" height="+std::to_string(size[1]));a.copy.width=size[0];a.copy.height=size[1];
  // Native add-component compatibility compares its canonical component definition at +0x58.
  uintptr_t merge_fn{};bool component=p34_virtual(n.item,merge_slot,merge_fn)&&(p34_body(merge_fn,ST_MERGE_8461)||p34_body(merge_fn,ST_MERGE_8383));
  if(component){if(!safe_read_memory(n.item+0x58,&a.variant,8)||!a.variant)return false;char part_id[112]{},part_name[128]{};if(!p34_copy_string(a.variant,part_id,sizeof(part_id))||!p34_copy_string(a.variant+0x10,part_name,sizeof(part_name)))return false;std::string key="component:"+std::string(part_id);strncpy_s(a.copy.definition,key.c_str(),_TRUNCATE);strncpy_s(a.copy.name,part_name,_TRUNCATE);for(auto& e:catalog_runtime::entries)if(!strcmp(e.metadata.id,key.c_str())){strncpy_s(a.copy.name,e.metadata.name,_TRUNCATE);break;}}
  if(std::any_of(c.items.begin(),c.items.end(),[&](auto& old){return old.copy.token==a.copy.token&&old.object!=a.object;}))return false;
  if(std::none_of(c.items.begin(),c.items.end(),[&](auto& old){return old.copy.token==a.copy.token;}))c.items.push_back(a);
 }
 return true;
}
static bool snapshot(Context& c){
 if(last_scene!=(uintptr_t)c.scene){last_scene=(uintptr_t)c.scene;++epoch;}
 auto id=parent_id(c.storage,c.storage.parent);c.frame.context=hash(&id,sizeof(id))^(epoch<<32)^(ui_state_runtime::generation.load()*0x9e3779b97f4a7c15ULL);c.frame.tick=GetTickCount64();p34_copy_string((uintptr_t)&c.storage+0x40,c.frame.storage_name,128);
 if(!collect(c,c.storage,ANY_INVENTORY_STORAGE)||c.grids.empty())return rejected("storage_snapshot grids="+std::to_string(c.grids.size()));
 for(size_t offset:{size_t(0x20),size_t(0x48)}){NativeVector view;if(!vector_view((uintptr_t)c.data+offset,view,88))return rejected("player_vector offset="+std::to_string(offset));for(int i=0;i<view.count;++i){Descriptor d;if(!descriptor(view.data+i*88,d)||!collect(c,d,ANY_INVENTORY_PLAYER))return rejected("player_collection offset="+std::to_string(offset));}}
 // Transfer carried bags as a whole; their children are not separate bulk-transfer candidates.
 std::vector<int32_t> carried;for(auto& a:c.items)if(a.copy.side==ANY_INVENTORY_PLAYER)carried.push_back(a.id.item);
 std::erase_if(c.items,[&](auto& a){return a.copy.side==ANY_INVENTORY_PLAYER&&std::find(carried.begin(),carried.end(),a.parent.parent)!=carried.end();});
 std::erase_if(c.grids,[&](auto& a){return a.copy.side==ANY_INVENTORY_PLAYER&&std::find(carried.begin(),carried.end(),a.parent.parent)!=carried.end();});
 for(auto& a:c.items)(a.copy.side==ANY_INVENTORY_PLAYER?c.frame.player_items:c.frame.storage_items)++;
 for(auto& a:c.grids)(a.copy.side==ANY_INVENTORY_PLAYER?c.frame.player_grids:c.frame.storage_grids)++;
 return true;
}
static bool allowed(){AnyUiSnapshotV1 state;return ready&&!faulted&&active&&drawing&&anyapi_owner_active(drawing->owner)&&ui_state_runtime::copy(&state)&&state.kind==ANY_UI_INVENTORY;}
static uint32_t available(){return ready&&!faulted;}
static bool add(const char* id,void(*fn)(const AnyInventoryUiFrameV1*,void*),void* user){if(platform::current_plugin<0||!services::identifier(id)||!fn)return false;std::lock_guard lock(registry_mutex);if(sections.size()>=32)return false;for(auto& s:sections)if(s.owner==platform::current_plugin&&s.id==id)return false;sections.push_back({id,platform::current_plugin,fn,user});log_line(ANY_LOG_INFO,"framework",("INVENTORY_SECTION_REGISTER id="+std::string(id)).c_str());return true;}
static bool add_v2(const char* id,bool(*visible)(const AnyItemDefinitionV1*,void*),void(*fn)(const AnyInventoryUiFrameV1*,void*),void* user,uint32_t reserve){if(!visible||reserve<1||reserve>16||!add(id,fn,user))return false;std::lock_guard lock(registry_mutex);auto& s=sections.back();s.visible=visible;s.reserved_rows=reserve;return true;}
static AnyItemDefinitionV1 definition(const Descriptor& d){AnyItemDefinitionV1 result;uintptr_t authored{};char name[192]{};if(d.item&&safe_read_memory(d.item+0x28,&authored,8)&&authored)p34_copy_string(authored+0x10,name,sizeof(name));for(auto& e:catalog_runtime::entries)if(name[0]&&(!strcmp(name,e.metadata.id)||!strcmp(name,e.metadata.name)))return e.metadata;p34_copy_string((uintptr_t)&d.name,result.name,sizeof(result.name));return result;}
static bool visible(Section& s,const AnyItemDefinitionV1& d){if(!anyapi_owner_active(s.owner))return false;if(!s.visible)return true;__try{return s.visible(&d,s.user);}__except(EXCEPTION_EXECUTE_HANDLER){anyapi_owner_fault(s.owner);return false;}}
// Expand borrowed copies of the per-frame UI descriptors, never game inventory.
// The column's existing cursor calculation then includes the reserved rows.
static void column_hook(void* inventory,void* frontend,void* client,void* data,const int32_t* index,const bool* hovered,const NativeVector* list,const int32_t* kind,void* dragged,const int32_t* active_id,void* movement,void* out_id,void* color,int32_t* cursor,void* hints){NativeVector view{},external{};std::vector<Descriptor> copied;std::vector<std::pair<const Descriptor*,int32_t>> heights;NativeVector borrowed;const NativeVector* draw_list=list;
 if(ready&&!faulted&&vector_view((uintptr_t)list,view,88)&&vector_view((uintptr_t)data+0x70,external,88)){copied.resize(view.count);if(!view.count||safe_read_memory(view.data,copied.data(),copied.size()*88)){std::vector<Section> local;{std::lock_guard lock(registry_mutex);local=sections;}for(auto& d:copied){heights.push_back({&d,d.flags});bool found=false;for(int i=0;i<external.count;++i){Descriptor e;if(descriptor(external.data+i*88,e)&&!memcmp(&d,&e,56)){found=true;break;}}if(!found)continue;auto meta=definition(d);for(auto& s:local)if(visible(s,meta)&&d.flags>=0&&d.flags<4096)d.flags+=s.reserved_rows;}borrowed=view;borrowed.data=(uintptr_t)copied.data();draw_list=&borrowed;}}
 auto previous_heights=original_heights;original_heights=&heights;original_column(inventory,frontend,client,data,index,hovered,draw_list,kind,dragged,active_id,movement,out_id,color,cursor,hints);original_heights=previous_heights;
}
static Item* find_item(uint64_t token){if(!allowed())return nullptr;for(auto& a:active->items)if(a.copy.token==token)return &a;return nullptr;}
static Grid* find_grid(uint64_t token){if(!allowed())return nullptr;for(auto& a:active->grids)if(a.copy.token==token)return &a;return nullptr;}
static bool copy_item(uint32_t side,uint32_t index,AnyStoredItemV1* out){if(!allowed()||side>1||!out||out->struct_size!=sizeof(*out)||out->version!=1)return false;for(auto& a:active->items)if(a.copy.side==side){if(!index--){*out=a.copy;return true;}}return false;}
static bool copy_grid(uint32_t side,uint32_t index,AnyStorageGridV1* out){if(!allowed()||side>1||!out||out->struct_size!=sizeof(*out)||out->version!=1)return false;for(auto& a:active->grids)if(a.copy.side==side){if(!index--){*out=a.copy;return true;}}return false;}
static std::string widget_id(const char* id){return "anyapi.inventory."+std::to_string(drawing->owner)+"."+drawing->id+"."+std::to_string(active->frame.context)+"."+id;}
static bool begin_row(const char* id,uint32_t columns){if(!allowed()||!services::identifier(id)||columns<1||columns>2||rows>=4)return false;auto name=widget_id(id);auto n=make_const_geostring(name.c_str());int32_t count=columns,spacing=0;int extent=6;for(auto& g:active->grids)if(g.copy.side==ANY_INVENTORY_STORAGE)extent=std::max(extent,g.native.origin_x+g.native.width);int cell_width=std::clamp(extent/int(columns),3,16);Vec2S32 size{cell_width,1};row_widths[rows]=cell_width;menus::native_table(active->ui,&n,&count,&size,&spacing);++rows;return true;}
static void end_row(){if(active&&drawing&&rows>0){menus::native_end(active->ui);--rows;}}
static uint32_t button(const char* id,const char* text,uint32_t disabled){if(!allowed()||!services::identifier(id)||!text||strlen(text)>160||!rows)return 0;auto name=widget_id(id);auto n=make_const_geostring(name.c_str()),label=make_const_geostring(text);Vec2S32 size{row_widths[rows-1],1};uintptr_t icon=0;bool off=disabled!=0,highlight=false;uint8_t selected=0,enter=0;menus::native_button(&selected,active->ui,&n,&label,&size,&icon,&off,&highlight);if(off||!selected)return 0;menus::native_enter(&enter,active->ui);return enter!=0;}
static bool compact_row(const char* id,uint32_t columns){if(!allowed()||!services::identifier(id)||columns<1||columns>3||rows>=4)return false;auto name=widget_id(id);auto n=make_const_geostring(name.c_str());int32_t count=columns,spacing=0;Vec2S32 size{2,1};row_widths[rows]=2;menus::native_table(active->ui,&n,&count,&size,&spacing);++rows;return true;}
static uint32_t symbol_button(const char* id,const char* symbol,const char* tooltip,uint32_t disabled){if(!allowed()||!services::identifier(id)||!symbol||strlen(symbol)>4||!tooltip||strlen(tooltip)>240||!rows)return 0;auto name=widget_id(id);auto n=make_const_geostring(name.c_str()),label=make_const_geostring(symbol);Vec2S32 size{2,1};uintptr_t icon=0;bool off=disabled!=0,highlight=false;uint8_t hovered=0,enter=0;uint32_t background=off?0xff181818:0xff202020;native_tag(&hovered,active->ui,&n,&label,&size,&background,&highlight);if(hovered)hover_tip=tooltip;if(off||!hovered)return 0;menus::native_enter(&enter,active->ui);return enter!=0;}
// The inventory owner's end-screen boundary renders copied hover text after the
// native storage hierarchy has closed, so tooltips do not consume grid rows.
static void screen_end_hook(void* ui){
 tip_end(ui);if(hover_tip.empty())return;auto text=std::move(hover_tip);hover_tip.clear();Vec2S32 point{},viewport{};if(!tooltip_cursor(&point,&viewport))return;
 auto id=make_const_geostring("anyapi_storage_hover"),position_id=make_const_geostring("position"),background_id=make_const_geostring("background"),entry=make_const_geostring("description"),label=make_const_geostring(text.c_str());
 // Vertical enum 2 leaves the child at the bottom-origin grid position.
 // Enum 0 adds (screen height - child height), pushing this popup off-screen.
 int32_t left=0,bottom=2,layout=1;bool capture=false,rounded=true;uint32_t background=0xb0202020,color=0xffffffff;
 int width=std::clamp(int(text.size()+3)/4+1,4,12),height=std::clamp(int(text.size()+width*4-1)/(width*4),1,5);Vec2S32 size{width,height};
 point.x=std::clamp(point.x+1,0,std::max(0,viewport.x-width));point.y=std::clamp(point.y>=height+1?point.y-height-1:point.y+1,0,std::max(0,viewport.y-height));
 tip_screen(ui,&id,&viewport,&left,&bottom,&left,&bottom,&layout,&capture);tip_position(ui,&position_id,&point,&size);tip_background(ui,&background_id,&rounded,&background);tip_text(ui,&entry,&label,&size,&left,&color);menus::native_end(ui);menus::native_end(ui);tip_end(ui);
}

static bool same_type(uint64_t a,uint64_t b){auto x=find_item(a),y=find_item(b);return x&&y&&x->definition==y->definition&&x->variant==y->variant;}
static bool native_merge(Item* source,Item* destination){__try{uintptr_t fn{};uint8_t result=0;if(!p34_virtual(destination->object,merge_slot,fn)||!st_verified_merge(fn))return false;((void(*)(uint8_t*,void*,void*))fn)(&result,(void*)destination->object,(void*)source->object);return result!=0;}__except(EXCEPTION_EXECUTE_HANDLER){faulted=true;return false;}}
static bool can_stack(uint64_t source,uint64_t destination){auto s=find_item(source),d=find_item(destination);return s&&d&&s!=d&&s->copy.quantity>0&&d->copy.capacity>d->copy.quantity&&native_merge(s,d);}
static bool budget(){return allowed()&&last_event_frame!=platform::frames.load();}
static void sent(const char* kind){last_event_frame=platform::frames.load();log_line(ANY_LOG_INFO,"framework",("INVENTORY_NATIVE_EVENT operation="+std::string(kind)+" submission=1 replication_confirmation=PENDING").c_str());}
static bool stack(uint64_t source,uint64_t destination){if(!budget()||!can_stack(source,destination))return false;auto s=find_item(source),d=find_item(destination);__try{stack_event((void*)((uintptr_t)active->client+0x90),&s->id,&d->id);sent("stack");return true;}__except(EXCEPTION_EXECUTE_HANDLER){faulted=true;return false;}}
static bool quick_store(uint64_t token,uint32_t side){auto item=find_item(token);if(!budget()||!item||side>1||item->copy.side==side)return false;
 __try{auto client=(void*)((uintptr_t)active->client+0x90);if(side==ANY_INVENTORY_PLAYER)quick_in(client,&item->id);else{auto target=parent_id(active->storage,active->storage.parent);NativeVector targets;targets.data=(uintptr_t)&target;targets.count=targets.capacity=1;targets.stride=24;quick_out(client,&item->id,&targets);}sent("quick_store");return true;}__except(EXCEPTION_EXECUTE_HANDLER){faulted=true;return false;}}
static bool accepts(uint64_t token,uint64_t grid,int32_t rotation){auto item=find_item(token);auto target=find_grid(grid);if(!item||!target||target->copy.fixed_slots||rotation<0||rotation>3||((rotation&1)&&!target->copy.allow_rotation)||item->copy.width<=0||item->copy.height<=0)return false;
 __try{auto n=target->native;n.filter=make_const_geostring(target->filter);uint8_t result=0;compatible(&result,active->inventory,&n,(void*)item->definition,&rotation);return result!=0;}__except(EXCEPTION_EXECUTE_HANDLER){faulted=true;return false;}}
static bool overlaps(int x,int y,int w,int h,const AnyStoredItemV1& a){int aw=(a.rotation&1)?a.height:a.width,ah=(a.rotation&1)?a.width:a.height;return x<a.x+aw&&a.x<x+w&&y<a.y+ah&&a.y<y+h;}
static bool can_move(uint64_t token,uint64_t grid,int32_t x,int32_t y,int32_t rotation){auto item=find_item(token);auto target=find_grid(grid);if(!accepts(token,grid,rotation)||!item||!target)return false;int w=(rotation&1)?item->copy.height:item->copy.width,h=(rotation&1)?item->copy.width:item->copy.height;if(x<0||y<0||x+w>target->copy.width||y+h>target->copy.height)return false;for(auto& a:active->items)if(a.copy.grid==grid&&a.copy.token!=token&&(a.copy.width<=0||a.copy.height<=0||overlaps(x,y,w,h,a.copy)))return false;return true;}
static bool move(uint64_t token,uint64_t grid,int32_t x,int32_t y,int32_t rotation){if(!budget()||!can_move(token,grid,x,y,rotation))return false;auto item=find_item(token);auto target=find_grid(grid);int32_t loc[]={target->parent.type,target->parent.peer,target->parent.vehicle,target->parent.floor,target->parent.part,target->parent.parent,target->native.index,x,y,rotation,target->native.index};double pose[12]{},velocity[3]{};
 __try{uintptr_t actor{},fn{};if(!safe_read_memory((uintptr_t)active->scene+0x188,&actor,8)||!actor){if(!safe_read_memory((uintptr_t)active->scene+0x190,&actor,8)||!actor)return false;}if(!p34_virtual(actor,drop_slot,fn)||(!p34_body(fn,ST_DROP_ACTOR)&&!p34_body(fn,ST_DROP_CHARACTER)))return false;((void(*)(double*,void*,const int32_t*))fn)(pose,(void*)actor,&item->id.item);move_event((void*)((uintptr_t)active->client+0x90),&item->id,loc,pose,velocity);sent("move");return true;}__except(EXCEPTION_EXECUTE_HANDLER){faulted=true;return false;}}
static void invoke(Section* s,const AnyInventoryUiFrameV1* frame){__try{s->draw(frame,s->user);}__except(EXCEPTION_EXECUTE_HANDLER){anyapi_owner_fault(s->owner);log_line(ANY_LOG_ERROR,"framework","INVENTORY_SECTION disabled_after_exception=1");}}
static void title_hook(uint8_t* out,void* ui,const GeoString16* id,const GeoString16* label,const uintptr_t* icon,const Vec2S32* size){original_title(out,ui,id,label,icon,size);if(!ready||faulted||!active){rejected("title_context ready="+std::to_string(ready.load())+" faulted="+std::to_string(faulted.load())+" active="+std::to_string(active!=nullptr));return;}AnyUiSnapshotV1 state;if(!ui_state_runtime::copy(&state)||state.kind!=ANY_UI_INVENTORY){rejected("title_ui_state native="+std::to_string(state.native_state));return;}NativeVector external;if(!vector_view((uintptr_t)active->data+0x70,external,88)){rejected("external_vector_invalid");return;}bool found=false;for(int i=0;i<external.count;++i){Descriptor d;if(descriptor(external.data+i*88,d)&&!memcmp(&d,&active->storage,56)){found=true;break;}}if(!found)return;auto meta=definition(active->storage);std::vector<Section> local;{std::lock_guard lock(registry_mutex);local=sections;}std::erase_if(local,[&](auto& s){return !visible(s,meta);});if(local.empty())return;if(!snapshot(*active)){rejected("snapshot_rejected");return;}
 active->ui=ui;for(auto& s:local){if(!anyapi_owner_active(s.owner))continue;drawing=&s;rows=0;int previous=platform::current_plugin;platform::current_plugin=s.owner;invoke(&s,&active->frame);while(rows)end_row();drawing=nullptr;platform::current_plugin=previous;}active->ui=nullptr;}
static void container_hook(void* inventory,void* frontend,void* client,void* data,void* scene,const int32_t* index,const Descriptor* d,void* dragged,const bool* hovered,const int32_t* active_id,void* out_id,void* color,void* hints){
 Context c;c.inventory=inventory;c.client=client;c.data=data;c.scene=scene;Context* previous=active;bool valid=descriptor((uintptr_t)d,c.storage);if(valid&&original_heights)for(auto& h:*original_heights)if(h.first==d){c.storage.flags=h.second;break;}active=valid?&c:nullptr;original_container(inventory,frontend,client,data,scene,index,valid?&c.storage:d,dragged,hovered,active_id,out_id,color,hints);active=previous;
}
static menus::Replacement replacements[4];
template<size_t N> static bool dependency(uintptr_t owner,size_t offset,const unsigned char(&body)[N],uintptr_t& fn){return p34_cell(owner,offset,fn)&&p34_body(fn,body);}
// Follow the native tooltip's own text overload chain. Several text and push
// element templates have identical bodies and cannot be chosen by byte count.
static bool resolve_tip_text(uintptr_t tooltip,uintptr_t& text){uintptr_t align{},color{};return dependency(tooltip,0xce0,ST_TIP_TEXT_ALIGN,align)&&dependency(align,0x50,ST_TIP_TEXT_COLOR,color)&&dependency(color,0xf0,ST_TIP_TEXT,text);}
static bool slot(uintptr_t owner,size_t offset,uintptr_t& out){return safe_read_memory(owner+offset,&out,8)&&out%8==0&&out<0x10000;}
static bool prepare(){if(!menus::ready)return false;auto columns=scan_exact(ST_COLUMN,sizeof(ST_COLUMN));auto containers=scan_exact(ST_CONTAINER,sizeof(ST_CONTAINER));auto release_grid=scan_exact(ST_RELEASE_GRID,sizeof(ST_RELEASE_GRID));auto release_stack=scan_exact(ST_RELEASE_STACK,sizeof(ST_RELEASE_STACK));if(columns.size()!=1||containers.size()!=1||release_grid.size()!=1||release_stack.size()!=1)return false;column_owner=columns[0];container_owner=containers[0];uintptr_t fn{},a{},b{},c{},d{},e{},f{},h{},i{},j{},k{},l{};
 if(!dependency(column_owner,0x2678,ST_CONTAINER,fn)||fn!=container_owner||!dependency(container_owner,0x6438,ST_TITLE,a)||!dependency(container_owner,0x6458,ST_GRIDS,b)||!dependency(container_owner,0x6468,ST_ITEMS,c)||!dependency(container_owner,0x6450,ST_GRID_CTOR,d)||!dependency(container_owner,0x6460,ST_ITEM_CTOR,e)||!p34_cell(container_owner,0x6708,f)||!p34_cell(container_owner,0x6710,h)||!dependency(container_owner,0x6690,ST_QUICK_IN,i)||!dependency(release_grid[0],0xc78,ST_MOVE,j)||!dependency(release_stack[0],0x3d0,ST_STACK,k)||!dependency(container_owner,0x64f0,ST_COMPATIBLE,l))return false;
 auto outside=scan_exact(ST_QUICK_OUT,sizeof(ST_QUICK_OUT));if(outside.size()!=1)return false;
 if(!slot(release_stack[0],0x3d8,capacity_slot)||!slot(release_stack[0],0x3e0,quantity_slot)||!slot(release_stack[0],0x3b0,merge_slot)||!slot(release_stack[0],0x3c8,id_slot)||!slot(release_grid[0],0xc68,drop_slot))return false;
 original_container=(ContainerFn)fn;original_title=(TitleFn)a;get_grids=(CollectionFn)b;get_items=(CollectionFn)c;grids_ctor=(CtorFn)d;items_ctor=(CtorFn)e;items_dtor=(DtorFn)f;grids_dtor=(DtorFn)h;quick_in=(InFn)i;quick_out=(OutFn)outside[0];move_event=(MoveFn)j;stack_event=(StackFn)k;compatible=(CompatibleFn)l;
 auto parent=scan_exact(ST_LAYOUT_OWNER,sizeof(ST_LAYOUT_OWNER));uintptr_t col{};if(parent.size()!=1||!dependency(parent[0],0xcea0,ST_COLUMN,col)||col!=column_owner)return false;original_column=(ColumnFn)col;replacements[2]={parent[0],0xcea0,0,(uintptr_t)column_hook};
 auto tags=scan_exact(ST_COMPACT_TAG,sizeof(ST_COMPACT_TAG)),tag_layouts=scan_exact(ST_COMPACT_TAG_LAYOUT,sizeof(ST_COMPACT_TAG_LAYOUT)),tag_renders=scan_exact(ST_COMPACT_TAG_RENDER,sizeof(ST_COMPACT_TAG_RENDER));if(tags.size()!=1||tag_layouts.size()!=1||tag_renders.size()!=1)return false;native_tag=(TagFn)tags[0];
 auto positions=scan_exact(ST_TIP_POSITION,sizeof(ST_TIP_POSITION));if(positions.size()!=1)return rejected("tooltip_position_matches="+std::to_string(positions.size()));tip_position=(decltype(tip_position))positions[0];
 if(!menus::dimension(parent[0],0xcdb0,screen_columns,16,512)||!menus::dimension(parent[0],0xcdb8,screen_rows,8,512))return false;
 uintptr_t screen{},end{};auto tips=scan_exact(ST_TIP_ENTRY,sizeof(ST_TIP_ENTRY));if(tips.size()!=1||!dependency(parent[0],0xcda0,ST_TIP_SCREEN,screen)||!dependency(parent[0],0xd028,ST_TIP_END,end))return false;uintptr_t background{};uintptr_t text_fn{};if(!dependency(tips[0],0xca0,ST_TIP_BACKGROUND,background)||!resolve_tip_text(tips[0],text_fn))return rejected("tooltip_dependencies_invalid");tip_text=(decltype(tip_text))text_fn;tip_background=(decltype(tip_background))background;tip_screen=(ScreenFn)screen;tip_entry=(TipFn)tips[0];tip_end=(void(*)(void*))end;replacements[3]={parent[0],0xd028,0,(uintptr_t)screen_end_hook};
 replacements[0]={column_owner,0x2678,0,(uintptr_t)container_hook};replacements[1]={container_owner,0x6438,0,(uintptr_t)title_hook};for(auto& r:replacements)if(!safe_read_memory(r.owner+r.offset,&r.previous,8)||!r.previous)return false;
 if(!menus::replace(replacements[0]))return false;if(!menus::replace(replacements[1])){menus::restore(replacements[0]);return false;}if(!menus::replace(replacements[2])){menus::restore(replacements[1]);menus::restore(replacements[0]);return false;}if(!menus::replace(replacements[3])){menus::restore(replacements[2]);menus::restore(replacements[1]);menus::restore(replacements[0]);return false;}ready=true;log_line(ANY_LOG_INFO,"framework","INVENTORY_UI ready=1 native_container_toolbar=1 native_events=1 copied_instances=1 centered_symbols=1 cursor_tooltip=1 no_external_window=1");return true;
}
static DWORD WINAPI worker(void*){while(!ready&&!faulted){if(prepare())break;Sleep(5000);}return 0;}
static const AnyInventoryUiV1 api{sizeof(AnyInventoryUiV1),1,add,available,copy_item,copy_grid,begin_row,end_row,button,same_type,can_stack,stack,quick_store,accepts,can_move,move};
static const AnyInventoryUiV2 api_v2{sizeof(AnyInventoryUiV2),2,&api,add_v2};
static const AnyInventoryUiV3 api_v3{sizeof(AnyInventoryUiV3),3,&api_v2,compact_row,symbol_button};
}
static const AnyInventoryUiV1* anyapi_inventory_ui_service(){return &inventory_ui::api;}
static const AnyInventoryUiV2* anyapi_inventory_ui_service_v2(){return &inventory_ui::api_v2;}

static const AnyInventoryUiV3* anyapi_inventory_ui_service_v3(){return &inventory_ui::api_v3;}
