"""Hand-written system map and extension analysis, validated against the generated json.

Every name below (types, functions by name, globals) is checked by sdk_systems.py; an unknown name
aborts generation, so this file cannot silently drift from the game build. Functions are referenced by
name (all overloads); the generator expands them to full signatures and stable IDs.

Status vocabulary for extension questions:
  ready            mechanism identified and every primitive it needs is runtime-validated
  needs_validation mechanism identified from metadata/static analysis; not yet exercised at runtime
  unresolved       a candidate exists but a required step (ownership, ids, sync) is not understood
  closed           no extension point: would need new gcl types, enum values or message ids
  not_present      the game has no such system
"""

# Common mechanisms referenced by many systems ------------------------------------------------------
MECH = {
    'cell_hook': 'Replace the 8-byte call cell of a gcl function (one cell per function; direct calls and '
                 'virtual-dispatch tables read the same cell) with a native thunk, and call the saved original. '
                 'Runtime-validated: 20,119 callees had exactly one cell; 5,072 dispatch-table cells were the call cell; '
                 'cells are in PAGE_READWRITE private memory.',
    'field_write': 'Write a field at its metadata offset on the owning thread, then let the game\'s own code '
                   'propagate it (set_modified for replicated properties).',
    'definition_json': 'Definitions are data-driven JSON in rom/data/*.json loaded by <container>.load; each entry '
                       'names an implementation class that must already exist in game.gcl.',
    'sidecar_file': 'Store mod state in a separate file keyed by the save name (server.save_game receives it); '
                    'the game save format itself is a closed set of type ids.',
}

