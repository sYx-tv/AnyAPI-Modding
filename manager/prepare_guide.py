"""Build an offline guide and source-only starter SDK from the current native profile."""
from pathlib import Path
import json,re,hashlib,zipfile,argparse

base=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser();parser.add_argument('--source',default=str(base/'native' if (base/'native').exists() else base/'github_public/native'));parser.add_argument("--output",type=Path);args=parser.parse_args()
source=Path(args.source);sdk_headers=source.parent/'sdk/include';contracts=source.parent/'docs';out=args.output or Path(__file__).resolve().parent/'developer';out.mkdir(parents=True,exist_ok=True)
caps=json.loads((source/'CURRENT_CAPABILITIES.json').read_text(encoding='utf-8-sig'))
manifest=json.loads((source/'BUILD_MANIFEST.json').read_text(encoding='utf-8-sig'))
articles=[]
def add(title,group,body,kind='Guide'):articles.append(dict(Title=title,Group=group,Body=body,Kind=kind))
add('Make your first DLL mod','Start here', '''# Your first mod

1. Install Visual Studio with Desktop development with C++. Include CMake and the Windows SDK.
2. Choose Export starter SDK in this guide, then extract the ZIP.
3. Open the extracted folder in a developer PowerShell and build:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

4. Close Anymaker. Copy build/Release/ExampleMod.dll into AnyAPI and Modding/mods inside the Anymaker Steam folder.
5. Install the compatible API with this manager, choose Play with mods, and check the framework log for example.hello / Loaded.

The starter is source only. It exports AnyAPI_ModInit, checks ABI 1 and writes a startup log. No game files or existing mod DLLs are included.

DLLs are loaded at game startup. Close and restart the game after rebuilding. Hot unloading is unsupported. Keep native game pointers out of the plugin ABI.

The included examples cover current native settings, inventory widgets, item lookup, session mode and screen translation. Each example names its required versioned service.
''')
add('Settings and keybinds','Start here', '''# Make options editable

Mods explicitly register their options. AnyHelpers does not discover arbitrary variables automatically.

Use AnyAPI_ModReady after every plugin has initialized. Query anyhelpers.settings v1 and anyhelpers.controls v1 through AnyAPI_Services()->query. Check for null, the version and struct_size before calling a table.

Settings accept bool, integer, number, choice and text. Register a stable mod_id and setting_id, a readable mod name/label and a default. Read the committed value with get(); your mod must actually use that value in its behavior. Apply commits staged values, while Cancel preserves the last saved state.

Controls accept a stable action_id and default virtual key. register_action returns a token; key(token) returns the committed binding. Use that result in your input callback instead of hard-coding a key. A zero key is unbound.

AnyHelpers supplies these services; base AnyAPI supplies the generic native menu services. Keep defaults when AnyHelpers is absent, as shown in examples/mod_setting.cpp. Your mod can use native menu v3 directly to add independent tabs and typed widgets.

See the AnyHelpers service articles and the two public headers for every field and function.
''')
add('Pick the right service','Start here', '''# What can you build?

Native settings tabs and rows: anyapi.menu v1/v2/v3.
Keyboard, mouse and Unicode text handling: plugin input callbacks, owned input capture and services.input_filter.
Player names, positions and facing: copied player snapshots through host.copy_players.
Immediate menu/inventory suppression: anyapi.ui_state v1.
Game mode checks: anyapi.session v1.
Searchable item and component metadata: anyapi.item_catalog v1.
Item previews: anyapi.item_images v1.
Queued native Give requests with mode masks: anyapi.inventory_actions v1/v2.
Native storage rows, copied grids/items and replicated transfers: anyapi.inventory_ui v1/v2/v3.
Shared inventory root translation: anyapi.screen_layout v1.
Fast in-process drawing: anyapi.gpu_draw v1; the plugin canvas is a fallback drawing path.
GPU shader effects: anyapi.post_process v1 (API 0.26.0 or newer).
Supported game profile and actual file fingerprint status: anyapi.build v1 (API 0.27.0 or newer).
Bounded mod-owned work on the native client tick: anyapi.client_tasks v1 (API 0.27.0 or newer).
Cooperating mod settings and keybinds: anyhelpers.settings v1/v2 and anyhelpers.controls v1.

The Services section includes capabilities, limits, versioned headers and available contract notes. Headers contain the exact signatures; examples show how to use them.

Legacy v16 native hook and action registries are retained as reference but quarantined in this runtime. Seeing an old function name in a header does not mean it is enabled. See Hooks and Legacy for that distinction.
''')
add('Compatibility and validation','Start here',f'''# Know what is supported

This guide describes AnyAPI 0.{manifest['revision']}.0, Anymaker {manifest['game_version']}, Steam build {manifest['steam_build_id']}, Windows x64.

Current services come from the DLL plugin runtime, not the old v16 hook worker. Native contract checks, automated fixtures and author host gameplay acceptance are separate evidence. Joining-client coverage should not be inferred from host tests.

Test each operation on a copied save. Check return values and logs. A queued/SENT Give request confirms event submission, not guaranteed server placement. Road routing uses authored roads, not live obstacles. Preview materials are approximate. Never bypass the installed executable/GCL compatibility guard after an update.

Unsupported: hot unload, arbitrary new main-menu entries, insertion into every vanilla settings category, automatic vanilla key conflict discovery, keyboard chords, and mouse/gamepad rebinding through AnyHelpers.

Game executable SHA-256: {manifest['game_identity_sha256']['game.exe']}
Game GCL SHA-256: {manifest['game_identity_sha256']['bin/game.gcl']}

The guide is versioned with the bundled API. If a newer API is installed, use matching documentation/source before adopting changed signatures.
''')
headers=sorted(p for p in sdk_headers.glob('*.h') if re.match(r'(anyapi_.*_v\d+|anyhelpers_settings_v\d+|mod_controls_v1|anymaker_mod_api|anymaker_mod_extension)\.h$',p.name))
for name,info in caps['base_api'].items():
    body=f'# {name}\n\n'+json.dumps(info,indent=2,ensure_ascii=False)
    add(info.get('service',name)+' · v'+str(info.get('version',info.get('abi',1)))+' · '+name,'Services',body,'Current service')
