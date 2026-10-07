#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_equipment_v2.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyapi_item_catalog_v1.h"
#include "anyapi_item_images_v1.h"
#include "anyhelpers_settings_v1.h"
#include "mod_controls_v1.h"
#include "wheel_math.h"
#include <mutex>
#include <map>
#include <vector>
#include <string>
#include <algorithm>
static AnyModHostV1 host;
static const AnyEquipmentV2* equipment_api;static const AnyGpuDrawV1* gpu;static const AnyUiStateV1* ui;
static const AnyItemCatalogV1* catalog;static const AnyItemImagesV1* images;
static const ModControlsV1* controls;static const AnyHelpersSettingsV1* settings;
static std::mutex mutex;static uint64_t action,tokens[5]{},revision=UINT64_MAX,ticket{},message_until{};
static double values[5]{1,100,85,55,1};
static bool opened,held,focused;static uint32_t active_key;static float cx,cy,outer,scale=1,mouse_x,mouse_y;static int selected=-1;
static size_t page{};
static std::vector<AnyEquipmentToolV2> slots;
static int visible_count(){return int(std::min<size_t>(12,slots.size()-page*12));}static uint64_t context;
struct Icon {uint32_t catalog_index;uint64_t texture{};std::string name;bool tried{};};static std::map<std::string,Icon> icons;
static bool gameplay(){AnyUiSnapshotV1 s;return ui&&ui->copy(&s)&&s.kind==ANY_UI_GAMEPLAY;}
static std::wstring wide(const char* s){int n=MultiByteToWideChar(CP_UTF8,0,s,-1,nullptr,0);if(n<=1)return L"Item";std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s,-1,out.data(),n);out.pop_back();return out;}
static void refresh(){if(!settings)return;auto r=settings->revision();if(r==revision)return;revision=r;
 double lo[]={0,75,40,25,0},hi[]={1,150,100,90,1};for(int i=0;i<5;++i){AnySettingValueV1 v;if(settings->get(tokens[i],&v)&&std::isfinite(v.number))values[i]=std::clamp(v.number,lo[i],hi[i]);}}
static void close(){opened=false;selected=-1;if(host.capture_input)host.capture_input(0);}
static void confirm(){if(selected>=0&&selected<int(slots.size())){const auto& s=slots[selected];ticket=equipment_api->equip(context,s.item_id);if(!ticket)message_until=GetTickCount64()+2200;}close();}
static void choose(){selected=wheel::sector(mouse_x-cx,mouse_y-cy,values[3]*scale,outer*1.12, visible_count());if(selected>=0)selected+=int(page*12);}
static bool open(){AnyEquipmentToolsSnapshotV2 snapshot;if(!equipment_api||!gpu||!equipment_api->copy(&snapshot)||snapshot.count>ANY_EQUIPMENT_TOOLS_CAPACITY)return false;
 slots.assign(snapshot.tools,snapshot.tools+snapshot.count);
 if(slots.empty()){message_until=GetTickCount64()+2200;return false;}context=snapshot.context;page=0;opened=true;mouse_x=cx;mouse_y=cy;selected=-1;
 if(host.capture_input)host.capture_input(1);RECT r{};POINT p{};auto window=GetForegroundWindow();DWORD pid{};if(window)GetWindowThreadProcessId(window,&pid);if(pid==GetCurrentProcessId()&&GetClientRect(window,&r)){p.x=r.right/2;p.y=r.bottom/2;if(ClientToScreen(window,&p))SetCursorPos(p.x,p.y);}return true;}
