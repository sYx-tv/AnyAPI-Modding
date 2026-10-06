"""Generate a source-reconciled API/hook reference without upgrading runtime claims."""
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
header=(root/'anymaker_mod_api.h').read_text()
slots=re.findall(r'(Anymaker\w+Fn) (\w+);',header.split('struct AnymakerModContextV16 {')[1].split('};')[0])
coverage={row['slot']:row for row in json.loads((root/'API_COVERAGE.json').read_text())}
signature_audit={row['symbol']:row for row in json.loads((root/'SIGNATURE_AUDIT.json').read_text())}
routes={row['label']:row for row in json.loads((root/'EVENT_ROUTE_AUDIT.json').read_text())}
audit={row['label']:row for row in json.loads((root/'HOOK_AUDIT.json').read_text())}
specs=re.findall(r'\{"([^"]+)",(BODY_\w+),(\d+),', (root/'phase27_hook_specs.h').read_text())
purposes={
 'log':'Write a framework log line with a level and consumer identifier.',
 'register_item_lifecycle':'Subscribe to copied item create/remove/destroy metadata.',
 'register_digital_action':'Subscribe to copied native action begin/end requests.',
 'register_server_item_use':'Subscribe to authoritative item-use requests and resolution diagnostics.',
 'register_local_player_state':'Subscribe to sampled local-player world state captured on native tick.',
 'get_local_player_state':'Copy the latest sampled local state; fail after local teardown.',
 'register_vehicle_component':'Subscribe to copied component create/destroy events and class metadata.',
 'is_vehicle_component_live':'Query the current component registry state for an address.',
 'get_vehicle_component_vehicle':'Copy the current component owner address from the registry.',
 'copy_vehicle_component_class':'Copy the registered component behavior class, including valid empty names.',
 'register_ui_render':'Quarantined render callback registration; always rejected.',
 'ui_draw_rect':'Quarantined draw request; does not inject native graphics commands.',
 'ui_draw_texture':'Quarantined texture draw request; does not inject native graphics commands.',
 'resolve_ui_texture':'Quarantined texture lookup; returns no owned resource handle.',
 'is_virtual_key_down':'Poll a Windows virtual key; this is not Controls integration.',
 'get_ui_api_info':'Copy rendering availability and counters; readiness remains disabled.',
 'register_mod_action':'Retained registered-action mechanism; external consumers are disabled.',
 'get_mod_action_binding':'Read a registered action binding; no native Controls insertion.',
 'set_mod_action_binding':'Change/persist a registered action key; not a gameplay acceptance certificate.',
 'get_mod_action_api_info':'Copy action registry and persistence counters.',
 'register_client_actor_lifecycle':'Subscribe to copied actor create/delete/bulk teardown events.',
 'enumerate_client_actors':'Enumerate live registry snapshots without dereferencing native objects.',
 'get_client_actor_state':'Copy a live actor registry snapshot without calling a native getter.',
 'is_client_actor_live':'Query whether an address is presently live in the actor registry.',
 'get_client_actor_api_info':'Copy actor registry, event and snapshot-query counters.',
 'copy_item_definition_id':'Copy a bounded native definition identifier at +0x10.',
 'copy_item_definition_name':'Copy a bounded native definition display name at +0x50.',
 'copy_item_definition_description':'Copy a bounded native definition description at +0x60.',
 'copy_item_definition_class':'Copy a bounded native definition class at +0x30.',
 'copy_item_definition_mesh_file':'Copy a bounded native definition mesh path at +0xA0.',
 'register_inventory_pulse':'Quarantined authoritative inventory subscription; always rejected.',
 'enumerate_inventory':'Quarantined inventory enumeration; count is cleared and UNAVAILABLE returned.',
 'can_merge_inventory_items':'Quarantined compatibility request; output is cleared and UNAVAILABLE returned.',
 'request_inventory_merge':'Quarantined mutation request; no native event is dispatched.'}
