// Minimal read-only Settings tab, independent of Mod Controls.
#include "../anyapi_services_v1.h"
#include "../anyapi_menu_v2.h"
static const AnyMenuV2* menu;
static void draw(const AnyMenuFrameV1*,void*) {
    menu->heading("section","MY MOD");
    menu->label("message","Welcome to My Mod settings.");
}
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(
    const AnyModHostV1* host,AnyModCallbacksV1* callbacks) {
    if(!host||!callbacks||host->abi!=1||host->struct_size!=sizeof(*host))return false;
    auto services=AnyAPI_Services();
    menu=services?static_cast<const AnyMenuV2*>(services->query("anyapi.menu",2)):nullptr;
    if(!menu||menu->version!=2||menu->struct_size!=sizeof(*menu))return false;
    AnyMenuTabV2 tab;
    tab.id="settings";tab.title="MY MOD";tab.order=200;
    tab.icon=ANY_MENU_ICON_SETTINGS;tab.draw=draw;
    callbacks->id="my_mod";
    return menu->add_tab(&tab);
}