for name in ('mod_controls_v1.h','anyhelpers_settings_v1.h'):
    add('AnyHelpers · '+('controls v1' if name.startswith('mod_controls') else 'settings v1'),'Services','# '+name+'\n\nRequires AnyHelpers.dll. Explicit registration; optional dependency with fallback defaults.\n\n'+(sdk_headers/name).read_text(encoding='utf-8-sig'),'Mod-provided service')
for p in headers:
    legacy=p.name.startswith('anymaker_')
    prefix='Legacy v16 reference. Native hook/action registry is disabled in this profile.\n\n' if legacy else 'Exact public declarations for the revision shown in this guide.\n\n'
    add(p.name,'Legacy' if legacy else 'Headers','# '+p.name+'\n\n'+prefix+'```cpp\n'+p.read_text(encoding='utf-8-sig')+'\n```','Disabled reference' if legacy else 'Public header')
for p in sorted((p for p in contracts.rglob('*.md') if 'releases' not in p.parts)):
    add(p.stem.replace('_',' ').title(),'Contracts',p.read_text(encoding='utf-8-sig'),'Contract notes')
for p in sorted(source.glob('*.json')):
    if p.name in ('CURRENT_CAPABILITIES.json','BUILD_MANIFEST.json'):continue
    add(p.stem.replace('_',' ').title(),'Contracts','# '+p.name+'\n\nExact native contract/reference snapshot from this API revision.\n\n'+p.read_text(encoding='utf-8-sig'),'Native contract data')
for p in sorted((source/'examples').glob('*.cpp')):add(p.stem.replace('_',' ').title(),'Examples','# '+p.name+'\n\n```cpp\n'+p.read_text(encoding='utf-8-sig')+'\n```','Example')
legacy_hooks=re.findall(r'^\{"([^"\n]+)"', (source/'legacy_hook_specs.h').read_text(),re.M)
add('Legacy hook registry · disabled','Legacy','# Historical native hooks\n\nThese '+str(len(legacy_hooks))+' hook specifications remain in source for audits. The current loader explicitly logs v16_legacy_hooks=DISABLED; this registry is not the active mod API.\n\n'+'\n'.join('- '+h+' — quarantined / not installed by this profile' for h in legacy_hooks),'Disabled reference')
hook_sections=[]
for name in ('anyapi_platform.inc','anyapi_menu.inc','anyapi_settings_tabs.inc','anyapi_screen_layout.inc','anyapi_inventory_ui.inc','anyapi_inventory_actions.inc','native_players.inc'):
    text=(source/name).read_text(encoding='utf-8-sig')
    callbacks=set(re.findall(r'\b([A-Za-z_]\w*(?:_hook|_detour))\s*\(',text))
    if name=='anyapi_platform.inc':callbacks.update(('present','present1','resize','resize1','execute','create','create_hwnd','wndproc'))
    callbacks=sorted(callbacks)
    hook_sections.append(name+':\n'+ ('\n'.join('- '+h for h in callbacks) or '- Validated native dependency reads/events; see this implementation and its contracts.'))