static void frame(const AnyFrameV1* f,AnyCanvasV1*,void*){if(!f)return;std::lock_guard lock(mutex);refresh();focused=f->focused!=0;
 scale=float(values[1]/100)*std::clamp(float(f->height)/1080.f,.65f,1.6f);cx=f->width*.5f;cy=f->height*.5f;outer=std::min(225*scale,std::min(cx,cy)-24);scale=outer/225;
 if(!focused||!gameplay()||!values[0]){close();held=false;return;}
 if(opened){AnyEquipmentToolsSnapshotV2 s;if(!equipment_api->copy(&s)||s.context!=context||!s.count){close();return;}slots.assign(s.tools,s.tools+s.count);page=std::min(page,(slots.size()-1)/12);if(host.capture_input)host.capture_input(1);choose();}
 if(ticket){auto state=equipment_api->state(ticket);if(state!=ANY_EQUIPMENT_QUEUED){if(state!=ANY_EQUIPMENT_APPLIED)message_until=f->tick+2200;ticket=0;}}
}
static uint32_t input(const AnyInputV1* e,void*){if(!e)return 0;std::lock_guard lock(mutex);refresh();
 if(e->kind==ANY_FOCUS_LOST){close();held=false;focused=false;return 0;}
 if(opened&&(!focused||!gameplay()||!values[0])){close();held=false;return 0;}
 if(e->kind==ANY_KEY_UP&&held&&e->key==active_key){held=false;if(opened)confirm();return 1;}
 uint32_t binding=controls&&action?controls->key(action):'Q';
 if(e->kind==ANY_KEY_DOWN&&e->key==binding&&binding){if(held)return 1;if(!focused||!gameplay()||!values[0])return 0;held=true;active_key=binding;if(!open()){held=false;return 0;}return 1;}
 if(!opened)return 0;
 if(e->kind==ANY_MOUSE_MOVE){
  // Convert window-client pixels to renderer pixels for non-native resolutions.
  POINT p{e->x,e->y};RECT r{};HWND window=GetForegroundWindow();DWORD pid{};if(window)GetWindowThreadProcessId(window,&pid);if(pid==GetCurrentProcessId()&&GetClientRect(window,&r)&&r.right>0&&r.bottom>0){mouse_x=p.x*cx*2/r.right;mouse_y=p.y*cy*2/r.bottom;}else{mouse_x=float(p.x);mouse_y=float(p.y);}choose();return 1;
 }
 if(e->kind==ANY_MOUSE_WHEEL){size_t pages=(slots.size()+11)/12;if(pages>1){page=(page+pages+(e->wheel<0?1:pages-1))%pages;choose();}return 1;}
 if(e->kind==ANY_KEY_DOWN&&e->key==VK_ESCAPE){close();return 1;}
 if(e->kind==ANY_MOUSE_DOWN&&e->button==2){close();return 1;}
 if(e->kind==ANY_MOUSE_DOWN&&e->button==1){confirm();return 1;}
 return e->kind==ANY_KEY_DOWN||e->kind==ANY_MOUSE_DOWN||e->kind==ANY_MOUSE_WHEEL;
}
static uint32_t filter(const AnyInputV1* e,void*){return input(e,nullptr);}
static void text(const std::wstring& s,float x,float y,float w,float h,float size,uint32_t color,uint32_t flags=ANY_GPU_CENTER|ANY_GPU_VCENTER){AnyGpuCommandV1 c;c.kind=ANY_GPU_TEXT;c.text=s.c_str();c.text_length=uint32_t(s.size());c.rect[0]=x;c.rect[1]=y;c.rect[2]=w;c.rect[3]=h;c.font_size=size;c.color=color;c.flags=flags;gpu->emit(&c);}
static void circle(float x,float y,float radius,uint32_t color){AnyGpuCommandV1 c;c.kind=ANY_GPU_ELLIPSE;c.rect[0]=x-radius;c.rect[1]=y-radius;c.rect[2]=c.rect[3]=radius*2;c.color=color;gpu->emit(&c);}
static void draw(const AnyFrameV1* f,void*){if(!f||!gpu)return;std::lock_guard lock(mutex);
 if(!f->focused||!gameplay()||!values[0])return;
 if(message_until>f->tick)text(L"Could not equip tool. Please try again.",cx-260,cy-30,520,60,18,0xffeeeeee);
 if(!opened)return;int count=visible_count();float inner=outer*.43f;
 for(int i=0;i<count;++i){int index=int(page*12)+i;double angle=-wheel::pi/2+i*2*wheel::pi/count,half=wheel::pi/count-.025;
  std::vector<AnyGpuPointV1> points;for(int step=0;step<=16;++step){double a=angle-half+2*half*step/16;points.push_back({cx+outer*float(cos(a)),cy+outer*float(sin(a))});}
  for(int step=16;step>=0;--step){double a=angle-half+2*half*step/16;points.push_back({cx+inner*float(cos(a)),cy+inner*float(sin(a))});}
  AnyGpuCommandV1 c;c.kind=ANY_GPU_POLYGON;c.points=points.data();c.point_count=uint32_t(points.size());c.color=index==selected?0xff0b668d:0xff1e242b;c.opacity=float(values[2]/100);gpu->emit(&c);
  float ix=cx+outer*.72f*float(cos(angle)),iy=cy+outer*.72f*float(sin(angle)),size=std::min(64.f, float(2*wheel::pi*outer*.72/count/scale)*.75f)*scale;
  auto it=icons.find(slots[index].definition_id);uint64_t texture=0;
  if(values[4]&&it!=icons.end()&&images&&gpu->texture){auto& icon=it->second;if(!icon.tried){icon.tried=true;std::vector<uint8_t> bytes(65536);uint32_t required{};if(images->copy(icon.catalog_index,bytes.data(),uint32_t(bytes.size()),&required)&&required==bytes.size()){uint64_t id=uint64_t(icon.catalog_index)+1;if(gpu->texture(id,128,128,512,bytes.data(),1))icon.texture=id;}}texture=icon.texture;}
  if(texture){c={};c.kind=ANY_GPU_IMAGE;c.texture=texture;c.rect[0]=ix-size/2;c.rect[1]=iy-size/2;c.rect[2]=c.rect[3]=size;c.source[2]=c.source[3]=128;gpu->emit(&c);}
  else {std::wstring s=wide(slots[index].name).substr(0,1);text(s,ix-30*scale,iy-30*scale,60*scale,60*scale,27*scale,0xfff3f5f8);}
 }
 circle(cx,cy,inner-5*scale,0xde141a20);
 std::wstring title=L"TOOLS",name=L"Release here to cancel";
 if(selected>=0){const auto& slot=slots[selected];auto it=icons.find(slot.definition_id);name=wide(slot.name[0]?slot.name:it!=icons.end()?it->second.name.c_str():"Tool");title=L"RELEASE TO EQUIP";}
 text(title,cx-inner+8*scale,cy-42*scale,inner*2-16*scale,22*scale,10*scale,0xffaebbc7);
 text(name,cx-inner+10*scale,cy-13*scale,inner*2-20*scale,65*scale,18*scale,0xfff2f5f8);
 text(slots.size()>12?(L"Scroll: page "+std::to_wstring(page+1)+L" / "+std::to_wstring((slots.size()+11)/12)+L" · Right click / Esc cancels"):L"Right click / Esc to cancel",cx-180*scale,cy+outer+13*scale,360*scale,26*scale,12*scale,0xffc6cbd1);
 // The game may hide its cursor during gameplay. Draw a small native HUD cursor.
 circle(mouse_x,mouse_y,4*scale,0xfff6f8fa);
}
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* h,AnyModCallbacksV1* out){if(!h||!out||h->struct_size!=sizeof(*h)||h->abi!=1||out->struct_size!=sizeof(*out))return false;host=*h;out->id="anyquickwheel";out->render=frame;return true;}
extern "C" __declspec(dllexport) void AnyAPI_ModReady(){auto s=AnyAPI_Services();if(!s)return;
 equipment_api=(const AnyEquipmentV2*)s->query("anyapi.equipment",2);gpu=(const AnyGpuDrawV1*)s->query("anyapi.gpu_draw",1);ui=(const AnyUiStateV1*)s->query("anyapi.ui_state",1);
 if(!equipment_api||equipment_api->struct_size!=sizeof(*equipment_api)||equipment_api->version!=2||!equipment_api->copy||!equipment_api->equip||!equipment_api->state||!gpu||gpu->struct_size!=sizeof(*gpu)||gpu->version!=1||!gpu->emit||!gpu->register_renderer||!ui||ui->struct_size!=sizeof(*ui)||ui->version!=1||!ui->copy){equipment_api=nullptr;gpu=nullptr;if(host.log)host.log(2,"anyquickwheel","Requires AnyAPI 0.31.0 inventory tools v2, GPU draw and UI state services.");return;}
 controls=(const ModControlsV1*)s->query("anyhelpers.controls",1);if(controls&&controls->struct_size==sizeof(*controls)&&controls->version==1&&controls->register_action&&controls->key){ModControlActionV1 d;d.mod_id="anyquickwheel";d.mod_name="AnyQuickWheel";d.action_id="equipment_wheel";d.label="Hold equipment wheel";d.default_key='Q';action=controls->register_action(&d);}else controls=nullptr;
 settings=(const AnyHelpersSettingsV1*)s->query("anyhelpers.settings",1);if(settings&&settings->struct_size==sizeof(*settings)&&settings->version==1&&settings->register_setting&&settings->get&&settings->revision){
 const char* ids[]={"enabled","size","opacity","deadzone","icons"};const char* labels[]={"Enable equipment wheel","Wheel size (%)","Wheel opacity (%)","Centre cancel radius","Show item icons"};double lo[]={0,75,40,25,0},hi[]={1,150,100,90,1},step[]={1,5,5,5,1};
 for(int i=0;i<5;++i){AnyModSettingV1 d;d.mod_id="anyquickwheel";d.mod_name="AnyQuickWheel";d.setting_id=ids[i];d.label=labels[i];d.order=i;d.kind=(i==0||i>3)?ANY_SETTING_BOOL:ANY_SETTING_INTEGER;d.default_number=values[i];d.minimum=lo[i];d.maximum=hi[i];d.step=step[i];tokens[i]=settings->register_setting(&d);}
 }else settings=nullptr;
 catalog=(const AnyItemCatalogV1*)s->query("anyapi.item_catalog",1);images=(const AnyItemImagesV1*)s->query("anyapi.item_images",1);
 if(catalog&&catalog->struct_size==sizeof(*catalog)&&catalog->version==1&&catalog->count&&catalog->copy){for(uint32_t i=0;i<catalog->count();++i){AnyItemDefinitionV1 d;if(catalog->copy(i,&d))icons.emplace(d.id,Icon{i,0,d.name,false});}}else catalog=nullptr;
 if(images&&(images->struct_size!=sizeof(*images)||images->version!=1||!images->copy))images=nullptr;
 if(!gpu->register_renderer(draw,nullptr)||!s->input_filter||!s->input_filter(filter,nullptr,20)){gpu=nullptr;return;}
 if(host.log)host.log(0,"anyquickwheel","Ready: hold Q; hover a tool and release to equip. Automatically discovered carried construction tools.");
}