SYSTEMS = [
    {
        'id': 'world',
        'title': 'Worlds, scenes, entities, transforms and spatial queries',
        'namespaces': ['server_scene', 'client_scene', 'scene', 'physics', 'collision', 'environment_tile', 'biome_manager',
                       'navigation'],
        'roots': {'globals': ['g_server', 'g_client'],
                  'types': ['server', 'client', 'server_scene', 'client_scene', 'server_scene.entity.container',
                            'client_scene.entity.container', 'server_scene.environment_tile_container', 'physics.scene']},
        'key_functions': ['server_scene.create', 'server_scene.destroy', 'server_scene.tick', 'server_scene.pre_tick',
                          'server_scene.post_tick', 'client_scene.create', 'client_scene.destroy', 'client_scene.tick',
                          'server_scene.entity.container.get_entity_by_id', 'server_scene.entity.container.create_entity',
                          'server_scene.entity.container.destroy_entity', 'server_scene.entity.get_transform',
                          'server_scene.actor.container.raycast', 'server_scene.get_spawn_position',
                          'physics.scene.raycast', 'server_scene.peer.container.get_peer_positions'],
        'object_graph': 'g_server (ref<server>) -> server.m_scene (ref<server_scene>) -> m_actors, m_vehicles, m_entities, '
                        'm_props, m_floor_items, m_physics ...; g_client (ref<client>) -> client.m_scene (client_scene, inline '
                        'at +0xf50) -> the replicated mirrors. Objects are addressed by s32 ids through get_*_by_id.',
        'side': 'server_scene.* is authoritative and exists only on the host; client_scene.* exists on every client '
                '(including the host\'s own client) and is rebuilt from replication.',
        'threads': 'server_scene runs on the server loop thread (server_tick -> server.tick -> server_scene.tick); '
                   'client_scene on the main thread (on_update -> client.tick). Tile generation runs on worker tasks '
                   '(server_scene.environment_tile_task.execute, client_scene.environment_tile_task.execute).',
        'extension': {
            'observe': ('needs_validation', 'cell_hook on server_scene.tick / pre_tick / post_tick, '
                        'server_scene.entity.container.tick, server_scene.push_event_tile_loaded.'),
            'change': ('needs_validation', 'Entity fields by offset on the server thread; transforms change through the '
                       'physics layer (physics.* natives), not by writing mat34 fields, or physics overwrites them.'),
            'create_destroy': ('needs_validation', 'server_scene.entity.container.create_entity / destroy_entity with the '
                               'server_scene pointer; ids come from the container. Server thread only.'),
            'register': ('closed', 'Entity kinds are a closed set (e_entity_type) with fixed gcl classes; new kinds need '
                         'new gcl types and a new typeinfo record.'),
            'attach': ('unresolved', 'No per-object user-data slot was found for gcl objects; keep a mod-side map keyed '
                       'by (kind, id) and clear it on server_scene.destroy.'),
            'persist': ('needs_validation', 'sidecar_file'),
            'replicate': ('closed', 'Replicated object types and properties are fixed in gcl; a mod has no channel of its own.'),
        },
    },
    {
        'id': 'players',
        'title': 'Players, characters, health, abilities and interaction',
        'namespaces': ['server_scene.peer', 'client_scene.peer', 'server_peer', 'client_peer', 'server_scene.actor',
                       'server_scene.actor_character', 'client_scene.actor', 'client_scene.actor_character',
                       'client_scene.actor_interactions'],
        'roots': {'globals': ['g_server', 'g_client'],
                  'types': ['server_scene.peer', 'server_peer', 'client_peer', 'server_scene.actor', 'server_scene.actor_character',
                            'client_scene.actor_character', 'client_scene.actor_interactions', 'server_scene.actor.container']},
        'key_functions': ['server_scene.peer.container.get_peer_by_id', 'server_scene.get_peer', 'server_scene.create_peer_actor',
                          'server_scene.actor.container.get_actor_by_id', 'server_scene.actor.container.create_character',
                          'server_scene.actor.damage', 'server_scene.actor.get_is_incapacitated', 'server_scene.actor.use_ability',
                          'server_scene.actor.get_inventory', 'client_scene.actor.container.get_peer_actor',
                          'server_scene.entity.on_interact_begin', 'server_scene.vehicle_component.interact_begin',
                          'server_scene.peer.respawn_authority_actor', 'server_peer_container.get_connected_peer'],
        'object_graph': 'A connected player is a server_peer (network) + server_scene.peer (scene data) + an actor in '
                        'server_scene.m_actors whose authority belongs to that peer. The local player on a client is '
                        'client_scene.actor.container.get_peer_actor.',
        'side': 'Damage, abilities, inventory and authority are server-side. Interaction starts on the client '
                '(client_scene.actor_interactions) and is applied by server_scene.*.interact_begin/end.',
        'threads': 'Server thread for server_scene.actor*; main thread for client_scene.actor*.',
        'extension': {
            'observe': ('needs_validation', 'cell_hook on server_scene.actor.damage (and overrides, e.g. '
                        'server_scene.actor_character.damage: the hook must cover every override it cares about), '
                        'server_scene.create_peer_actor, server_peer_container.handle_peer_disconnect.'),
            'change': ('needs_validation', 'Call server_scene.actor.damage with a damage source for health changes; '
                       'direct field writes skip replication events.'),
            'create_destroy': ('needs_validation', 'server_scene.actor.container.create_character / create_creature / '
                               'create_zombie and destroy_actor on the server thread.'),
            'register': ('closed', 'Abilities and actor classes are fixed gcl code paths; new ones need new gcl types.'),
            'attach': ('unresolved', 'Key mod data by actor id or peer id; no user-data slot on gcl actors.'),
            'persist': ('needs_validation', 'sidecar_file keyed by peer identity (server_scene.peer.container.get_object '
                        'takes the string key the game uses for players).'),
            'replicate': ('closed', 'Peer data events (client_peer.data.*) are fixed types.'),
        },
    },
    {
        'id': 'items',
        'title': 'Items, inventories and equipment (no crafting system present)',
        'namespaces': ['server_scene.item_world', 'server_scene.item', 'server_scene.inventory', 'server_scene.inventory_hotbar',
                       'server_scene.item_floor', 'client_scene.item_world', 'inventory_definition', 'inventory_item_util',
                       'frontend_ui_inventory'],
        'roots': {'globals': [],
                  'types': ['server_scene.inventory', 'server_scene.item_world', 'server_scene.item_world_ref',
                            'server_scene.item_floor_container', 'inventory_definition', 'inventory_definition_container']},
        'key_functions': ['server_scene.inventory.get_items', 'server_scene.inventory.try_store_item',
                          'server_scene.inventory.destroy_item', 'server_scene.inventory.equip_handheld',
                          'server_scene.inventory.equip_wearable', 'server_scene.inventory.pick_up_item',
                          'server_scene.item_world.create_item', 'server_scene.item_world.modify_stack_count',
                          'server_scene.get_world_item_unique_id', 'inventory_definition_container.create_definition',
                          'inventory_definition_container.load'],
        'object_graph': 'actor -> server_scene.actor.get_inventory -> server_scene.inventory (wearable slots e_wearable_slot, '
                        'handheld slot, hotbar). Items are server_scene.item_world objects referenced through item_world_ref; '
                        'their behaviour class comes from inventory_definition.class (rom/data/inventory_definitions.json).',
        'side': 'Server-authoritative. client_scene.item_world mirrors them for rendering and UI.',
        'threads': 'Server thread for mutation; UI reads on the main thread.',
        'extension': {
            'observe': ('needs_validation', 'cell_hook on server_scene.inventory.pick_up_item / try_store_item / '
                        'destroy_item and server_scene.item_world.on_use_*.'),
            'change': ('needs_validation', 'server_scene.item_world.modify_stack_count; equip/unequip functions.'),
            'create_destroy': ('needs_validation', 'server_scene.item_world.create_item then try_store_item or drop to '
                               'server_scene.item_floor_container; server_scene.inventory.destroy_item returns the ref to release.'),
            'register': ('needs_validation', 'definition_json: after inventory_definition_container.load returns, add '
                         'definitions (existing classes only) with create_definition or _add_definitions, then let the game '
                         'assign ids (reset_assigned_ids is the static candidate). Every peer must load the same definitions '
                         'because saves and replication use definition ids/indices.'),
            'attach': ('unresolved', 'Key by item world id (server_scene.get_world_item_unique_id).'),
            'persist': ('needs_validation', 'sidecar_file keyed by item id; ids are remapped on load '
                        '(server_scene.save_data.id_map), so store the mapping you see after load.'),
            'replicate': ('closed', 'No mod channel; item state replicates only through existing properties.'),
        },
        'not_present': ['crafting: no recipe, craft, fabricator, workbench or assembly types or functions exist in this build '
                        '(name search; static).'],
    },
    {
        'id': 'vehicles',
        'title': 'Vehicles, parts, components, physics and mechanical systems',
        'namespaces': ['server_scene.vehicle', 'server_scene.vehicle_component', 'server_scene.vehicle_element',
                       'client_scene.vehicle', 'client_scene.vehicle_component', 'vehicle_component_definition',
                       'vehicle_element_definition', 'vehicle_util', 'vehicle_buoyancy', 'server_scene.vehicle_torque_node'],
        'roots': {'globals': ['vehicle_buoyancy.g_parameters'],
                  'types': ['server_scene.vehicle', 'server_scene.vehicle.container', 'server_scene.vehicle_component',
                            'vehicle_component_definition', 'vehicle_component_definition_container', 'physics.object']},
        'key_functions': ['server_scene.vehicle.container.get_vehicle_by_id', 'server_scene.vehicle.container.create',
                          'server_scene.vehicle.container.destroy_vehicle', 'server_scene.vehicle.add_component',
                          'server_scene.vehicle_component.create_component_class', 'server_scene.vehicle.damage_functional',
                          'server_scene.vehicle_torque_node.apply_torque', 'server.load_vehicle_group_json',
                          'server.destroy_vehicle_by_id', 'server_scene.vehicle_component.get_data_f64'],
        'object_graph': 'server_scene.m_vehicles -> vehicle (grids, plates, edges, nodes, links) -> vehicle_component '
                        'instances whose class comes from vehicle_component_definition.class.',
        'side': 'Server-authoritative simulation; client_scene.vehicle is a replicated, interpolated mirror '
                '(client_scene.vehicle_task runs on the Client Vehicle Worker).',
        'threads': 'Server thread; client vehicle integration on a worker task (client_scene.vehicle_task.execute).',
        'extension': {
            'observe': ('needs_validation', 'cell_hook on server_scene.vehicle.container.tick, vehicle_component.tick overrides.'),
            'change': ('needs_validation', 'Component data getters/setters (get_data_*), torque node setters.'),
            'create_destroy': ('needs_validation', 'server.load_vehicle_group_json spawns from a vehicle JSON; '
                               'server.destroy_vehicle_by_id removes one.'),
            'register': ('needs_validation', 'definition_json for new components that reuse an existing class '
                         '(vehicle_component_definitions.json "class"); new behaviour classes are closed.'),
            'attach': ('unresolved', 'Key by vehicle id + component index.'),
            'persist': ('needs_validation', 'sidecar_file'),
            'replicate': ('closed', ''),
        },
    },
    {
        'id': 'networks',
        'title': 'Electrical, fluid, logic and programmable systems',
        'namespaces': ['server_scene.connections_manager', 'server_scene.vehicle_electric_node', 'server_scene.vehicle_liquid_node',
                       'server_scene.vehicle_gas_node', 'server_scene.vehicle_data_node', 'server_scene_script',
                       'frontend_ui_microcontroller'],
        'roots': {'globals': [], 'types': ['server_scene.connections_manager', 'server_scene.vehicle_electric_node',
                                           'server_scene.vehicle_liquid_node', 'server_scene.vehicle_gas_node',
                                           'server_scene.vehicle_data_node']},
        'key_functions': ['server_scene.connections_manager.build', 'server_scene.connections_manager.electric.add_connection',
                          'server_scene.connections_manager.liquid.add_connection', 'server_scene.connections_manager.data.add_connection',
                          'server_scene.vehicle_electric_node.consume_factor', 'server_scene.vehicle_liquid_node.set_static_pressure',
                          'server_scene_script.element.run', 'server_scene_script.element.resolve'],
        'object_graph': 'server_scene.m_connections_manager holds electric/liquid/gas/data/mechanical/torque/ammo connection '
                        'graphs between vehicle nodes; microcontrollers run server_scene_script element trees.',
        'side': 'Server only.', 'threads': 'Server thread.',
        'extension': {
            'observe': ('needs_validation', 'cell_hook on node consume/produce functions and script element run.'),
            'change': ('needs_validation', 'Node setters (set_content, set_static_pressure, consume_*).'),
            'create_destroy': ('needs_validation', 'Connections are created by vehicle edits (vehicle.add_*_link).'),
            'register': ('closed', 'Script element types and data types (e_script_data_type, e_script_element_type) are '
                         'closed enums.'),
            'attach': ('unresolved', ''),
            'persist': ('needs_validation', 'sidecar_file'),
            'replicate': ('closed', ''),
        },
    },
    {
        'id': 'definitions',
        'title': 'Definitions, content registration and asset loading',
        'namespaces': ['inventory_definition_container', 'vehicle_component_definition_container', 'creature_definition_container',
                       'zombie_definition_container', 'prop_definition_container', 'dungeon_tile_definition_container',
                       'environment_tile_definition_container', 'localization', 'json', 'file'],
        'roots': {'globals': [], 'types': ['inventory_definition_container', 'vehicle_component_definition_container',
                                           'creature_definition_container', 'prop_definition_container']},
        'key_functions': ['inventory_definition_container.load', 'inventory_definition_container.create_definition',
                          'inventory_definition_container._add_definitions', 'inventory_definition_container.reset_assigned_ids',
                          'creature_definition_container._add_definition', 'creature_definition_container._post_load',
                          'creature_definition_container.load', 'creature_definition_container.reset_assigned_ids',
                          'application.graphics.create_asset_texture', 'application.graphics.create_asset_mesh',
                          'file.read_string', 'json.parser.read_string'],
        'object_graph': 'server_scene.m_inventory_definitions, m_component_definitions, m_creature_definitions, '
                        'm_zombie_definitions, m_prop_definitions are loaded from rom/data/*.json at server_scene.create.',
        'side': 'Definition containers live inside server_scene; clients load the same rom data for rendering/UI.',
        'threads': 'Loaded on the thread that creates the scene (static: during server_scene.create / tick_loading).',
        'extension': {
            'observe': ('needs_validation', 'cell_hook on <container>.load and _post_load.'),
            'change': ('needs_validation', 'get_definition_by_id then edit fields right after <container>.load returns, '
                       'before objects are created from them.'),
            'create_destroy': ('needs_validation', '<container>.create_definition / remove_definition.'),
            'register': ('needs_validation', 'definition_json; ids are assigned (reset_assigned_ids, _post_load). Every '
                         'peer must register identical definitions in identical order.'),
            'attach': ('unresolved', ''),
            'persist': ('needs_validation', 'Saves reference definitions through server_scene.save_data.definition_table; '
                        'a save made with a mod definition will not load without it.'),
            'replicate': ('closed', 'Definitions are not replicated; they must match on every machine.'),
        },
    },
    {
        'id': 'rendering',
        'title': 'Rendering, cameras, materials, textures, shaders and lighting',
        'namespaces': ['renderer', 'scene_renderer', 'application.graphics', 'render_util', 'client_scene.lights'],
        'roots': {'globals': ['g_renderer', 'app.graphics'], 'types': ['renderer', 'scene_renderer', 'application.graphics',
                                                                       'application.graphics.camera']},
        'key_functions': ['renderer.render', 'client_scene.build_scene_render', 'client_scene.get_camera',
                          'client_scene.get_camera_transform', 'client_scene.lights.push_omni_light',
                          'client_scene.lights.push_spot_light', 'scene_renderer.push_omni_light',
                          'application.graphics.create_render_pipeline', 'application.graphics.create_asset_texture',
                          'application.graphics.camera.get_ray'],
        'object_graph': 'g_renderer owns ~150 pipeline_* globals (each a ref<render_pipeline>); client_scene.build_scene_render '
                        'fills a scene_renderer each frame; renderer._record_commands_* record D3D12 work through '
                        'application.graphics.cmd_* natives.',
        'side': 'Client only.', 'threads': 'Main thread (on_render).',
        'extension': {
            'observe': ('needs_validation', 'cell_hook on client_scene.build_scene_render / renderer.render.'),
            'change': ('needs_validation', 'Camera via client_scene fields (m_is_fov_override, m_fov_override); lights '
                       'by calling client_scene.lights.push_* during build_scene_render.'),
            'create_destroy': ('unresolved', 'Pipelines are created from compiled shader libraries (rom *.slib) by '
                               'application.graphics.create_render_pipeline; shader source and binding layouts are unknown.'),
            'register': ('closed', 'Materials are per-pipeline constant buffers; no material registry.'),
            'attach': ('unresolved', ''),
            'persist': ('not_applicable', ''),
            'replicate': ('not_applicable', ''),
        },
    },
    {
        'id': 'ui',
        'title': 'UI construction, layout, widgets, input routing and localization',
        'namespaces': ['mm_ui', 'client_ui', 'client_ui_element', 'frontend_ui', 'localization', 'main_menu'],
        'roots': {'globals': ['g_ui', 'g_frontend_ui', 'g_localization', 'g_main_menu'],
                  'types': ['mm_ui', 'client_ui', 'frontend_ui', 'localization']},
        'key_functions': ['mm_ui.begin', 'mm_ui.end', 'mm_ui.button', 'mm_ui.begin_menu_bar', 'client_ui.begin', 'client_ui.button',
                          'client_scene.update_ui', 'server.update_ui_menu_bar', 'client.update_ui_menu_bar',
                          'localization.get_localization', 'localization.add_localization'],
        'object_graph': 'Two immediate-mode toolkits: mm_ui (g_ui; menus, debug and editor panels) and client_ui (HUD and '
                        'frontend, owned by frontend_ui/client.ui_data). Widgets are declared every frame inside update_ui '
                        'calls, so "create" means "emit during the frame" and cleanup is simply not emitting.',
        'side': 'Client only.', 'threads': 'Main thread.',
        'extension': {
            'observe': ('needs_validation', 'cell_hook on client_scene.update_ui / frontend_ui update functions.'),
            'change': ('needs_validation', 'Emit extra widgets from inside a hooked update_ui call (between the toolkit\'s '
                       'begin/end).'),
            'create_destroy': ('needs_validation', 'Immediate mode: emit each frame; stop emitting to remove.'),
            'register': ('closed', 'Localization strings are a closed enum (e_localization_string); mods must carry their '
                         'own strings.'),
            'attach': ('not_applicable', ''),
            'persist': ('needs_validation', 'Mod settings in a mod-owned file.'),
            'replicate': ('not_applicable', ''),
        },
    },
    {
        'id': 'audio',
        'title': 'Audio creation, playback and spatial audio',
        'namespaces': ['audio_manager', 'audio_effect_single', 'audio_effect_container', 'application.audio'],
        'roots': {'globals': ['g_audio_manager'], 'types': ['audio_manager', 'audio_effect_single']},
        'key_functions': ['audio_manager.play', 'audio_manager.set_listener', 'audio_effect_single.update_play',
                          'audio_effect_single.set_position_velocity', 'application.audio.play', 'application.audio.load_library',
                          'client_scene.filter_positional_audio'],
        'object_graph': 'g_audio_manager plays e_audio_effect entries from a loaded library (rom/audio *.ogg). Positional '
                        'effects are audio_effect_single objects updated each frame.',
        'side': 'Client only.', 'threads': 'Main thread.',
        'extension': {
            'observe': ('needs_validation', 'cell_hook on audio_manager.play.'),
            'change': ('needs_validation', 'audio_effect_single setters.'),
            'create_destroy': ('needs_validation', 'audio_effect_single.update_play each frame / end.'),
            'register': ('closed', 'Effects are a closed enum (e_audio_effect); custom sounds would need the native '
                         'application.audio layer (load_library) whose ownership rules are unknown.'),
            'attach': ('not_applicable', ''), 'persist': ('not_applicable', ''), 'replicate': ('not_applicable', ''),
        },
    },
    {
        'id': 'input',
        'title': 'Input actions, bindings and device handling',
        'namespaces': ['input', 'application.input', 'settings.controls'],
        'roots': {'globals': ['g_input', 'g_settings'], 'types': ['input.e_action_digital', 'input.e_action_axis',
                                                                   'settings.controls']},
        'key_functions': ['on_action_digital', 'on_action_axis', 'on_input_pointer', 'on_input_character',
                          'client.on_action_digital_begin', 'client.on_action_digital_end', 'client.on_action_axis',
                          'input.create_input_bindings', 'application.input.create_action_digital',
                          'application.input.get_digital', 'application.input.set_is_mouse_captured'],
        'object_graph': 'game.exe calls the gcl entry points on_action_digital/on_action_axis with action ids '
                        '(input.e_action_digital / e_action_axis) created by input.create_input_bindings.',
        'side': 'Client only.', 'threads': 'Main thread.',
        'extension': {
            'observe': ('needs_validation', 'cell_hook on client.on_action_digital_begin/end and on_action_axis.'),
            'change': ('needs_validation', 'Return early from a hooked handler to consume an action.'),
            'create_destroy': ('unresolved', 'application.input.create_action_digital can create actions natively, but the '
                               'action id space and rebinding UI are owned by the game (closed enums).'),
            'register': ('closed', 'Action enums are closed; raw Win32 input in the mod is the portable alternative.'),
            'attach': ('not_applicable', ''), 'persist': ('needs_validation', 'Mod-owned bindings file.'),
            'replicate': ('not_applicable', ''),
        },
    },
    {
        'id': 'network',
        'title': 'Networking, replication, messages, authority and multiplayer lifecycle',
        'namespaces': ['replication', 'replication_stream', 'messages_server_to_client', 'messages_client_to_server',
                       'application.network', 'server_peer', 'client_peer', 'binary'],
        'roots': {'globals': ['g_server', 'g_client'], 'types': ['server', 'client', 'server_peer_container',
                                                                  'client_peer_container', 'replication.master.object',
                                                                  'messages_server_to_client.e_type']},
        'key_functions': ['server.handle_message', 'client.handle_message', 'server.on_network_event', 'client.on_network_event',
                          'server.send_message_chat_message', 'client.send_message_chat_message',
                          'server.send_message_replicate_scene', 'server_peer_container.connect_peer_pending',
                          'server_peer_container.handle_peer_disconnect', 'replication.master.object.set_modified',
                          'server_scene.replicate', 'client_scene.replicate'],
        'object_graph': 'One process can host: g_server owns the authoritative server_scene and an application.network.host; '
                        'g_client connects to it (also locally). Server->client: replicate_scene / replicate_scene_stream '
                        'messages carrying replication.master deltas; client->server: a small closed message set.',
        'side': 'Explicit: server.* host only, client.* every player.',
        'threads': 'server.* on the server loop thread, client.* on the main thread.',
        'extension': {
            'observe': ('needs_validation', 'cell_hook on server.handle_message / client.handle_message and on_network_event.'),
            'change': ('needs_validation', 'Server-side state changes replicate automatically if they go through '
                       'replication.master property setters or set_modified.'),
            'create_destroy': ('not_applicable', ''),
            'register': ('closed', 'Message types (messages_*.e_type) and replicated object types are closed enums; '
                         'unknown ids would desync or disconnect vanilla peers.'),
            'attach': ('unresolved', 'Chat messages are the only free-form string channel both ways; using them for mod '
                       'data would be visible to vanilla players and is not recommended.'),
            'persist': ('not_applicable', ''),
            'replicate': ('closed', 'No mod channel in the protocol. A separate mod transport (own socket/Steam P2P '
                          'channel) is outside the game.'),
        },
    },
    {
        'id': 'save',
        'title': 'Save/load, serialization, persistence and schema/version handling',
        'namespaces': ['server_scene.save_data', 'client_scene.save_data', 'binary', 'json', 'file'],
        'roots': {'globals': ['client_scene.save_data.G_EXTENSION_DATA', 'client_scene.save_data.G_EXTENSION_META'],
                  'types': ['server_scene.save_data', 'server_scene.save_data.parser', 'server_scene.save_data.type_id',
                            'server_scene.save_data.definition_table']},
        'key_functions': ['server.save_game', 'server._save_game', 'server.autosave_game',
                          'server_scene.save_data.create_data_from_scene', 'server_scene.save_data.create_scene_from_data',
                          'server_scene.save_data.parse_save_data', 'server_scene.actor.parse_save_data',
                          'server_scene.save_data.id_map.get_mapped_id', 'file.write_string', 'file.read_string'],
        'object_graph': 'One parse_save_data(parser) method per type reads or writes depending on the parser mode '
                        '(e_parse_mode). Polymorphic objects store a save_data.type_id and are recreated with '
                        '<base>.save_data.create_object. server_scene.m_version is the scene schema version.',
        'side': 'Host only (server).', 'threads': 'Server thread; the file write runs in server_scene.save_data.save_game_task_data.save.',
        'extension': {
            'observe': ('needs_validation', 'cell_hook on server.save_game and server_scene.save_data.create_scene_from_data.'),
            'change': ('needs_validation', 'Modify scene state before create_data_from_scene.'),
            'create_destroy': ('not_applicable', ''),
            'register': ('closed', 'Save type ids are a closed set; injecting unknown objects breaks vanilla loads.'),
            'attach': ('needs_validation', 'sidecar_file next to the save (the save name is the string argument of '
                       'server.save_game).'),
            'persist': ('needs_validation', 'sidecar_file, written after server.save_game returns and read after '
                        'create_scene_from_data, with your own schema version field.'),
            'replicate': ('not_applicable', ''),
        },
    },
    {
        'id': 'ticks',
        'title': 'Updates, ticks, callbacks, events and task scheduling',
        'namespaces': ['state_manager', 'system', 'server_scene.event', 'replication.master.object_events'],
        'roots': {'globals': ['g_state_manager', 'g_debug_render_server_mutex'], 'types': ['state_manager', 'system.mutex']},
        'key_functions': ['on_create', 'on_destroy', 'on_update', 'on_render', 'server_tick', 'server.tick', 'client.tick',
                          'state_manager.tick', 'state_manager.queue_state', 'server_scene.push_event_scene_explosion',
                          'system.mutex.lock', 'system.mutex.unlock', 'system.work',
                          'server_scene.environment_tile_task.execute', 'client_scene.vehicle_task.execute'],
        'object_graph': 'game.exe drives on_create/on_update/on_render/on_destroy on the main thread and server_tick on '
                        'its own thread; state_manager switches menu/load/game/unload states. Server events are '
                        'replicated objects pushed with push_event_* and consumed by client.on_event.',
        'side': 'Both.', 'threads': 'main: on_update/on_render/client.*; server: server_tick/server.*; workers: *_task.execute.',
        'extension': {
            'observe': ('needs_validation', 'cell_hook on on_update/server.tick/state_manager.queue_state.'),
            'change': ('not_applicable', ''),
            'create_destroy': ('needs_validation', 'Mod work queue drained from a hooked server.tick (server work) or '
                               'client.tick (main-thread work).'),
            'register': ('closed', 'Event types are closed.'),
            'attach': ('not_applicable', ''), 'persist': ('not_applicable', ''), 'replicate': ('closed', ''),
        },
    },
]