add('Current integration points','Hooks','# Native integration in the active profile\n\nImplementation callbacks below back the current public services. These are framework internals, not permission for a DLL mod to patch the game directly. Use the versioned service tables, ownership rules and native callback context.\n\n'+ '\n\n'.join(hook_sections)+'\n\nGraphics uses presentation callbacks, input uses the owned platform input path, and native settings/storage extensions use guarded native callbacks/dependencies. The individual contract notes describe the validated bodies and offsets.','Implementation reference')
add('Capability snapshot','Contracts','# Current capability manifest\n\nSnapshot from the source used to generate this guide. Historical acceptance fields are retained verbatim. Current acceptance is described in the compatibility page.\n\n'+json.dumps(caps,indent=2,ensure_ascii=False),'Source metadata')
local_docs="""# Local mods and Import DLL

An AnyAPI mod is a Windows x64 DLL exporting AnyAPI_ModInit. Use the starter project and current public headers. A generic DLL cannot be used as an AnyAPI mod.

In Mods, choose Import DLL, or place your DLL in AnyAPI and Modding/mods and select Refresh. The manager discovers local DLLs and disabled DLLs without a GitHub listing. Close the game before changing files. Local mods can be enabled, disabled and removed; their saved settings remain. Disabled files end in .dll.disabled. The native loader discovers lowercase .dll filenames, so import normalizes the extension.

Optional companion metadata: put ExampleMod.anymod.json beside ExampleMod.dll before importing. The manager stores imported details in its receipt. A manually placed companion file is also read while the DLL is enabled. Use this format:

```json
{"Name":"Example Mod","Version":"1.0.0","Description":"My own Anymaker mod.","MinimumApi":25}
```

MinimumApi is enforced. Omit the file if you only need discovery by DLL filename. Import inspects the PE export table without executing the DLL. This verifies its format, not its behavior, ABI implementation or safety.

Local mods show game compatibility as unverified. They may launch with a compatible AnyAPI, but the manager cannot prove they work on every game build. Test them in game. Local mods receive no automatic downloads or updates. Replace a local DLL through Import DLL; replacement keeps a backup. Publish a verified release and add it to catalog.json to provide library downloads and updates. The manager refreshes the catalog; no app code change is needed.

This manager controls DLL files, not mod dependency resolution or custom load order. The current native loader sorts DLL filenames. Settings and keybinds still require explicit registration with AnyHelpers; local discovery does not invent editable options.
"""
add('Local mod import and discovery','Start here',local_docs)
starter='''#include "anyapi_mod_v1.h"
static const AnyModHostV1* host;
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* incoming, AnyModCallbacksV1* callbacks) {
    if (!incoming || !callbacks || incoming->abi != ANYAPI_MOD_ABI || incoming->struct_size != sizeof(AnyModHostV1)) return false;
    host = incoming; *callbacks = {}; callbacks->id = "example.hello";
    if (host->log) host->log(0, callbacks->id, "Loaded");
    return true;
}
'''
cmake='''cmake_minimum_required(VERSION 3.20)
project(ExampleMod LANGUAGES CXX)
add_library(ExampleMod SHARED src/mod.cpp)
target_include_directories(ExampleMod PRIVATE include)
target_compile_features(ExampleMod PRIVATE cxx_std_20)
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "AnyAPI mods require Windows x64")
endif()
'''
sdk=out/'starter-sdk.zip'
with zipfile.ZipFile(sdk,'w',zipfile.ZIP_DEFLATED) as z:
    for p in headers:z.write(p,'include/'+p.name)
    for p in sorted((source/'examples').glob('*.cpp')):z.write(p,'examples/'+p.name)
    for p in sorted((p for p in contracts.rglob('*.md') if 'releases' not in p.parts)):z.write(p,'docs/'+p.relative_to(contracts).as_posix())
    z.writestr('src/mod.cpp',starter);z.writestr('CMakeLists.txt',cmake);z.writestr('README.md',articles[0]['Body']);z.writestr('docs/LOCAL_MODS.md',local_docs);z.writestr('ExampleMod.anymod.json',json.dumps({'Name':'Example Mod','Version':'1.0.0','Description':'My own Anymaker mod.','MinimumApi':manifest['revision']},indent=2))
guide={'Revision':manifest['revision'],'GameVersion':manifest['game_version'],'SteamBuild':manifest['steam_build_id'],'Articles':articles,'HeaderCount':len(headers),'LegacyHookCount':len(legacy_hooks),'SourceHashes':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in headers}}
(out/'guide.json').write_text(json.dumps(guide,ensure_ascii=False,indent=2),encoding='utf-8')
print(f'Built {len(articles)} guide articles, {len(headers)} headers, {len(legacy_hooks)} disabled legacy hook references and a source-only starter SDK.')