# Offsets above are reconciled directly with the production string readers below.
source=(root/'dinput8_proxy.cpp').read_text()
for slot in [s for _,s in slots if s.startswith('copy_item_definition_')]:
    match=re.search(r'static size_t '+slot+r'\([^)]*\)\s*\{\s*return copy_definition_string_at\(d,(0x[0-9A-Fa-f]+),',source)
    assert match,slot
    purposes[slot]=re.sub(r'\+0x[0-9A-Fa-f]+', '+'+match.group(1),purposes[slot])
api=[]
for typedef,slot in slots:
    match=re.search(r'typedef\s+([^\n;()]+?)\s*\(\*'+typedef+r'\)\(([\s\S]*?)\);',header)
    assert match,typedef
    return_type=match.group(1).strip()
    signature=return_type+' '+slot+'('+re.sub(r'\s+',' ',match.group(2)).strip()+')'
    quarantine=slot in {'register_ui_render','ui_draw_rect','ui_draw_texture','resolve_ui_texture','register_inventory_pulse','enumerate_inventory','can_merge_inventory_items','request_inventory_merge'}
    snapshot=slot in {'enumerate_client_actors','get_client_actor_state','get_local_player_state','is_client_actor_live','is_vehicle_component_live','get_vehicle_component_vehicle','copy_vehicle_component_class'}
    if slot.startswith('copy_item_definition_'):
        thread='Native lifecycle/use/tick producer scope only for pointer reads; arbitrary consumer-thread/lifetime use is not established.'
        ownership='Borrowed definition address. No refcount acquisition or retained lifetime. Prefer strings already copied into events.'
        errors='ANYMAKER_API_INVALID_SIZE on null/unreadable/invalid header; returns full length on success and truncates a provided buffer with NUL termination.'
        example=f'char text[192]{{}}; size_t n=ctx->{slot}(event->definition,text,sizeof(text)); // native lifetime must already be valid'
    elif slot.startswith('register_'):
        thread='Registration uses a dedicated lock; queued callbacks run on the framework worker, not on the native producer thread.'
        ownership='Callback and user_data must remain valid for process lifetime; no unregister or safe module unload exists. External mods remain disabled.'
        errors='false for null callback or capacity exhaustion; quarantined registrations always return false.'
        example=f'bool registered=ctx->{slot}("example",callback,user_data); // adapt callback type to the signature; external loading disabled'
        if slot=='register_mod_action': example='bool registered=ctx->register_mod_action("example","action","Example action",0x46,callback,user_data); // external loading disabled'
    elif snapshot:
        thread='Registry/copied-state query under its SRWLOCK; no native object dereference. Enumeration calls consumers after releasing the registry lock.'
        ownership='Caller owns output copies. Address handles are not generation tokens; a historical v16 address can refer to a newly reused object. Actor snapshots refresh on creation and local-player tick; remote movement is not continuously sampled.'
        errors='Null output/unknown/dead/missing snapshot fails; see exact return type. Actor outputs are zeroed on failure. Component class uses INVALID_SIZE; enumeration returns copied callback count and honors false to stop.'
        example='AnymakerClientActorStateV1 copy{}; bool ok=ctx->get_client_actor_state(event->state.actor,&copy);' if 'actor' in slot else 'AnymakerLocalPlayerStateV2 copy{}; bool ok=ctx->get_local_player_state(&copy);' if slot=='get_local_player_state' else 'bool live=ctx->is_vehicle_component_live(event->component); // no pointer dereference'
    else:
        thread='See production implementation in dinput8_proxy.cpp (inventory stubs in phase27_inventory_observer.inc). UI drawing would require a verified native render scope; currently always unavailable.'
        ownership='No ownership of native actors, textures or game memory is transferred. Caller owns output storage; retained action registration is process-lifetime.'
        errors='Quarantined calls fail or return ANY_INVENTORY_UNAVAILABLE; info functions reject null output. Binding lookups fail for unknown action; setters reject invalid key/action. log returns void.'
        example=f'// Use the exact {slot} signature above; check every result. Quarantined calls must not be retried as native mutations.'
    if slot=='log':
        thread='May be called on native or worker threads; serialized by the logging SRWLOCK. Avoid logging while a registry/installation suspension lock is held.'
        ownership='Strings are borrowed for the duration of the call and written synchronously. No native resource ownership.'
        errors='Returns void; file/open/write exceptions are contained. Absence of a log line does not prove a callback never ran.'
        example='ctx->log(ANY_LOG_INFO,"example","copied snapshot received");'
    if quarantine:
        thread='No enabled native or callback scope: the function rejects the request before native execution.'
    if slot=='is_client_actor_live': example='bool live=ctx->is_client_actor_live(event->state.actor); // address liveness, not historical generation identity'
    if slot=='enumerate_client_actors': example='size_t emitted=ctx->enumerate_client_actors(on_actor_snapshot,user_data); // callback returns bool to continue'
    if slot=='get_vehicle_component_vehicle': example='AnymakerVehicleHandle owner=ctx->get_vehicle_component_vehicle(event->component); // borrowed address; zero means unavailable'
    if slot=='copy_vehicle_component_class': example='char name[128]{}; size_t n=ctx->copy_vehicle_component_class(event->component,name,sizeof(name)); // check INVALID_SIZE'
    info_types={'get_ui_api_info':'AnymakerUiApiInfoV1','get_mod_action_api_info':'AnymakerModActionApiInfoV1','get_client_actor_api_info':'AnymakerClientActorApiInfoV1'}
    if slot in info_types:
        thread='Reads framework state and atomic counters; no native object getter is invoked. Counters are observations, not a single coherent native-world transaction.'
        ownership='Caller owns the output structure. The framework copies headers/counters and transfers no game ownership.'
        errors='false for null output; otherwise fills the declared output version. Readiness counters do not certify a feature is safe.'
        example=f'{info_types[slot]} info{{}}; bool ok=ctx->{slot}(&info);'
    if slot=='is_virtual_key_down':
        thread='Calls Windows GetAsyncKeyState; can be used on the worker. Polling is not an authoritative game input event.'
        errors='false for an out-of-range/unpressed key; a false result is not a full input-error diagnosis.'
        example='bool down=ctx->is_virtual_key_down(0x46); // Windows F key; prefer copied game action events'
    if slot in {'get_mod_action_binding','set_mod_action_binding','register_mod_action'}:
        thread='Framework action/binding locks protect state and persistence. These functions do not insert native Controls rows; external registrations are disabled by the loader policy.'
        ownership='Registered callback/user_data must remain valid for process lifetime. Binding strings/outputs belong to the caller; saved bindings are local TSV state.'
    if slot=='get_mod_action_binding':example='uint32_t key=0; bool ok=ctx->get_mod_action_binding("example","action",&key);'
    if slot=='set_mod_action_binding':example='bool ok=ctx->set_mod_action_binding("example","action",0x46); // registered action required'
    if slot=='enumerate_inventory':example='size_t count=0; auto result=ctx->enumerate_inventory(scope,nullptr,0,&count); // currently UNAVAILABLE'
    if slot=='can_merge_inventory_items':example='bool compatible=false; auto result=ctx->can_merge_inventory_items(scope,destination,source,&compatible); // currently UNAVAILABLE'
    if slot=='request_inventory_merge':example='auto result=ctx->request_inventory_merge(scope,destination,source); // currently UNAVAILABLE; no native write'
    api.append(dict(slot=slot,purpose=purposes[slot],signature=signature,thread=thread,ownership=ownership,errors=errors,example=example,
                    evidence='anymaker_mod_api.h; dinput8_proxy.cpp; '+('phase29_actor_queries.inc; phase29_queries_test' if slot in {'enumerate_client_actors','get_client_actor_state'} else 'API_COVERAGE.json; existing portable regression tests and Phase28 Windows review'),
                    status='QUARANTINED' if quarantine else 'PHASE33_PORTABLE_TESTED_WINDOWS_PENDING' if slot in {'enumerate_client_actors','get_client_actor_state'} else coverage[slot]['status']))
