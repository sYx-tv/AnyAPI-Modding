// Photo mode: while hide_ui is on, sets the game's own g_is_disable_ui, which
// _record_render_commands checks before drawing the HUD and menus. The flag is
// written only while the policy asks for it and cleared once when it stops,
// so the game's own developer toggle keeps working otherwise.
namespace scene_effects {
static std::atomic<uint8_t*> disable_ui{};static bool photo_set{},photo_wanted{};
// g_is_disable_ui is global-address slot 39 of _record_render_commands (end 2600).
static void install_photo(const std::vector<uintptr_t>& record){uint8_t* found{};
 for(auto caller:record){uintptr_t p{};if(!safe_read_memory(caller+2600+39*8,&p,8)||!p)continue;if(found&&found!=(uint8_t*)p){found=nullptr;break;}found=(uint8_t*)p;}
 uint8_t value=2;if(found&&(!safe_read_memory(uintptr_t(found),&value,1)||value>1))found=nullptr;
 disable_ui=found;photo_available=found!=nullptr;
 log_line(found?ANY_LOG_INFO:ANY_LOG_WARN,"framework",found?"SCENE_PHOTO resolved=1":"SCENE_PHOTO unavailable=1");}
// Runs once a frame before the game records its commands, menus included.
// SC-16: the argument is ignored; photo mode needs only an active owner (copy_owned), not the lighting cell.
static void update_photo(const AnySceneEffectsParametersV1&){auto p=copy_owned();bool want=p.enabled&&p.hide_ui;photo_wanted=want;auto flag=disable_ui.load();if(!flag)return;
 if(want==photo_set)return;*flag=want?1:0;photo_set=want;log_line(ANY_LOG_INFO,"framework",want?"SCENE_PHOTO hide_ui=1":"SCENE_PHOTO hide_ui=0");}
// SC-13: photo mode also hides the damage, flash and acid screen-edge overlays: vignette_data.m_is_enabled at
// scene+0xbc0/+0xbe0/+0xc00 (render_scene, 0.1.27 game code), which client_scene.build_scene_render rewrites every frame, so nothing
// needs restoring. Controls calls this from scene_hook (before _build_postprocess reads them) on a writable scene.
static void photo_scene(void* scene){if(!photo_wanted||!scene)return;for(uintptr_t o:{uintptr_t(0xbc0),uintptr_t(0xbe0),uintptr_t(0xc00)}){auto b=(uint8_t*)scene+o;if(*b<=1)*b=0;}}
}
