#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_menu_v3.h"
#include "mod_controls_state.h"
#include "anyhelpers_settings_state.h"
#include <mutex>
#include <algorithm>
static AnyModHostV1 host;static const AnyMenuV2* menu;static modcontrols::State state;static std::mutex mutex;
static void log(const std::string& message,uint32_t level=0){host.log(level,"anyhelpers",message.c_str());}
// Register stable mod/action IDs; never depend on DLL load order or numeric native control enums.
static uint64_t register_action(const ModControlActionV1* action){std::lock_guard<std::mutex> lock(mutex);auto token=state.add(action);if(token)log("ACTION_REGISTER mod="+state.actions.back().mod+" action="+state.actions.back().id+" key="+std::to_string(state.actions.back().committed));else log("ACTION_REGISTER rejected=1",2);return token;}
static uint32_t get_key(uint64_t token){std::lock_guard<std::mutex> lock(mutex);return token&&token<=state.actions.size()?state.actions[token-1].committed:0;}
static bool get_name(uint64_t token,char* output,uint32_t capacity){std::lock_guard<std::mutex> lock(mutex);if(!token||token>state.actions.size()||!output||!capacity)return false;auto text=modcontrols::key_name(state.actions[token-1].committed);if(text.size()+1>capacity){output[0]=0;return false;}memcpy(output,text.c_str(),text.size()+1);return true;}
static const ModControlsV1 bindings{sizeof(ModControlsV1),1,register_action,get_key,get_name};
static uint32_t dirty(void*){std::lock_guard<std::mutex> lock(mutex);return state.dirty();}
static uint32_t defaults(void*){std::lock_guard<std::mutex> lock(mutex);return state.defaults();}
static void end_capture(){state.capture=-1;host.capture_input(0);}
// Native Apply commits; Reset stages; close/cancel discards. The game controls retain their own lifecycle.
static void event(uint32_t event,void*){std::lock_guard<std::mutex> lock(mutex);end_capture();if(event==ANY_MENU_OPEN)state.begin();else if(event==ANY_MENU_RESET){state.reset();log("BINDINGS_RESET staged=1");}else if(event==ANY_MENU_APPLY){bool changed=state.dirty(),ok=state.apply();if(changed)log(ok?"BINDINGS_APPLY saved=1":"BINDINGS_APPLY saved=0 pending_retained=1",ok?0:2);}else if(event==ANY_MENU_CANCEL){bool pending=state.dirty();state.cancel();log("MENU_CLOSED pending_discarded="+std::to_string(pending));}}
static std::string upper(std::string text){for(auto& c:text)if(c>='a'&&c<='z')c-='a'-'A';return text;}
// Native headings and rows use the game's font, geometry, hover state and scrolling.
static void draw(const AnyMenuFrameV1* frame,void*){std::lock_guard<std::mutex> lock(mutex);state.last_draw=frame->tick;
 if(state.capture>=0&&frame->tick-state.capture_tick>15000){end_capture();state.status="Key capture timed out.";log("CAPTURE_CANCEL reason=timeout");}
 if(state.actions.empty()){menu->label("empty","No mods have registered controls yet.");return;}
 std::vector<size_t> sorted;for(size_t i=0;i<state.actions.size();++i)sorted.push_back(i);std::sort(sorted.begin(),sorted.end(),[](size_t a,size_t b){auto& x=state.actions[a];auto& y=state.actions[b];return x.mod==y.mod?x.id<y.id:x.mod<y.mod;});std::string previous;
 for(auto i:sorted){auto& a=state.actions[i];if(a.mod!=previous){auto title=upper(a.name);menu->heading(("group."+std::to_string(i+1)).c_str(),title.c_str());previous=a.mod;}
  auto label=upper(a.label),key=state.capture==int(i)?std::string("Press a key..."):upper(modcontrols::key_name(a.draft));
  // Process-local tokens keep widget IDs short even with maximum-length stable IDs.
  auto result=menu->key_row(("action."+std::to_string(i+1)).c_str(),label.c_str(),key.c_str(),state.capture>=0);
  if(state.capture<0&&(result&ANY_MENU_ROW_CLEAR)){state.stage(i,0);log("BINDING_CLEAR mod="+a.mod+" action="+a.id+" staged=1");}
  else if(state.capture<0&&(result&ANY_MENU_ROW_REBIND)){state.capture=int(i);state.capture_tick=frame->tick;state.status="Press a key. Esc cancels; Delete unbinds.";host.capture_input(1);log("CAPTURE_BEGIN mod="+a.mod+" action="+a.id);}

 }
 menu->label("help",state.status.empty()?"Select a binding to change it. Apply saves changes.":state.status.c_str());
}
// Consume capture events before any consumer mod. Retain a down-key quarantine until release.
static uint32_t input(const AnyInputV1* e,void*){std::lock_guard<std::mutex> lock(mutex);if(!e)return 0;
 if(e->kind==ANY_FOCUS_LOST){end_capture();memset(state.held,0,sizeof(state.held));return 0;}
 if(e->kind==ANY_KEY_UP&&e->key<256&&state.held[e->key]){state.held[e->key]=false;return 1;}
 if(e->kind==ANY_KEY_DOWN&&e->key<256&&state.held[e->key])return 1;
 if(state.capture<0)return 0;if(!menu->settings_open()||GetTickCount64()-state.last_draw>1000){end_capture();log("CAPTURE_CANCEL reason=menu_left");return 0;}
 if(e->kind==ANY_KEY_DOWN&&e->key<256){state.held[e->key]=true;int index=state.capture;
  if(e->key==VK_ESCAPE){state.status="Key capture cancelled.";end_capture();log("CAPTURE_CANCEL reason=escape");}
  else if(modcontrols::State::key_valid(e->key)){bool ok=state.stage(size_t(index),e->key==VK_DELETE?0:e->key);end_capture();log("BINDING_STAGE mod="+state.actions[index].mod+" action="+state.actions[index].id+" key="+std::to_string(state.actions[index].draft)+" accepted="+std::to_string(ok));}
 }return 1;
}
#include "anyhelpers_settings.inc"
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* api,AnyModCallbacksV1* callbacks){if(!api||!callbacks||api->abi!=1||api->struct_size!=sizeof(*api)||!api->log||!api->capture_input||!api->plugin_directory)return false;
 host=*api;auto services=AnyAPI_Services();menu=services?(const AnyMenuV2*)services->query("anyapi.menu",2):nullptr;if(!menu||menu->struct_size!=sizeof(*menu))return false;
 state.file=std::filesystem::path(api->plugin_directory)/L"AnyHelpers"/L"bindings.tsv";
 auto canonical=state.file,legacy=std::filesystem::path(api->plugin_directory)/L"ModControls"/L"bindings.tsv";
 if(!std::filesystem::exists(canonical)&&std::filesystem::exists(legacy))state.file=legacy;
 auto rejected=state.load();state.file=canonical;callbacks->id="anyhelpers";
 AnyMenuTabV2 section;section.icon=ANY_MENU_ICON_KEYBOARD;section.id="mod_controls";section.title="MOD CONTROLS";section.order=100;section.draw=draw;section.event=event;section.dirty=dirty;section.defaults=defaults;
 if(!menu->add_tab(&section)||!services->input_filter(input,nullptr,100)||!services->publish("modcontrols.bindings",1,&bindings)||!services->publish("anyhelpers.controls",1,&bindings)||!settingsmod::init(services))return false;
 log("DLL_READY native_tabs_registered=2 legacy_binding_service=1 rejected_saved_rows="+std::to_string(rejected));return true;}
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(module);return TRUE;}