hooks=[]
for slot,(label,symbol,overwrite) in enumerate(specs):
    entry=audit[label];route=routes.get(label)
    stage='Existing observer forwards the original exactly once; payload copies and ordering are retained from Phase28. See the hook body for pre/post-original timing.'
    ownership=('Exact named dispatcher incoming dependency at byte '+str(route['dependency_offset']) if route and route.get('dependency_offset') else 'Unique body or runtime owner/virtual-slot selection; see phase27_runtime_audit.inc, phase27_virtual_owners.h and factory/dispatch provenance. No address-proximity acceptance.')
    hooks.append(dict(slot=slot,label=label,purpose='Observe '+label.replace('_',' ')+' without enabling gameplay mods or mutations.',signature=entry['signature'],
                      symbol=symbol,gcl_index=signature_audit[symbol]['index'],overwrite=int(overwrite),unwind_hex=entry['unwind_hex'],ownership=ownership,
                      thread='Native producer thread recorded per hook by PHASE33_THREAD. First/last/other_thread_calls are observations, not proof of required affinity. Queued consumers run on the worker.',
                      lifetime='Native arguments are borrowed only during the hook. Events contain copied values; raw addresses do not extend lifetime. No runtime unhook/unload.',
                      errors='Reject unresolved/ambiguous target, changed prologue, unsafe paused thread, allocation/protection/unwind failure. Retry worker-side; installed is not exercised.',
                      example='Inspect PHASE33_THREAD slot='+str(slot)+' and PHASE27_HOOK_AUDIT target='+label+' after the combined test.',stage=stage,status='NATIVE_STATIC_VERIFIED_PHASE33_WINDOWS_PENDING'))
