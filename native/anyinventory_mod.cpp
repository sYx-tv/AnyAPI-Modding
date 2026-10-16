#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include "anyapi_services_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyapi_menu_v1.h"
#include "anyapi_item_catalog_v1.h"
#include "anyapi_item_images_v1.h"
#include "anyapi_inventory_actions_v2.h"
#include "anyapi_screen_layout_v1.h"
#include "inventory_layout.h"
#include "inventory_localization.h"
#include <optional>
#include "mod_controls_v1.h"
#include "anyhelpers_settings_v1.h"
#include "map_markers.h"
#include <mutex>
#include <map>
#include <set>
#include <fstream>
#include <filesystem>
#include <algorithm>
using namespace Gdiplus;
namespace browser {
using UiText=inventory_localization::Text;
static inventory_localization::Localization localization;
static std::vector<inventory_localization::DisplayItem> display_items;
static bool localization_loaded{},language_resolved{},options_registered{};static uint64_t language_check{};
static std::wstring tr(UiText key){return inventory_localization::wide(localization.text(key));}
static const AnyUiStateV1* ui_api;
static bool inventory_open(){AnyUiSnapshotV1 state;return ui_api&&ui_api->copy(&state)&&state.kind==ANY_UI_INVENTORY;}
static void inventory_offset(double);
static AnyModHostV1 host;static const AnyItemCatalogV1* catalog;static const ModControlsV1* controls;static const AnyHelpersSettingsV1* settings;static const AnyMenuV1* menu;
static const AnySessionV1* session;static const AnyInventoryActionsV2* inventory_v2;static bool add_allowed{};static RectF search_all_box;
static const AnyItemImagesV1* images;static const AnyInventoryActionsV1* inventory;static const AnyScreenLayoutV1* layout;static std::map<uint32_t,std::vector<uint8_t>> image_cache;static uint64_t add_ticket{};static float ui_scale=1;static bool mouse_capture{},captured{},pressed_add{};
static uint64_t action{},tokens[5]{},settings_revision=UINT64_MAX;static bool enabled=true,descriptions=true,show_ids=false,left=false,active{},open=true,search_focus{},filter_open{},favorites_only{},key_held[256]{};static double width_option=480;
static std::mutex mutex;static ULONG_PTR gdiplus;static std::filesystem::path folder;static std::vector<AnyItemDefinitionV1> items;static std::vector<uint32_t> results;static std::vector<std::string> categories;static std::set<std::string> favorites;static std::string search,category;static std::optional<UiText> status;static uint32_t selected=UINT32_MAX;static int scroll{},filter_scroll{};static uint64_t revision{},last_tick{};static int last_w{},last_h{},origin_x{},origin_y{},frame_w{},frame_h{},logged_w{},logged_h{};
static RectF search_box,category_box,close_box,favorite_filter,reset_box,list_box,star_box,add_box;static std::vector<std::pair<RectF,uint32_t>> rows;static std::vector<std::pair<RectF,std::string>> filters;static std::vector<uint8_t> pixels;
static std::wstring wide(const std::string& value){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);if(!n)return {};std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),n);return out;}
static void log(const std::string& event){host.log(0,"anyinventory",event.c_str());}
// Search keeps English names/IDs as aliases without changing the catalog ABI.
static void rebuild(){
 if(display_items.size()!=items.size()){
  display_items.clear();display_items.reserve(items.size());
  for(auto& item:items)display_items.push_back(localization.item(item));
 }
 results.clear();auto query=inventory_localization::fold(search);
 for(uint32_t i=0;i<items.size();++i){
  auto& item=items[i];auto& display=display_items[i];
  if((!category.empty()&&category!=item.category)||(favorites_only&&!favorites.contains(item.id)))continue;
  auto hay=display.search+(descriptions?" "+display.description_search:"");
  if(inventory_localization::matches(hay,query))results.push_back(i);
 }
 std::stable_sort(results.begin(),results.end(),[](uint32_t a,uint32_t b){
  auto& x=display_items[a].sort;auto& y=display_items[b].sort;
  return x==y?strcmp(items[a].id,items[b].id)<0:x<y;
 });
 if(selected<items.size()&&std::find(results.begin(),results.end(),selected)==results.end())selected=UINT32_MAX;
 scroll=0;last_tick=0;
}
static void localization_refresh(){
 auto now=GetTickCount64();if(localization_loaded&&now-language_check<1000)return;language_check=now;
 auto steam=inventory_localization::steam_language();
 language_resolved=!steam.empty();
 auto code=steam.empty()?localization.language:inventory_localization::language_code(steam);
 if(localization_loaded&&code==localization.language)return;
 localization.load(std::filesystem::path(host.game_directory),code);localization_loaded=true;
 display_items.clear();rebuild();
 log("LOCALIZATION language="+code+" source="+(steam.empty()?"fallback":"steam")+
     " item_names="+std::to_string(localization.names.size())+" component_names="+std::to_string(localization.component_names.size()));
}
static void register_options(){
 if(options_registered||(!language_resolved&&GetModuleHandleW(L"steam_api64.dll")))return;
 options_registered=true;
 if(controls){ModControlActionV1 d;d.mod_id="anyinventory";d.mod_name="AnyInventory";d.action_id="toggle_browser";d.label=localization.text(UiText::toggle);d.default_key=VK_F8;action=controls->register_action(&d);}if(settings){const char* ids[]={"enabled","panel_width","panel_side","search_descriptions","show_ids"},*labels[]={localization.text(UiText::enabled),localization.text(UiText::width),localization.text(UiText::side),localization.text(UiText::search_descriptions),localization.text(UiText::show_ids)},*sides[]={localization.text(UiText::right),localization.text(UiText::left)};uint32_t kinds[]={ANY_SETTING_BOOL,ANY_SETTING_INTEGER,ANY_SETTING_CHOICE,ANY_SETTING_BOOL,ANY_SETTING_BOOL};double defaults[]={1,480,0,1,0},minimum[]={0,380,0,0,0},maximum[]={1,720,1,1,1};for(int i=0;i<5;++i){AnyModSettingV1 d;d.mod_id="anyinventory";d.mod_name="AnyInventory";d.setting_id=ids[i];d.label=labels[i];d.kind=kinds[i];d.order=i;d.default_number=defaults[i];d.minimum=minimum[i];d.maximum=maximum[i];d.step=i==1?20:1;if(i==2){d.choices=sides;d.choice_count=2;}tokens[i]=settings->register_setting(&d);}log("SETTINGS_SERVICE registered=5");}
}
static void capture_update(){bool wanted=search_focus||mouse_capture;if(captured!=wanted){captured=wanted;host.capture_input(wanted?1:0);}}
static void focus(bool value){search_focus=value;capture_update();last_tick=0;}
static void release_input(){mouse_capture=false;pressed_add=false;focus(false);}
// Favorites are stable definition IDs, never pointers or owned item handles.
static void save_favorites(){auto file=folder/L"favorites.txt",temp=folder/L"favorites.tmp";std::ofstream out(temp);out<<"ANYINVENTORY_FAVORITES 1\n";for(auto& id:favorites)out<<id<<'\n';out.flush();bool ok=bool(out);out.close();if(!ok||!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temp.c_str());status=UiText::save_failed;log("FAVORITES save_failed=1");}}
static void load_favorites(){std::ifstream in(folder/L"favorites.txt");std::string line;if(!std::getline(in,line)||line!="ANYINVENTORY_FAVORITES 1")return;while(std::getline(in,line))if(std::any_of(items.begin(),items.end(),[&](auto& item){return line==item.id;}))favorites.insert(line);}
static void settings_read(){if(!options_registered||!settings||settings_revision==settings->revision())return;settings_revision=settings->revision();for(int i=0;i<5;++i){AnySettingValueV1 v;if(!settings->get(tokens[i],&v))continue;switch(i){case 0:enabled=v.number!=0;break;case 1:width_option=v.number;break;case 2:left=v.number!=0;break;case 3:descriptions=v.number!=0;break;case 4:show_ids=v.number!=0;break;}}rebuild();log("SETTINGS_APPLIED enabled="+std::to_string(enabled)+" width="+std::to_string(int(width_option))+" side="+(left?"left":"right"));}
static void text(Graphics& g,const std::wstring& value,RectF rect,float size=20,Color color=Color(255,235,235,235),bool wrap=false,bool bold=false){FontFamily family(L"Segoe UI");Font font(&family,size,bold?FontStyleBold:FontStyleRegular,UnitPixel);SolidBrush brush(color);StringFormat f;if(!wrap)f.SetFormatFlags(StringFormatFlagsNoWrap);f.SetTrimming(wrap?StringTrimmingEllipsisWord:StringTrimmingEllipsisCharacter);g.DrawString(value.c_str(),int(value.size()),&font,rect,&f,&brush);}
static void panel(Graphics& g,RectF r,Color c=Color(255,39,39,39)){SolidBrush brush(c);g.FillRectangle(&brush,r);}
static void button(Graphics& g,RectF r,const wchar_t* label,bool on=false){panel(g,r,on?Color(255,9,79,119):Color(255,43,43,43));if(r.Width<50){FontFamily family(L"Segoe UI Symbol");Font font(&family,18,FontStyleRegular,UnitPixel);SolidBrush brush(Color(255,235,235,235));StringFormat format;format.SetAlignment(StringAlignmentCenter);format.SetLineAlignment(StringAlignmentCenter);g.DrawString(label,-1,&font,r,&format,&brush);return;}text(g,label,{r.X+8,r.Y+8,r.Width-16,r.Height-8},18);}
// Each preview is a copied image made from the installed item's mesh and colours.
static void thumbnail(Graphics& g,uint32_t index,RectF rect){if(!images)return;auto found=image_cache.find(index);if(found==image_cache.end()){std::vector<uint8_t> bytes(128*128*4);uint32_t required{};if(!images->copy(index,bytes.data(),uint32_t(bytes.size()),&required))bytes.clear();found=image_cache.emplace(index,std::move(bytes)).first;}if(found->second.empty())return;Bitmap bitmap(128,128,512,PixelFormat32bppPARGB,found->second.data());g.SetInterpolationMode(InterpolationModeHighQualityBicubic);g.DrawImage(&bitmap,rect);}
static void draw(Graphics& g,int w,int h){g.Clear(Color(250,30,30,30));rows.clear();filters.clear();text(g,tr(UiText::title),{16,12,float(w)-68,34},26,Color(255,238,238,238),false,true);close_box={float(w)-46,10,34,34};button(g,close_box,L"\u00d7");
 search_box={12,54,float(w)-24,44};panel(g,search_box,search_focus?Color(255,9,66,96):Color(255,20,20,20));text(g,search.empty()?tr(UiText::search):wide(search)+(search_focus?L"_":L""),{24,65,float(w)-48,28},20,search.empty()?Color(255,150,150,150):Color(255,238,238,238));
 category_box={12,106,float(w)-164,38};favorite_filter={float(w)-144,106,132,38};button(g,category_box,(category.empty()?tr(UiText::all_categories):wide(localization.category(category))).c_str());button(g,favorite_filter,tr(UiText::favorites).c_str(),favorites_only);text(g,std::to_wstring(results.size())+L" / "+std::to_wstring(items.size())+L" "+tr(UiText::items),{16,156,float(w)-128,26},16,Color(255,165,165,165));reset_box={float(w)-98,152,86,32};button(g,reset_box,tr(UiText::clear).c_str());
 float detail_height=selected<items.size()?300.f:0;list_box={12,194,float(w)-24,std::max(72.f,float(h)-240-detail_height)};int capacity=std::max(1,int(list_box.Height/76));scroll=std::clamp(scroll,0,std::max(0,int(results.size())-capacity));
 for(int row=0;row<capacity&&scroll+row<int(results.size());++row){auto index=results[scroll+row];auto& item=items[index];auto& display=display_items[index];RectF cell(list_box.X,list_box.Y+row*76,list_box.Width-7,72);panel(g,cell,index==selected?Color(255,9,79,119):Color(255,39,39,39));thumbnail(g,index,{cell.X+3,cell.Y+4,64,64});text(g,(favorites.contains(item.id)?L"\u2605 ":L"")+wide(display.name),{cell.X+78,cell.Y+7,cell.Width-90,38},20,Color(255,235,235,235),true);text(g,show_ids?wide(item.id):wide(display.category),{cell.X+78,cell.Y+47,cell.Width-90,22},16,Color(255,170,180,185));rows.push_back({cell,index});}
 search_all_box={};if(results.empty()){text(g,tr(items.empty()?UiText::catalog_unavailable:UiText::no_matches),{24,216,float(w)-48,36},20);if(!category.empty()||favorites_only){search_all_box={24,258,float(w)-48,38};button(g,search_all_box,tr(UiText::search_all).c_str());}}
 if(int(results.size())>capacity){float y=list_box.Y+(list_box.Height-36)*scroll/std::max(1,int(results.size())-capacity);panel(g,{list_box.GetRight()-5,y,5,36},Color(255,88,88,88));}
 star_box={};add_box={};if(selected<items.size()){auto& item=items[selected];auto& display=display_items[selected];float y=float(h)-344;panel(g,{12,y,float(w)-24,298},Color(255,23,23,23));thumbnail(g,selected,{float(w)-136,y+12,96,96});text(g,wide(display.name),{24,y+12,float(w)-178,58},24,Color(255,235,235,235),true,true);star_box={float(w)-48,y+10,28,32};button(g,star_box,favorites.contains(item.id)?L"\u2605":L"\u2606");text(g,show_ids?wide(item.id):wide(display.category),{24,y+78,float(w)-178,24},16,Color(255,165,165,165));text(g,display.description.empty()?tr(UiText::no_description):wide(display.description),{24,y+118,float(w)-48,110},18,Color(255,215,215,215),true);if(add_allowed){add_box={22,y+246,float(w)-44,40};bool available=inventory&&inventory->available();panel(g,add_box,available?Color(255,9,79,119):Color(255,43,43,43));text(g,tr(UiText::add),{add_box.X+12,add_box.Y+7,add_box.Width-24,30},20,available?Color(255,238,238,238):Color(255,130,130,130));}}
 text(g,status?tr(*status):L"F8: "+tr(UiText::hide),{16,float(h)-35,float(w)-32,28},16,Color(255,165,165,165));
 if(filter_open){float height=std::min(420.f,float(h)-160);int capacity=int(height/38);filter_scroll=std::clamp(filter_scroll,0,std::max(0,int(categories.size())+1-capacity));panel(g,{12,146,float(w)-24,height},Color(255,18,18,18));for(int row=0;row<capacity&&filter_scroll+row<=int(categories.size());++row){int at=filter_scroll+row;std::string value=at?categories[at-1]:"";RectF cell(16,150+row*38.f,float(w)-32,36);button(g,cell,(at?wide(localization.category(value)):tr(UiText::all_categories)).c_str(),category==value);filters.push_back({cell,value});}}
}
// AnyAPI appends definitions that mods add to a loaded world (new parts and items), so pick up any growth.
static void catalog_refresh(){if(!catalog)return;uint32_t n=catalog->count();if(n<=items.size()||n>10000)return;size_t before=items.size();for(uint32_t i=uint32_t(before);i<n;++i){AnyItemDefinitionV1 item;if(!catalog->copy(i,&item))break;items.push_back(item);}if(items.size()==before)return;
 std::set<std::string> unique(categories.begin(),categories.end());for(size_t i=before;i<items.size();++i)if(items[i].category[0])unique.insert(items[i].category);categories.assign(unique.begin(),unique.end());load_favorites();rebuild();last_tick=0;log("CATALOG_GROWN definitions="+std::to_string(items.size())+" added="+std::to_string(items.size()-before));}
