#include "../anyapi_services_v1.h"
#include "../anyapi_ui_state_v1.h"
// Resolve after all mods initialize; inspect in both input and render callbacks.
bool inventory_visible(){auto services=AnyAPI_Services();if(!services)return false;
 auto ui=(const AnyUiStateV1*)services->query("anyapi.ui_state",1);
 AnyUiSnapshotV1 state;return ui&&ui->struct_size==sizeof(*ui)&&ui->version==1&&ui->copy&&ui->copy(&state)&&state.kind==ANY_UI_INVENTORY;
}
