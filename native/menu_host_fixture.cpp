#include <cassert>
#include "anyapi_item_catalog.h"
#include "item_catalog_preview.h"
#include "anyapi_item_images_v1.h"
#include "anyapi_inventory_actions_v2.h"
#include "anyapi_screen_layout_v1.h"
#define NOMINMAX
#include "anyapi_services_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyapi_menu_v3.h"
#include <map>
#include <string>
static AnyMenuSectionV1 graphics_section;
static AnyMenuTabV2 section,settings;static bool registered{},open{},clicked{};
static std::map<std::string,const void*> providers;
static uint32_t rows,click_mode;static std::string setting_click,setting_text;static double setting_number;
static uint32_t(*filter)(const AnyInputV1*,void*);static void* filter_user;
static bool add(const AnyMenuTabV2* s){if(!s)return false;if(!strcmp(s->id,"mod_controls")){if(registered)return false;section=*s;registered=true;}else if(!strcmp(s->id,"mod_settings")&&!settings.draw)settings=*s;else return false;return true;}
static uint32_t available(){return 1;}static uint32_t is_open(){return open;}
static void heading(const char*,const char*){}static bool table(const char*,int32_t,int32_t){return true;}static void end(){}
static uint32_t button(const char* id,const char*,uint32_t disabled){++rows;if(clicked&&!disabled&&!strcmp(id,"action.1")){clicked=false;return 1;}return 0;}
static void label(const char*,const char*){++rows;}
static uint32_t key_row(const char* id,const char*,const char* key,uint32_t disabled){return button(id,key,disabled)?(click_mode==2?ANY_MENU_ROW_CLEAR:ANY_MENU_ROW_REBIND):0;}
static uint32_t change(const char* id){++rows;return id==setting_click;}
static uint32_t toggle(const char* id,const char*,uint32_t* value){if(!change(id))return 0;*value=uint32_t(setting_number);return 1;}
static uint32_t number(const char* id,const char*,double* value,double,double,double,uint32_t){if(!change(id))return 0;*value=setting_number;return 1;}
static uint32_t choice(const char* id,const char*,const char*const*,uint32_t,uint32_t* value){if(!change(id))return 0;*value=uint32_t(setting_number);return 1;}
static uint32_t text(const char* id,const char*,char* value,uint32_t capacity){if(!change(id))return 0;strncpy_s(value,capacity,setting_text.c_str(),_TRUNCATE);return 1;}
static const AnyMenuV2 menu2{sizeof(AnyMenuV2),2,add,available,is_open,heading,label,key_row};
static const AnyMenuV3 menu3{sizeof(AnyMenuV3),3,&menu2,toggle,number,choice,text};
static bool add_section(const AnyMenuSectionV1* s){if(!s||s->location!=ANY_MENU_SETTINGS_GRAPHICS||graphics_section.draw)return false;graphics_section=*s;return true;}
static const AnyMenuV1 menu{sizeof(AnyMenuV1),1,add_section,available,is_open,heading,table,end,button};
static std::vector<itemcatalog::Entry> catalog_entries;
static uint32_t catalog_count(){return uint32_t(catalog_entries.size());}
static bool catalog_copy(uint32_t i,AnyItemDefinitionV1* out){if(!out||out->struct_size!=sizeof(*out)||out->version!=1||i>=catalog_entries.size())return false;*out=catalog_entries[i].metadata;return true;}
static bool catalog_json(uint32_t i,char* bytes,uint32_t capacity,uint32_t* required){if(required)*required=0;if(!required||i>=catalog_entries.size())return false;auto& json=catalog_entries[i].json;*required=uint32_t(json.size()+1);if(!bytes||capacity<*required)return false;memcpy(bytes,json.c_str(),*required);return true;}
static const AnyItemCatalogV1 catalog_api{sizeof(AnyItemCatalogV1),1,catalog_count,catalog_copy,catalog_json};
static std::filesystem::path catalog_game;
static bool image_copy(uint32_t i,uint8_t* bytes,uint32_t capacity,uint32_t* required){if(required)*required=0;if(!required||i>=catalog_entries.size())return false;*required=65536;if(!bytes||capacity<*required)return false;auto image=itemcatalog::preview(catalog_game,catalog_entries[i]);memcpy(bytes,image.data(),*required);return true;}
static uint32_t add_calls;static std::string add_id;static uint32_t add_available=1;static double layout_x,portrait_x,tooltip_x;
static uint32_t actions_available(){return add_available;}
static uint64_t request_add(const char* id){if(!add_available)return 0;add_id=id;++add_calls;return add_calls;}
static uint32_t request_state(uint64_t ticket){return ticket?ANY_ITEM_REQUEST_SENT:ANY_ITEM_REQUEST_UNKNOWN;}
static bool screen_offset(const char* id,int32_t state,double x,double){if(state!=2)return false;if(!strcmp(id,"ui_aligner"))layout_x=x;else if(!strcmp(id,"portrait_aligner"))portrait_x=x;else if(!strcmp(id,"ui_aligner_tooltip"))tooltip_x=x;else return false;return true;}
static int32_t fixture_ui=2;
static bool ui_copy(AnyUiSnapshotV1* out){if(!out||out->struct_size!=sizeof(*out)||out->version!=1)return false;*out={};if(fixture_ui<0||fixture_ui>=128)return false;out->native_state=fixture_ui;out->kind=fixture_ui==1?ANY_UI_GAMEPLAY:fixture_ui==2?ANY_UI_INVENTORY:ANY_UI_MENU;return true;}
static const AnyUiStateV1 ui_api{sizeof(AnyUiStateV1),1,ui_copy};
extern "C" __declspec(dllexport) void FixtureUiState(int32_t state){fixture_ui=state;}
static uint32_t fixture_mode=ANY_MODE_SANDBOX;
static bool session_copy(AnySessionStateV1* out){if(!out||out->struct_size!=sizeof(*out)||out->version!=1||fixture_mode==ANY_MODE_UNKNOWN)return false;*out={};out->mode=fixture_mode;out->sampled_tick=GetTickCount64();return true;}
static uint64_t masked_request(const char* id,uint32_t mask){return fixture_mode<32&&(mask&(1u<<fixture_mode))?request_add(id):0;}
static const AnySessionV1 session_api{sizeof(AnySessionV1),1,session_copy};
extern "C" __declspec(dllexport) void FixtureMode(uint32_t value){fixture_mode=value;}
static const AnyItemImagesV1 images_api{sizeof(AnyItemImagesV1),1,image_copy};
static const AnyInventoryActionsV1 actions_api{sizeof(AnyInventoryActionsV1),1,actions_available,request_add,request_state};
static const AnyInventoryActionsV2 actions_api2{sizeof(AnyInventoryActionsV2),2,&actions_api,masked_request};
static const AnyScreenLayoutV1 layout_api{sizeof(AnyScreenLayoutV1),1,actions_available,screen_offset};
extern "C" __declspec(dllexport) uint32_t FixtureAddCalls(){return add_calls;}
extern "C" __declspec(dllexport) const char* FixtureAddId(){return add_id.c_str();}
extern "C" __declspec(dllexport) void FixtureAddAvailable(uint32_t value){add_available=value;}
extern "C" __declspec(dllexport) double FixtureLayoutOffset(){assert(layout_x==portrait_x&&layout_x==tooltip_x);return layout_x;}
extern "C" __declspec(dllexport) uint32_t FixtureCatalogLoad(const wchar_t* game){catalog_game=game;catalog_entries=itemcatalog::load(game);return catalog_count();}
static const void* query(const char* id,uint32_t version){if(!strcmp(id,"anyapi.ui_state"))return version==1?&ui_api:nullptr;if(!strcmp(id,"anyapi.item_images"))return version==1?&images_api:nullptr;if(!strcmp(id,"anyapi.session"))return version==1?&session_api:nullptr;if(!strcmp(id,"anyapi.inventory_actions"))return version==1?(const void*)&actions_api:version==2?(const void*)&actions_api2:nullptr;if(!strcmp(id,"anyapi.screen_layout"))return version==1?&layout_api:nullptr;if(!strcmp(id,"anyapi.item_catalog"))return version==1?&catalog_api:nullptr;if(!strcmp(id,"anyapi.menu"))return version==1?(const void*)&menu:version==2?(const void*)&menu2:version==3?(const void*)&menu3:nullptr;auto found=providers.find(std::string(id)+"@"+std::to_string(version));return found==providers.end()?nullptr:found->second;}
static bool publish(const char* id,uint32_t version,const void* value){return version&&value&&providers.emplace(std::string(id)+"@"+std::to_string(version),value).second;}
static bool input_filter(uint32_t(*fn)(const AnyInputV1*,void*),void* user,int32_t priority){if((priority!=100&&priority!=20)||filter)return false;filter=fn;filter_user=user;return true;}
static const AnyServicesV1 services{sizeof(AnyServicesV1),1,query,publish,input_filter};
extern "C" __declspec(dllexport) const AnyServicesV1* AnyAPI_GetServices(uint32_t version){return version==1?&services:nullptr;}
extern "C" __declspec(dllexport) void FixtureEvent(uint32_t event){open=event!=ANY_MENU_CANCEL;section.event(event,section.user);}
extern "C" __declspec(dllexport) void FixtureDraw(uint32_t click){rows=0;click_mode=click;clicked=click!=0;AnyMenuFrameV1 frame;frame.location=ANY_MENU_SETTINGS_TAB;frame.tick=GetTickCount64();section.draw(&frame,section.user);}
extern "C" __declspec(dllexport) void FixtureTimeout(){clicked=false;AnyMenuFrameV1 frame;frame.tick=GetTickCount64()+16000;section.draw(&frame,section.user);}
extern "C" __declspec(dllexport) uint32_t FixtureRows(){return rows;}
extern "C" __declspec(dllexport) uint32_t FixtureInput(const AnyInputV1* e){return filter(e,filter_user);}
extern "C" __declspec(dllexport) uint32_t FixtureDirty(){return section.dirty(section.user);}
extern "C" __declspec(dllexport) void FixtureSettingsEvent(uint32_t event){open=event!=ANY_MENU_CANCEL;settings.event(event,settings.user);}
extern "C" __declspec(dllexport) uint32_t FixtureSettingsDirty(){return settings.dirty(settings.user);}
extern "C" __declspec(dllexport) void FixtureStage(uint64_t token,double number,const char* text){rows=0;setting_click="setting."+std::to_string(token);setting_number=number;setting_text=text?text:"";AnyMenuFrameV1 frame;frame.location=ANY_MENU_SETTINGS_TAB;settings.draw(&frame,settings.user);setting_click.clear();}

extern "C" __declspec(dllexport) const char* FixtureGraphicsTitle(){return graphics_section.title;}
extern "C" __declspec(dllexport) void FixtureGraphicsEvent(uint32_t type){assert(graphics_section.event);graphics_section.event(type,graphics_section.user);}
extern "C" __declspec(dllexport) uint32_t FixtureGraphicsDirty(){return graphics_section.dirty(graphics_section.user);}
extern "C" __declspec(dllexport) void FixtureGraphicsStage(uint64_t token,double value){rows=0;setting_click="setting."+std::to_string(token);setting_number=value;AnyMenuFrameV1 frame;frame.location=ANY_MENU_SETTINGS_GRAPHICS;graphics_section.draw(&frame,graphics_section.user);setting_click.clear();}