assert len(api)==34 and len(hooks)==165
contracts=dict(api_version=16,phase=31,api=api,hooks=hooks)
(root/'PHASE33_CONTRACTS.json').write_text(json.dumps(contracts,indent=2))
manual=['# Phase33 consolidated developer reference','',
 'This is an unfinished framework. Source API v16 retains 34 slots and 165 guarded observer hooks. External mods, injected rendering, Controls insertion and inventory mutation stay disabled.',
 '', 'Read IMPLEMENTATION.md for the six work areas, coordinate/native-layout findings, architecture, unresolved issues and release gates. Read TEST_SEQUENCE.md for the exhaustive Windows diagnostic run with no time limit.',
 '', 'The records below reconcile declarations, implementations and audit inventories. A pending contract is documented as pending; autogenerated completeness does not prove native semantics. Windows/MSVC/runtime results are separate from Linux tests and Windows-target cross-compilation.',
 '', '## API function contracts']
for row in api:
    manual+=['',f'### `{row["slot"]}`','',row['purpose'],'',f'```cpp\n{row["signature"]};\n```','',f'- Thread: {row["thread"]}',f'- Ownership/lifetime: {row["ownership"]}',f'- Errors: {row["errors"]}',f'- Evidence: {row["evidence"]}',f'- Validation status: {row["status"]}','',f'```cpp\n{row["example"]}\n```']
manual+=['','## Hook contracts']
for row in hooks:
    manual+=['',f'### {row["slot"]}: `{row["label"]}`','',row['purpose'],'',f'Native signature: `{row["signature"]}`. Pinned GCL record {row["gcl_index"]}; body symbol `{row["symbol"]}`; overwrite {row["overwrite"]} bytes; unwind `{row["unwind_hex"]}`.',
             '',f'- Dependency ownership: {row["ownership"]}',f'- Thread: {row["thread"]}',f'- Lifetime: {row["lifetime"]}',f'- Scheduling/forwarding: {row["stage"]}',f'- Errors: {row["errors"]}',f'- Example: {row["example"]}',f'- Validation status: {row["status"]}']
(root/'DEVELOPER_MANUAL.md').write_text('\n'.join(manual)+'\n')
print('Generated consolidated contracts for 34 public slots and 165 hooks')
