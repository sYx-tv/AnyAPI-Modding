"""Generates include/anymaker_sdk_symbols.hpp: a versioned table of the symbols mods use most.

    python tools/sdk_codegen.py --json json --out include/anymaker_sdk_symbols.hpp

Mods should depend on the C++ names below, never on raw offsets: regenerating this header after a game
update moves every signature, slot offset, field offset and RVA together, and the static_asserts in
anymaker_sdk_types.hpp catch layout changes. Each entry carries its stable SDK ID in a comment.
A name that no longer exists in the json aborts generation (the update changed the API surface).
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import load_json, write_text  # noqa: E402

FUNCS = {  # C++ name -> exact gcl signature
    'server_tick': '() server.tick (server)',
    'client_tick': '() client.tick (client, const f64, const bool, frontend_ui, settings)',
    'server_scene_create': '() server_scene.create (server_scene, const string, const e_gamemode)',
    'server_scene_destroy': '() server_scene.destroy (server_scene)',
    'client_scene_create': '() client_scene.create (client_scene)',
    'client_scene_destroy': '() client_scene.destroy (client_scene, client)',
    'client_scene_update_ui': '() client_scene.update_ui (client_scene, frontend_ui, settings, const f64, client)',
    'state_manager_queue_state': '() state_manager.queue_state (state_manager, frontend_ui, const ref<state_manager.state_base>, const bool, const bool)',
    'actor_container_get_actor_by_id': '(ptr<server_scene.actor>) server_scene.actor.container.get_actor_by_id (server_scene.actor.container, const s32)',
    'entity_container_get_entity_by_id': '(ptr<server_scene.entity>) server_scene.entity.container.get_entity_by_id (const server_scene.entity.container, const s32)',
    'vehicle_container_get_vehicle_by_id': '(ptr<server_scene.vehicle>) server_scene.vehicle.container.get_vehicle_by_id (const server_scene.vehicle.container, const s32)',
    'entity_get_transform': '(mat34) server_scene.entity.get_transform (const server_scene.entity)',
    'actor_damage': '() server_scene.actor.damage (server_scene.actor, server_scene, const f64, const server_scene.actor_damage_src)',
    'actor_get_inventory': '(ptr<server_scene.inventory>) server_scene.actor.get_inventory (server_scene.actor)',
    'mm_ui_begin': '() mm_ui.begin (mm_ui)',
    'mm_ui_end': '() mm_ui.end (mm_ui)',
    'mm_ui_button': '(bool) mm_ui.button (mm_ui, const string)',
    'audio_manager_play': '() audio_manager.play (audio_manager, const e_audio_effect, const e_audio_group)',
    'server_save_game': '() server.save_game (server, const string)',
    'save_create_scene_from_data': '() server_scene.save_data.create_scene_from_data (server_scene.save_data, server_scene)',
    'inventory_definitions_load': '() inventory_definition_container.load (inventory_definition_container, const file.path, const outfit_resources)',
    'inventory_definitions_get_by_id': '(ptr<inventory_definition>) inventory_definition_container.get_definition_by_id (inventory_definition_container, const string)',
    'client_on_action_digital_begin': '(bool) client.on_action_digital_begin (client, const s32)',
    'client_update_ui_menu_bar': '() client.update_ui_menu_bar (client, frontend_ui)',
    'inventory_definition_file_ctor': '() ctor (inventory_definition_file)',
    'inventory_definition_file_dtor': '() dtor (inventory_definition_file)',
    'inventory_definition_file_load': '(bool) inventory_definition_file.load (inventory_definition_file, const file.path)',
    'inventory_definitions_add_definitions': '() inventory_definition_container._add_definitions (inventory_definition_container, const inventory_definition_file, const outfit_resources)',
}
ENUMS = ['file.e_store', 'e_actor_damage_type', 'e_audio_group', 'state_manager.e_state', 'e_gamemode']
NATIVES = {
    'string_ctor_cstr': '() $string_ctor_cstr (string, const uptr)',
    'string_dtor': '() $string_dtor (string)',
    'string_length': '(s32) string.length (const string)',
    'file_write_string': None,   # resolved by name prefix below
    'file_read_string': None,
    'file_get_is_exists': None,
}
NATIVE_NAMES = {'file_write_string': 'file.write_string', 'file_read_string': 'file.read_string',
                'file_get_is_exists': 'file.get_is_exists'}
GLOBALS = ['g_server', 'g_client', 'g_settings', 'g_state_manager', 'g_ui', 'g_frontend_ui', 'g_audio_manager',
           'g_localization', 'g_renderer', 'g_input']
FIELDS = [('server', 'm_scene'), ('server', 'm_network_state'), ('client', 'm_scene'), ('server_scene', 'm_peers'),
          ('server_scene', 'm_actors'), ('server_scene', 'm_vehicles'), ('server_scene', 'm_entities'),
          ('server_scene', 'm_physics'), ('server_scene', 'm_inventory_definitions'), ('server_scene', 'm_version'),
          ('server_scene.actor', 'm_id'), ('server_scene.actor', 'm_transform'), ('server_scene.actor', 'm_peer_authority'),
          ('server_scene.entity', 'm_id'), ('server_scene.entity', 'm_transform'), ('server_scene.vehicle', 'm_id'),
          ('server_scene.peer', 'm_unique_identifier'), ('server_scene.peer', 'm_authority_actor'),
          ('server_scene.actor_damage_src', 'm_actor_id_src'), ('server_scene.actor_damage_src', 'm_type'),
          ('server_scene.actor_damage_src', 'm_position'), ('server_scene.actor_damage_src', 'm_normal'),
          ('server_scene.actor_damage_src', 'm_hit_flesh_effect')]
SIZES = ['server_scene.actor_damage_src', 'mat34', 'vec3', 'inventory_definition_file', 'file.path', 'outfit_resources']


def cname(s):
    return re.sub(r'[^0-9A-Za-z]', '_', s)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--json', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args(argv)
    ident = load_json(os.path.join(a.json, 'build_identity.json'))
    T = load_json(os.path.join(a.json, 'types.json'))
    F = {f['sig']: f for f in load_json(os.path.join(a.json, 'functions.json'))}
    A = load_json(os.path.join(a.json, 'anchors.json'))
    N = load_json(os.path.join(a.json, 'natives.json'))
    B = {b['sig']: b for b in load_json(os.path.join(a.json, 'native_bindings.json'))} \
        if os.path.exists(os.path.join(a.json, 'native_bindings.json')) else {}
    err = []
    L = ['// Generated by tools/sdk_codegen.py. Do not edit; regenerate after every game update.',
         '#pragma once', '#include <cstdint>', '#include "anymaker_sdk_runtime.hpp"', '',
         'namespace anymaker::sym {', '',
         'inline constexpr const char* steam_build_id = "%s";' % ident['steam']['build_id'],
         'inline constexpr uint32_t game_exe_pe_timestamp = %du;' % ident['files']['game.exe']['pe_timestamp'],
         'inline constexpr uint64_t game_gcl_size = %dull;' % ident['files']['game.gcl']['size'],
         'inline constexpr const char* game_exe_sha256 = "%s";' % ident['files']['game.exe']['sha256'],
         'inline constexpr const char* game_gcl_sha256 = "%s";' % ident['files']['game.gcl']['sha256'], '',
         '// ---- functions (gcl, JIT-loaded). Resolve with anymaker::resolve(desc) / anymaker::cell_of(desc).']
    for k, sig in FUNCS.items():
        f = F.get(sig)
        e = A['functions'].get(sig)
        if not f or not e:
            err.append('function %s: %s' % (k, 'missing' if not f else 'no locator route'))
            continue
        d = e.get('direct', {}).get('signature', '')
        vc = e.get('via_caller')
        vt = e.get('via_typeinfo') or {}
        ch = e.get('chain') or e.get('cell_chain')
        if vc:
            root, hops = vc['anchor_signature'], [vc['slot_offset']]
        elif ch:
            root, hops = ch['root_signature'], ch['hops']
        else:
            root, hops = '', []
        if len(hops) > 8:
            root, hops = '', []
        vslots = [v['slot'] for v in f['vtable_slots'] if v['type'] == (f['owner_type'] or '')]
        L.append('// %s  [%s]' % (f['id'], ', '.join(sorted(e))))
        L.append('inline constexpr func_desc %s{"%s", "%s", {"%s", {%s}, %d}, "%s", %du, %d};' % (
            k, sig.replace('"', '\\"'), d, root, ', '.join('%du' % h for h in hops), len(hops),
            vt.get('anchor_signature', ''), vt.get('slot_offset', 0), vt.get('method_slot', -1)))
        L.append('inline constexpr int %s_vslot = %d;  // zero-based slot in %s\'s method table, -1 if not virtual' % (
            k, vslots[0] if vslots else -1, f['owner_type']))
    L += ['', '// ---- natives (game.exe RVAs). Evidence: metadata (static scan) and/or runtime (call cells).']
    nat_by_sig = {n['sig']: n for n in N}
    for k, sig in NATIVES.items():
        if sig is None:
            nm = NATIVE_NAMES[k]
            cands = [n for n in N if n['name'] == nm and n.get('thunk_rva')]
            if len(cands) != 1:
                err.append('native %s: %d candidates' % (k, len(cands)))
                continue
            n = cands[0]
            sig = n['sig']
        n = nat_by_sig.get(sig, {})
        rva = n.get('thunk_rva') or (B.get(sig) or {}).get('rva') or (n.get('runtime') or {}).get('rva')
        if not rva:
            err.append('native %s: no rva' % k)
            continue
        ev = 'metadata+runtime' if n.get('thunk_rva') and (n.get('runtime') or {}).get('rva') == rva else \
            ('runtime' if not n.get('thunk_rva') else 'metadata')
        L.append('inline constexpr uint32_t %s_rva = %s;  // N:%s [%s]' % (k, rva, sig, ev))
    L += ['', '// ---- globals: address = slot at anchor+slot_offset (anchors.json; runtime-validated layout)']
    for g in GLOBALS:
        an = A['globals'].get(g)
        if not an:
            err.append('global %s: no anchor' % g)
            continue
        L.append('inline constexpr global_desc %s{"%s", "%s", %du};  // G:%s' % (cname(g), g, an['anchor_signature'],
                                                                             an['slot_offset'], g))
    L += ['', '// ---- field offsets (metadata)']
    for t, fn in FIELDS:
        fl = [x for x in T[t]['fields'] if x['name'] == fn] if t in T else []
        if not fl:
            err.append('field %s.%s missing' % (t, fn))
            continue
        L.append('inline constexpr uint32_t off_%s__%s = 0x%x;  // F:%s::%s : %s' % (cname(t), fn, fl[0]['offset'], t, fn,
                                                                                  fl[0]['type']))
    L += ['', '// ---- sizes (metadata)']
    for t in SIZES:
        if t not in T:
            err.append('type %s missing' % t)
            continue
        L.append('inline constexpr uint32_t size_%s = 0x%x;  // T:%s' % (cname(t), T[t]['size'], t))
    L += ['', '// ---- enums (value = position in the recorded name table: static inference, see docs)']
    for e in ENUMS:
        if e not in T:
            err.append('enum %s missing' % e)
            continue
        L.append('namespace %s {  // T:%s' % (cname(e), e))
        for v in T[e]['enum_values']:
            L.append('inline constexpr int32_t %s = %d;' % (cname(v['name']) if not v['name'][:1].isdigit() else '_' + cname(v['name']), v['value']))
        L.append('}')
    L += ['', '}  // namespace anymaker::sym', '']
    if err:
        print('\n'.join(err), file=sys.stderr)
        sys.exit(1)
    write_text(a.out, '\n'.join(L))
    print('wrote', a.out)


if __name__ == '__main__':
    main()
