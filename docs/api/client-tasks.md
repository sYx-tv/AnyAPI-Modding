# Client tasks

Query `anyapi.client_tasks`, version 1. Register an owner during ModInit, ModReady
or another owned callback, then retain its token. post() accepts that token,
an optional world epoch, a callback and caller-owned user data. It can be called
from a background thread. A zero ticket means the request was rejected.
During ModInit the owned callback may register and post, but background callers
must wait for initialization to finish and the plugin slot to be published.

Tasks run after the original native client actor tick and player snapshot
publication, outside the snapshot lock. Dispatch is limited to 32 jobs per
sample (at most every 50 ms), with at most 128 pending jobs. Menus, missing
players or a failed native producer pause dispatch. The queue does not supply
server authority or permission to mutate native objects from arbitrary threads.

Use the copied player snapshot's world_epoch to discard work when the producer
observes another scene. Epoch zero deliberately accepts the next active world.
Epochs describe producer observations, not persistent save IDs or a guarantee
that an unloaded scene cannot reuse a memory address.

cancel() removes a pending ticket only for its owner. Once dispatched, it returns
false. Keep user data alive until execution or successful cancellation. Stale
epoch jobs are discarded without calling their callback; cancel pending work
before releasing user data, or use process-lifetime task storage. Callbacks must
be short and nonblocking. Exceptions disable the provider and restore callback
ownership. Faulted providers' queued jobs are discarded.

DLLs remain loaded for process lifetime. This is a bounded scheduling mechanism,
not a hot-unload or server scheduling API.