CAPABILITIES = [
    # id, capability, systems, functions, types, globals, thread, authority, lifetime, status, note
    ('build.guard', 'Detect the game build and refuse to run on a mismatch', ['all'], [], [], [],
     'any', 'n/a', 'process', 'ready',
     'Compare game.exe PE timestamp/size and game.gcl hash with build_identity.json before resolving anything.'),
    ('locate.function', 'Find a gcl function entry point', ['all'], [], [], [], 'any (after on_create)', 'n/a', 'process',
     'ready', 'Code signature scan of MEM_PRIVATE executable memory at 64-byte steps, or slot->cell of a located caller.'),
    ('locate.global', 'Resolve a global (managers, settings, definitions)', ['all'], [],
     [], ['g_server', 'g_client', 'g_settings', 'g_state_manager', 'g_ui', 'g_renderer', 'g_audio_manager'],
     'any (after on_create)', 'n/a', 'process', 'ready', 'Global slot of an anchor function (anchors.json).'),
    ('locate.native', 'Call or hook a game.exe native', ['all'], [], [], [], 'depends on native', 'n/a', 'process', 'ready',
     'game.exe base + RVA (natives.json static, native_bindings.json runtime-confirmed).'),
    ('hook.function', 'Observe or replace a gcl function (direct and virtual calls)', ['all'], [], [], [],
     'the thread that calls it', 'n/a', 'until restored', 'ready',
     'Cell hook: swap the 8-byte cell value; keep the original for calling through. Runtime: calltest hooked '
     'state_manager.tick for 5 s (300 calls, called through, restored).'),
    ('call.function', 'Call a gcl function or native from native code', ['all'], [], [], [], 'owner thread of its data', 'n/a',
     'call', 'ready', 'gc-x64 ABI: pointer args, hidden return pointer in rcx. Runtime: calltest called 4 natives, '
     '3 JIT functions and application.get_version correctly. Exception: $-prefixed runtime helpers take scalars by value.'),
    ('world.lookup_entity', 'Resolve the server scene and look up an entity, actor or vehicle by id', ['world', 'players', 'vehicles'],
     ['server_scene.entity.container.get_entity_by_id', 'server_scene.actor.container.get_actor_by_id',
      'server_scene.vehicle.container.get_vehicle_by_id'], ['server', 'server_scene'], ['g_server'],
     'server', 'host only', 'pointer valid until the object is destroyed; re-resolve every tick', 'needs_validation',
     'Field-walk g_server -> m_scene (ref +0x08) -> container field, then call get_*_by_id.'),
    ('world.read_transform', 'Read an object transform', ['world'], ['server_scene.entity.get_transform'],
     ['mat34'], [], 'owner thread', 'any', 'copy the mat34', 'needs_validation', ''),
    ('players.damage', 'Apply damage / healing to an actor', ['players'], ['server_scene.actor.damage'],
     ['server_scene.actor_damage_src'], [], 'server', 'host only', 'call', 'needs_validation',
     'Construct actor_damage_src on the stack with the documented layout; the function is virtual.'),
    ('items.give', 'Create an item and give it to a player', ['items'],
     ['server_scene.item_world.create_item', 'server_scene.inventory.try_store_item'], ['server_scene.item_world_ref'], [],
     'server', 'host only', 'ref ownership passes to the inventory', 'unresolved',
     'ref<item_world> ownership transfer through $ref_* natives is runtime-resolved but not exercised.'),
    ('definitions.register', 'Register a new item/component/prop definition', ['definitions', 'items', 'vehicles'],
     ['inventory_definition_container.create_definition', 'inventory_definition_container._add_definitions',
      'inventory_definition_container.reset_assigned_ids', 'inventory_definition_container.load'], ['inventory_definition'], [], 'scene creation', 'all peers identically',
     'scene lifetime', 'needs_validation', 'Existing implementation classes only.'),
    ('ui.immediate_widget', 'Draw a widget/panel', ['ui'], ['mm_ui.begin', 'mm_ui.button', 'mm_ui.end', 'client_scene.update_ui'],
     ['mm_ui'], ['g_ui'], 'main', 'local', 'per frame', 'needs_validation', ''),
    ('ui.native_overlay', 'Draw an overlay without game UI code', ['ui', 'rendering'], [], [], [], 'present thread', 'local',
     'per frame', 'needs_validation', 'DXGI/D3D12 composition outside the game code. The AnyAPI loader (Phase34 ABI 1) '
     'documents this as user-confirmed in gameplay; not re-validated by this SDK.'),
    ('audio.play', 'Play a built-in sound', ['audio'], ['audio_manager.play'], ['audio_manager'], ['g_audio_manager'],
     'main', 'local', 'fire and forget', 'needs_validation', ''),
    ('input.observe_action', 'Observe/consume game actions', ['input'],
     ['client.on_action_digital_begin', 'client.on_action_digital_end', 'client.on_action_axis'], [], [], 'main', 'local',
     'per event', 'needs_validation', ''),
    ('ticks.schedule', 'Run mod work on the server or main thread', ['ticks'], ['server.tick', 'client.tick'], [], [],
     'server / main', 'n/a', 'queued', 'needs_validation', 'Lock-free queue drained in hooked ticks.'),
    ('lifecycle.world', 'Know when a world is created/destroyed', ['ticks', 'world'],
     ['server_scene.create', 'server_scene.destroy', 'client_scene.create', 'client_scene.destroy', 'state_manager.queue_state'],
     [], ['g_state_manager'], 'server / main', 'n/a', 'per world', 'needs_validation', ''),
    ('save.sidecar', 'Persist mod state with a save', ['save'], ['server.save_game', 'server_scene.save_data.create_scene_from_data',
                                                                'file.write_string', 'file.read_string'], [], [],
     'server', 'host only', 'per save', 'needs_validation', ''),
    ('net.custom_channel', 'Send mod data between players', ['network'], [], [], [], '-', '-', '-', 'unresolved',
     'Closed protocol; no in-game channel identified.'),
    ('render.custom_pipeline', 'Add a render pass / material', ['rendering'], ['application.graphics.create_render_pipeline'],
     [], ['g_renderer'], 'main', 'local', '-', 'unresolved', 'Shader library format unknown.'),
]