// Input arrives in window client pixels; the panel is drawn in backbuffer pixels. They differ when fullscreen
// or display scaling stretches the backbuffer, so map clicks before hit testing.
static PointF backbuffer_point(int x,int y){PointF p{float(x),float(y)};HWND window=GetForegroundWindow();DWORD pid{};RECT client{};if(frame_w>0&&frame_h>0&&window&&GetWindowThreadProcessId(window,&pid)&&pid==GetCurrentProcessId()&&GetClientRect(window,&client)&&client.right>0&&client.bottom>0){p.X*=float(frame_w)/client.right;p.Y*=float(frame_h)/client.bottom;}return p;}
static uint32_t input(const AnyInputV1* event,void*){if(!event||event->struct_size!=sizeof(*event))return 0;std::lock_guard lock(mutex);auto& e=*event;if(e.kind==ANY_FOCUS_LOST){release_input();memset(key_held,0,sizeof(key_held));active=false;filter_open=false;return 0;}if(e.kind==ANY_KEY_UP&&e.key<256)key_held[e.key]=false;if(!inventory_open()){active=false;release_input();filter_open=false;memset(key_held,0,sizeof(key_held));inventory_offset(0);return 0;}if(!active)return 0;
 uint32_t key=controls&&action?controls->key(action):VK_F8;if((e.kind==ANY_KEY_DOWN||e.kind==ANY_KEY_UP)&&e.key==key){bool edge=e.kind==ANY_KEY_DOWN&&!key_held[e.key<256?e.key:0];if(e.key<256)key_held[e.key]=e.kind==ANY_KEY_DOWN;if(edge){open=!open;filter_open=false;release_input();last_tick=0;mouse_capture=false;pressed_add=false;capture_update();}return 1;}
 if(!open)return 0;auto point=backbuffer_point(e.x,e.y);PointF mouse((point.X-origin_x)/ui_scale,(point.Y-origin_y)/ui_scale);bool mouse_event=e.kind>=ANY_MOUSE_MOVE&&e.kind<=ANY_MOUSE_WHEEL;bool inside=mouse.X>=0&&mouse.Y>=0&&mouse.X<last_w/ui_scale&&mouse.Y<last_h/ui_scale;
 if(e.kind==ANY_MOUSE_DOWN&&!inside){release_input();filter_open=false;return 0;}
 if(search_focus&&(e.kind==ANY_KEY_DOWN||e.kind==ANY_KEY_UP)){if(e.key<256){bool held=key_held[e.key];key_held[e.key]=e.kind==ANY_KEY_DOWN;if(e.kind==ANY_KEY_UP||held)return 1;}if(e.key==VK_ESCAPE||e.key==VK_RETURN){focus(false);return 1;}if(e.key==VK_BACK){mapmarkers::backspace(search);rebuild();}return 1;}
 if(search_focus&&e.kind==ANY_TEXT_INPUT){if(e.key>=32&&e.key!=127){mapmarkers::append(search,e.key);rebuild();}return 1;}
 if(e.kind==ANY_MOUSE_UP&&!inside){mouse_capture=false;pressed_add=false;capture_update();return 0;}if(!mouse_event||!inside)return 0;
 if(e.kind==ANY_MOUSE_WHEEL){if(filter_open)filter_scroll+=e.wheel>0?-3:3;else if(list_box.Contains(mouse))scroll+=e.wheel>0?-3:3;last_tick=0;return 1;}
 if(e.kind==ANY_MOUSE_DOWN&&e.button==1){mouse_capture=true;pressed_add=add_box.Contains(mouse);capture_update();if(search_box.Contains(mouse)){focus(true);filter_open=false;return 1;}focus(false);}
 if(e.kind==ANY_MOUSE_UP&&e.button==1){if(close_box.Contains(mouse)){open=false;focus(false);}else if(category_box.Contains(mouse)){filter_open=!filter_open;}else if(filter_open){for(auto& row:filters)if(row.first.Contains(mouse)){category=row.second;filter_open=false;rebuild();break;}}else if(favorite_filter.Contains(mouse)){favorites_only=!favorites_only;rebuild();}else if(reset_box.Contains(mouse)){search.clear();category.clear();favorites_only=false;rebuild();}else if(search_all_box.Contains(mouse)){category.clear();favorites_only=false;rebuild();}else if(star_box.Contains(mouse)&&selected<items.size()){auto id=std::string(items[selected].id);if(!favorites.erase(id))favorites.insert(id);save_favorites();rebuild();}else if(add_box.Contains(mouse)&&pressed_add){AnySessionStateV1 mode;bool permitted=session&&session->copy(&mode)&&(mode.mode==ANY_MODE_SANDBOX||mode.mode==ANY_MODE_CREATIVE);add_ticket=permitted&&inventory_v2?inventory_v2->add_one(items[selected].id,ANY_MODE_MASK_CREATIVE_SANDBOX):0;status=add_ticket?UiText::requesting:UiText::add_unavailable;}else for(auto& row:rows)if(row.first.Contains(mouse)){selected=row.second;last_tick=0;break;}last_tick=0;mouse_capture=false;pressed_add=false;capture_update();}return 1;
}
// Move all native inventory roots together, including the separate portrait/tooltip screens.
static void inventory_offset(double x){if(!layout)return;for(auto id:{"ui_aligner","portrait_aligner","ui_aligner_tooltip"})layout->set_offset(id,2,x,0);}
static void render(const AnyFrameV1* frame,AnyCanvasV1* canvas,void*){if(!frame||!canvas||frame->struct_size!=sizeof(*frame)||frame->width<640||frame->height<480||frame->width>8192||frame->height>8192)return;std::lock_guard lock(mutex);localization_refresh();register_options();settings_read();catalog_refresh();active=enabled&&frame->focused&&inventory_open()&&(!menu||!menu->settings_open());if(!active||!open){if(!active)memset(key_held,0,sizeof(key_held));release_input();filter_open=false;inventory_offset(0);return;}
 AnySessionStateV1 mode;bool mode_allowed=session&&session->copy(&mode)&&(mode.mode==ANY_MODE_SANDBOX||mode.mode==ANY_MODE_CREATIVE);if(add_allowed!=mode_allowed){add_allowed=mode_allowed;last_tick=0;pressed_add=false;}
 // Fit the backbuffer every frame so any resolution, aspect ratio or resize keeps the panel and the shifted inventory on screen.
 auto fit=inventory_layout::fit(int(frame->width),int(frame->height),width_option,left);bool rescaled=fit.scale!=ui_scale;ui_scale=fit.scale;int w=fit.width,h=fit.height;origin_x=fit.x;origin_y=fit.y;frame_w=int(frame->width);frame_h=int(frame->height);
 if(frame_w!=logged_w||frame_h!=logged_h){logged_w=frame_w;logged_h=frame_h;log("LAYOUT_FIT size="+std::to_string(frame_w)+"x"+std::to_string(frame_h)+" scale="+std::to_string(ui_scale)+" width="+std::to_string(w)+" shift="+std::to_string(fit.shift)+" beside="+std::to_string(fit.fits));}
 if(layout)inventory_offset(left?fit.shift:-fit.shift);
 if(add_ticket&&inventory){auto state=inventory->state(add_ticket);if(state!=ANY_ITEM_REQUEST_QUEUED){status=state==ANY_ITEM_REQUEST_SENT?UiText::requested:state==ANY_ITEM_REQUEST_EXPIRED?UiText::cancelled:UiText::request_failed;add_ticket=0;last_tick=0;}}
 if(!last_tick||rescaled||frame->tick-last_tick>=100||last_w!=w||last_h!=h){Bitmap bitmap(w,h,PixelFormat32bppPARGB);{Graphics g(&bitmap);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);g.ScaleTransform(ui_scale,ui_scale);draw(g,int(w/ui_scale),int(h/ui_scale));}BitmapData data{};Rect rect(0,0,w,h);if(bitmap.LockBits(&rect,ImageLockModeRead,PixelFormat32bppPARGB,&data)!=Ok)return;pixels.resize(size_t(w)*h*4);for(int y=0;y<h;++y)memcpy(pixels.data()+size_t(y)*w*4,(uint8_t*)data.Scan0+ptrdiff_t(y)*data.Stride,size_t(w)*4);bitmap.UnlockBits(&data);last_tick=frame->tick;last_w=w;last_h=h;++revision;}
 canvas->width=w;canvas->height=h;canvas->pitch=w*4;canvas->pixels=pixels.data();canvas->x=origin_x;canvas->y=origin_y;canvas->revision=revision;
}
static void shutdown(void*){std::lock_guard lock(mutex);release_input();inventory_offset(0);pixels.clear();image_cache.clear();if(gdiplus){GdiplusShutdown(gdiplus);gdiplus=0;}}
}
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* host,AnyModCallbacksV1* callbacks){using namespace browser;if(!host||!callbacks||host->struct_size!=sizeof(*host)||host->abi!=ANYAPI_MOD_ABI||!host->log||!host->copy_players||!host->capture_input||!host->game_directory||!host->plugin_directory)return false;browser::host=*host;folder=std::filesystem::path(host->plugin_directory)/L"AnyInventory";std::filesystem::create_directories(folder);GdiplusStartupInput in;if(GdiplusStartup(&gdiplus,&in,nullptr)!=Ok)return false;callbacks->id="anyinventory";callbacks->input=input;callbacks->render=render;callbacks->shutdown=shutdown;log("DLL_READY item_browser=1 item_previews=1 native_add_requests=1");return true;}
extern "C" __declspec(dllexport) void AnyAPI_ModReady(){using namespace browser;auto services=AnyAPI_Services();if(!services)return;localization_refresh();ui_api=(const AnyUiStateV1*)services->query("anyapi.ui_state",1);if(ui_api&&(ui_api->struct_size!=sizeof(*ui_api)||ui_api->version!=1||!ui_api->copy))ui_api=nullptr;session=(const AnySessionV1*)services->query("anyapi.session",1);inventory_v2=(const AnyInventoryActionsV2*)services->query("anyapi.inventory_actions",2);if(session&&(session->struct_size!=sizeof(*session)||session->version!=1||!session->copy))session=nullptr;if(inventory_v2&&(inventory_v2->struct_size!=sizeof(*inventory_v2)||inventory_v2->version!=2||!inventory_v2->add_one))inventory_v2=nullptr;images=(const AnyItemImagesV1*)services->query("anyapi.item_images",1);inventory=(const AnyInventoryActionsV1*)services->query("anyapi.inventory_actions",1);layout=(const AnyScreenLayoutV1*)services->query("anyapi.screen_layout",1);if(images&&(images->struct_size!=sizeof(*images)||images->version!=1||!images->copy))images=nullptr;if(inventory&&(inventory->struct_size!=sizeof(*inventory)||inventory->version!=1||!inventory->available||!inventory->add_one||!inventory->state))inventory=nullptr;if(layout&&(layout->struct_size!=sizeof(*layout)||layout->version!=1||!layout->set_offset))layout=nullptr;catalog=(const AnyItemCatalogV1*)services->query("anyapi.item_catalog",1);if(catalog&&catalog->struct_size==sizeof(*catalog)&&catalog->version==1){for(uint32_t i=0;i<catalog->count()&&i<10000;++i){AnyItemDefinitionV1 item;if(!catalog->copy(i,&item)){items.clear();log("CATALOG_READY copy_failed=1");break;}items.push_back(item);}std::set<std::string> unique;for(auto& item:items)if(item.category[0])unique.insert(item.category);categories.assign(unique.begin(),unique.end());load_favorites();rebuild();}else catalog=nullptr;log("CATALOG_READY definitions="+std::to_string(items.size())+" read_only=1");menu=(const AnyMenuV1*)services->query("anyapi.menu",1);controls=(const ModControlsV1*)services->query("anyhelpers.controls",1);settings=(const AnyHelpersSettingsV1*)services->query("anyhelpers.settings",1);register_options();}
