#include "anyapi_services_v1.h"
#include "anyapi_item_catalog_v1.h"
#include <vector>
// Call after all mods have initialized, from your AnyAPI_ModReady export.
void inspect_first_definition() {
    auto services = AnyAPI_Services();
    if (!services) return;
    auto catalog = static_cast<const AnyItemCatalogV1*>(services->query("anyapi.item_catalog", 1));
    if (!catalog || !catalog->count()) return;
    AnyItemDefinitionV1 item;
    if (!catalog->copy(0, &item)) return;
    // Use item.id/name/description. Check valid_fields for optional numbers.
    uint32_t required = 0;
    catalog->definition_json(0, nullptr, 0, &required);
    if (!required) return;
    std::vector<char> json(required);
    if (!catalog->definition_json(0, json.data(), required, &required)) return;
    // json now contains the entire authored definition, including its NUL.
}
