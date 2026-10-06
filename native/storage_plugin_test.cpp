#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_inventory_ui_v3.h"
#include "storage_sort.h"
#include "anyhelpers_settings_v1.h"
#include <vector>
#include <string>
#include <cassert>
#include <iostream>
static void(*draw)(const AnyInventoryUiFrameV1*,void*);static void* user;static std::vector<AnyStoredItemV1> items;static std::vector<AnyStorageGridV1> grids;static std::string click;static uint64_t tick=1000;static unsigned submits;static bool apply_events=true;static std::vector<std::string> labels;static uint32_t setting_count;
static bool reg(const char* id,void(*f)(const AnyInventoryUiFrameV1*,void*),void* u){assert(!strcmp(id,"storage_tools"));draw=f;user=u;return true;}
static bool(*visible)(const AnyItemDefinitionV1*,void*);
static bool reg_v2(const char* id,bool(*v)(const AnyItemDefinitionV1*,void*),void(*f)(const AnyInventoryUiFrameV1*,void*),void* u,uint32_t reserve){assert(reserve==3);visible=v;return reg(id,f,u);}
static uint32_t available(){return 1;}
static bool item(uint32_t s,uint32_t i,AnyStoredItemV1* out){for(auto& a:items)if(a.side==s&&!i--){*out=a;return true;}return false;}
static bool grid(uint32_t s,uint32_t i,AnyStorageGridV1* out){for(auto& a:grids)if(a.side==s&&!i--){*out=a;return true;}return false;}
static bool row(const char*,uint32_t){return true;}static void end(){}
static uint32_t button(const char* id,const char* label,uint32_t off){labels.push_back(label);if(!off&&click==id){click.clear();return 1;}return 0;}
static AnyStoredItemV1* find(uint64_t id){for(auto& a:items)if(a.token==id)return &a;return nullptr;}
static bool same(uint64_t a,uint64_t b){auto x=find(a),y=find(b);return x&&y&&!strcmp(x->definition,y->definition);}
static bool can_stack(uint64_t a,uint64_t b){auto x=find(a),y=find(b);return x&&y&&a!=b&&same(a,b)&&y->quantity<y->capacity;}
static bool stack(uint64_t a,uint64_t b){if(!can_stack(a,b))return false;++submits;if(apply_events){auto x=find(a),y=find(b);int n=std::min(x->quantity,y->capacity-y->quantity);x->quantity-=n;y->quantity+=n;if(!x->quantity)std::erase_if(items,[&](auto& i){return i.token==a;});}return true;}
static bool quick(uint64_t a,uint32_t s){auto x=find(a);if(!x)return false;++submits;if(apply_events)x->side=s;return true;}
static bool accepts(uint64_t,uint64_t,int){return true;}
static bool move_check(uint64_t id,uint64_t grid,int x,int y,int r){auto a=find(id);if(!a)return false;auto g=std::find_if(grids.begin(),grids.end(),[&](auto& g){return g.token==grid;});if(g==grids.end()||x<0||y<0||x+storage_sort::width(*a,r)>g->width||y+storage_sort::height(*a,r)>g->height)return false;storage_sort::Placement p{id,grid,x,y,r};for(auto& b:items)if(b.token!=id&&storage_sort::overlap(p,*a,b))return false;return true;}
static bool move(uint64_t id,uint64_t grid,int x,int y,int r){auto a=find(id);if(!a)return false;++submits;if(apply_events){a->grid=grid;a->x=x;a->y=y;a->rotation=r;for(auto& g:grids)if(g.token==grid)a->side=g.side;}return true;}
static const AnyInventoryUiV1 api{sizeof(AnyInventoryUiV1),1,reg,available,item,grid,row,end,button,same,can_stack,stack,quick,accepts,move_check,move};
static const AnyInventoryUiV2 api_v2{sizeof(AnyInventoryUiV2),2,&api,reg_v2};
static bool compact_row(const char*,uint32_t columns){assert(columns==3);return true;}
static uint32_t symbol(const char* id,const char* label,const char* tip,uint32_t off){assert(strlen(label)<=3&&strlen(tip)>0&&strlen(tip)<=240);return button(id,label,off);}
static const AnyInventoryUiV3 api_v3{sizeof(AnyInventoryUiV3),3,&api_v2,compact_row,symbol};
static uint64_t setting(const AnyModSettingV1* d){assert(!strcmp(d->mod_id,"anystorage"));++setting_count;return setting_count;}
static bool get(uint64_t id,AnySettingValueV1* v){v->number=id==3?0:id==5?120:1;return true;}static uint64_t revision(){return 1;}
static const AnyHelpersSettingsV1 settings{sizeof(AnyHelpersSettingsV1),1,setting,get,revision};
static void log(uint32_t,const char*,const char*){}
static AnyStoredItemV1 make(uint64_t id,const char* name,int qty,uint32_t side,int x){AnyStoredItemV1 a;a.token=id;a.side=side;a.grid=side?100:200;a.x=x;a.width=a.height=1;a.quantity=qty;a.capacity=10;strcpy_s(a.name,name);strcpy_s(a.definition,name);return a;}
static void frame(const char* hit="",uint64_t context=77){tick+=150;click=hit;labels.clear();AnyInventoryUiFrameV1 f;f.context=context;f.tick=tick;for(auto& a:items)(a.side?f.storage_items:f.player_items)++;for(auto& g:grids)(g.side?f.storage_grids:f.player_grids)++;draw(&f,user);}
int wmain(int argc,wchar_t** argv){assert(argc==3);auto fixture=LoadLibraryW(argv[1]);assert(fixture);auto service=(AnyGetServicesV1)GetProcAddress(fixture,"AnyAPI_GetServices");assert(service);assert(service(1)->publish("anyapi.inventory_ui",3,&api_v3));assert(service(1)->publish("anyhelpers.settings",1,&settings));auto mod=LoadLibraryW(argv[2]);assert(mod);auto init=(AnyModInitV1)GetProcAddress(mod,"AnyAPI_ModInit");auto ready=(void(*)())GetProcAddress(mod,"AnyAPI_ModReady");AnyModHostV1 host;host.log=log;AnyModCallbacksV1 callbacks;assert(init(&host,&callbacks)&&!callbacks.render&&!callbacks.input&&!strcmp(callbacks.id,"anystorage"));ready();assert(draw&&visible&&setting_count==6);
 AnyItemDefinitionV1 meta;for(auto cls:{"clothing","torch","gun_rifle"}){strcpy_s(meta.item_class,cls);assert(!visible(&meta,nullptr));}meta={};strcpy_s(meta.id,"angled_torch");strcpy_s(meta.name,"Angled Torch");assert(!visible(&meta,nullptr));meta={};strcpy_s(meta.name,"Signal Detector");assert(!visible(&meta,nullptr));meta={};strcpy_s(meta.id,"backpack_pouch");assert(!visible(&meta,nullptr));meta={};strcpy_s(meta.item_class,"backpack");assert(visible(&meta,nullptr));meta={};strcpy_s(meta.item_class,"container");strcpy_s(meta.name,"Crate");assert(visible(&meta,nullptr));
 AnyStorageGridV1 g;g.token=100;g.side=1;g.width=4;g.height=2;grids.push_back(g);g.token=200;g.side=0;grids.push_back(g);
 items={make(1,"Apple",3,0,0),make(2,"Apple",8,1,0),make(3,"Zinc",1,0,1)};frame("stack_out");for(int i=0;i<10;++i)frame();assert(find(2)->quantity==10&&find(3)->side==0&&find(1)->side==1&&find(1)->y==1);assert(submits==2);
 frame("deposit");for(int i=0;i<10;++i)frame();assert(find(3)->side==1);frame("withdraw");for(int i=0;i<10;++i)frame();for(auto& a:items)assert(a.side==0);
 items={make(1,"Zinc",3,1,0),make(2,"Apple",8,1,1)};frame("sort_action");for(int i=0;i<20;++i)frame();assert(find(2)->x==0&&find(1)->x==1&&find(1)->y==1&&find(2)->y==1);
 items={make(1,"Apple",3,0,0)};apply_events=false;unsigned before=submits;frame("deposit");frame();for(int i=0;i<8;++i)frame();assert(submits==before+1);frame("cancel");for(int i=0;i<20;++i)frame();assert(submits==before+1);
 frame("deposit");frame();before=submits;frame("",88);for(int i=0;i<20;++i)frame("",88);assert(submits==before);
 std::cout<<"PASS: actual AnyStorage DLL, native-only callbacks, six settings, matching-only/full-stack transfers, both bulk directions, replicated sort, no duplicate unacknowledged events, cancel/context change\n";FreeLibrary(mod);FreeLibrary(fixture);
}
