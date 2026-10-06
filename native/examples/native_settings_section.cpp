// Example independent settings mod. Compile as an x64 DLL and place in the mods root.
#include "anyapi_services_v1.h"
#include "anyapi_menu_v1.h"
static const AnyMenuV1* menu;static bool committed{},pending{};
static void draw(const AnyMenuFrameV1*,void*){if(menu->begin_table("rows",1,0)){if(menu->button("toggle",pending?"Example option: On":"Example option: Off",0))pending=!pending;menu->end_table();}}
static uint32_t dirty(void*){return pending!=committed;}static uint32_t defaults(void*){return !pending;}
static void event(uint32_t kind,void*){if(kind==ANY_MENU_OPEN||kind==ANY_MENU_CANCEL)pending=committed;else if(kind==ANY_MENU_RESET)pending=false;else if(kind==ANY_MENU_APPLY)committed=pending;}
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1*,AnyModCallbacksV1* callbacks){auto services=AnyAPI_Services();menu=services?(const AnyMenuV1*)services->query("anyapi.menu",1):nullptr;if(!menu)return false;
 AnyMenuSectionV1 s;s.id="example_options";s.title="EXAMPLE OPTIONS";s.order=200;s.draw=draw;s.event=event;s.dirty=dirty;s.defaults=defaults;callbacks->id="example_settings";return menu->add_section(&s);}
// Persistence intentionally omitted in this small example; production mods own their save format.
