// A mod owns its settings behavior; AnyHelpers stores committed values.
#include "../anyapi_services_v1.h"
#include "../anyhelpers_settings_v1.h"
static const AnyHelpersSettingsV1* settings;
static uint64_t token;
extern "C" __declspec(dllexport) void AnyAPI_ModReady(){
 auto services=AnyAPI_Services();settings=services?(const AnyHelpersSettingsV1*)services->query("anyhelpers.settings",1):nullptr;
 if(!settings||settings->version!=1||settings->struct_size!=sizeof(*settings)){settings=nullptr;return;}
 AnyModSettingV1 d;d.mod_id="example";d.mod_name="Example Mod";d.setting_id="enabled";d.label="Enabled";d.kind=ANY_SETTING_BOOL;d.default_number=1;token=settings->register_setting(&d);
}
bool example_enabled(){AnySettingValueV1 value;return settings&&token&&settings->get(token,&value)?value.number!=0:true;}
