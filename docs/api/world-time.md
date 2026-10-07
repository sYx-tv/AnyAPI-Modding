# World time

Query `anyapi.world_time`, version **1**, during `AnyAPI_ModReady`. Available in the AnyAPI 0.30.0 local candidate. Header: [anyapi_world_time_v1.h](../../sdk/include/anyapi_world_time_v1.h).

`copy(AnyWorldTimeSnapshotV1*)` validates the caller's structure size and version and returns a copied snapshot. It returns false before the first native sample or when the sample is older than 500 milliseconds. A valid caller's output is cleared on an unavailable result.

| Field | Meaning |
| --- | --- |
| `cycle_factor` | Native cycle normalized to [0, 1); midnight 0, noon 0.5 |
| `seconds_since_midnight` | Cycle mapped to the range 0–86399 |
| `sampled_tick` | Monotonic Windows milliseconds when the getter was observed |

This is a day/night-cycle service, not real-world time, a calendar or a simulation tick API. It respects the game's time override getter. Callers should check UI state separately before drawing gameplay UI.

The provider observes a verified native shared call cell, forwards the original getter exactly once and exposes no native pointers. The game's executable and data fingerprints must match the reviewed build. No setter is exposed. Live-world acceptance remains pending for this candidate.

```cpp
#include "anyapi_services_v1.h"
#include "anyapi_world_time_v1.h"

const auto* service = static_cast<const AnyWorldTimeV1*>(
    AnyAPI_Services()->query("anyapi.world_time", 1));
AnyWorldTimeSnapshotV1 time;
if (service && service->struct_size == sizeof(*service) &&
    service->version == 1 && service->copy && service->copy(&time)) {
    const unsigned hour = time.seconds_since_midnight / 3600;
    const unsigned minute = time.seconds_since_midnight / 60 % 60;
    // Format or use the copied values; do not retain native game objects.
}
```
