// Glass struts: a strut painted the glass colour is drawn see-through, with the same tint as the stock windows, so
// a cockpit can be framed with struts you can see out of. Only the look changes: the strut keeps its collision,
// strength and replicated paint, so players without the mod see a strut in that paint colour.
// The edge mesh builder (vehicle_edge_util.add_static_geometry) adds every strut face through
// vehicle_plates_edges_builder.add_edge_triangle; while it builds a glass strut, those faces go to the builder's
// window glass list instead (add_triangle_glass, the call window plates make), tinted by get_window_glass_color(intact).
// Curved windows (setting, on by default): vehicle_plate_util.get_is_suitable_for_window is the window shape rule, run only
// when a window plate is placed or a plate is switched to window (the tool on the client, the event handler on the host).
// It is answered yes for any plate shape. Saves do not re-check it, so the windows load in the unmodded game too.
// Hidden struts: a strut painted kHiddenColor is not drawn at all (it still holds plates and collides), so two windows can
// meet at a bend with nothing between them. Frameless windows (setting, on by default): while vehicle_plate_util.add_static_geometry
// builds an intact window plate, its frame and rim faces (add_plate_triangle) go to the glass list too, so the glass runs edge to edge.
#include "anymaker_sdk_runtime.hpp"
#include "route_cell.h"
#include "anyhelpers_bindings.h"
#include <atomic>
namespace glass {
constexpr int32_t kHiddenColor=68;  // C68, the pale green (185,206,172): hidden strut
constexpr int32_t kGlassColor=70;   // palette index (rom/textures/color_palette.txtr) that marks a glass strut: the pale aqua (126,186,181)
struct Color8 {uint8_t r,g,b,a;};
using StaticGeometryFn=void(*)(const void*,const void*,const int32_t*,const int32_t*,void*);
using EdgeTriangleFn=void(*)(void*,const void*,const void*,const void*,const void*,const int32_t*);
using GlassTriangleFn=void(*)(void*,const void*,const void*,const void*,const void*,const Color8*);
using GlassColorFn=void(*)(Color8*,const int32_t*);
using PlateGeometryFn=void(*)(const void*,const void*,const int32_t*,const int32_t*,const void*,const int32_t*,const int32_t*,const int32_t*,const int32_t*,const void*,void*);
using SuitableFn=void(*)(bool*,const void*,const void*,const int32_t*,const void*);
static anymaker::cell_hook h_static,h_triangle,h_window,h_plate,h_plate_triangle;static uint64_t curved_setting,frameless_setting;static std::atomic<uint64_t> curved_allowed{};
static GlassTriangleFn glass_triangle;static GlassColorFn glass_color;
static std::atomic<bool> ready{},stop{};
static thread_local bool building_glass{},building_frameless{};static thread_local Color8 tint{},window_tint{};
static std::atomic<uint64_t> seen[4]{};   // palette indices 0 to 255 seen on struts, logged once each
static std::atomic<uint64_t> glass_struts{};

static void hk_static(const void* node_0,const void* node_1,const int32_t* color,const int32_t* size,void* builder){
 auto original=(StaticGeometryFn)h_static.original;int32_t c=color?*color:-1;
 if(c>=0&&c<256){uint64_t bit=1ull<<(c&63);if(!(seen[c>>6].fetch_or(bit)&bit))log("GLASS_STRUT_COLOR_SEEN index="+std::to_string(c)+(c==kGlassColor?" glass=1":""));}
 if(c==kHiddenColor&&ready)return;
 if(c!=kGlassColor||!ready){original(node_0,node_1,color,size,builder);return;}
 // Half way from the window tint to clear: a strut is thick and seen through two faces, so the window tint alone reads too dark.
 int32_t intact=0;glass_color(&tint,&intact);for(uint8_t* ch:{&tint.r,&tint.g,&tint.b})*ch=uint8_t(*ch+(255-*ch)/2);
 if(!glass_struts.fetch_add(1))log("GLASS_STRUT_FIRST tint="+std::to_string(tint.r)+","+std::to_string(tint.g)+","+std::to_string(tint.b)+","+std::to_string(tint.a));
 building_glass=true;original(node_0,node_1,color,size,builder);building_glass=false;}
static void hk_triangle(void* builder,const void* p0,const void* p1,const void* p2,const void* normal,const int32_t* color){
 if(building_glass){glass_triangle(builder,p0,p1,p2,normal,&tint);return;}
 ((EdgeTriangleFn)h_triangle.original)(builder,p0,p1,p2,normal,color);}

static bool setting_on(uint64_t token){AnySettingValueV1 v;return !token||!settingsmod::get(token,&v)||v.number!=0;}
static bool curved_on(){return setting_on(curved_setting);}
static std::atomic<uint64_t> frameless_count{};
static void hk_plate(const void* nodes,const void* normal,const int32_t* front,const int32_t* back,const void* dir,const int32_t* size,const int32_t* type,
 const int32_t* border,const int32_t* glass_state,const void* impacts,void* builder){auto original=(PlateGeometryFn)h_plate.original;
 if(!ready||!type||*type!=1||!glass_state||*glass_state!=0||!setting_on(frameless_setting)){original(nodes,normal,front,back,dir,size,type,border,glass_state,impacts,builder);return;}
 glass_color(&window_tint,glass_state);if(!frameless_count.fetch_add(1))log("FRAMELESS_WINDOW_FIRST");
 building_frameless=true;original(nodes,normal,front,back,dir,size,type,border,glass_state,impacts,builder);building_frameless=false;}
static void hk_plate_triangle(void* builder,const void* p0,const void* p1,const void* p2,const void* normal,const int32_t* color){
 if(building_frameless){glass_triangle(builder,p0,p1,p2,normal,&window_tint);return;}
 ((EdgeTriangleFn)h_plate_triangle.original)(builder,p0,p1,p2,normal,color);}
static void hk_window(bool* ok,const void* dir,const void* normal,const int32_t* size,const void* nodes){
 ((SuitableFn)h_window.original)(ok,dir,normal,size,nodes);
 if(!*ok&&curved_on()){*ok=true;if(!curved_allowed.fetch_add(1))log("CURVED_WINDOW_ALLOWED first=1");}}

static void remove_window(){h_window.remove((void*)&hk_window);h_plate.remove((void*)&hk_plate);h_plate_triangle.remove((void*)&hk_plate_triangle);}
static void remove(){h_triangle.remove((void*)&hk_triangle);h_static.remove((void*)&hk_static);}
static void install(){using namespace anymaker::experimental;namespace b=anyhelpers::bind;
 for(int attempt=0;attempt<180&&!stop;++attempt){if(attempt)Sleep(1000);
  if(!matching_build()){if(!attempt)log("GLASS_OFF reason=game_build_mismatch",1);return;}
  void** static_cell=route::cell(b::edge_static_geometry);void** triangle_cell=route::cell(b::edge_triangle);
  void* triangle=route::function(b::triangle_glass);void** window_cell=route::cell(b::suitable_for_window);void** plate_cell=route::cell(b::plate_static_geometry);void** plate_triangle_cell=route::cell(b::plate_triangle);void* color=route::function(b::window_glass_color);
  if(!static_cell||!triangle_cell||!triangle||!color){if(attempt==20||attempt==179)log(std::string("GLASS_LOOKUP found=")+(static_cell?"1":"0")+(triangle_cell?"1":"0")+(triangle?"1":"0")+(color?"1":"0"),attempt==179?2:1);continue;}
  glass_triangle=(GlassTriangleFn)triangle;glass_color=(GlassColorFn)color;
  // The triangle hook goes in first: until the strut hook is in, nothing marks a strut as glass.
  if(!h_triangle.install(triangle_cell,(void*)&hk_triangle)||!h_static.install(static_cell,(void*)&hk_static)){log("GLASS_OFF reason=hook_cell_not_writable",2);remove();return;}
  ready=true;log("GLASS_READY color_index="+std::to_string(kGlassColor)+" after_s="+std::to_string(attempt));
  bool curved=window_cell&&h_window.install(window_cell,(void*)&hk_window);log("CURVED_WINDOWS_READY installed="+std::to_string(curved),curved?0:2);
  bool frameless=plate_cell&&plate_triangle_cell&&h_plate_triangle.install(plate_triangle_cell,(void*)&hk_plate_triangle)&&h_plate.install(plate_cell,(void*)&hk_plate);
  log("FRAMELESS_WINDOWS_READY installed="+std::to_string(frameless)+" hidden_color_index="+std::to_string(kHiddenColor),frameless?0:2);return;}}
static void start(){
 AnyModSettingV1 row;row.mod_id="anyhelpers";row.mod_name="AnyHelpers";row.setting_id="curved_windows";row.label="Curved windows";
 row.description="Lets the window tool and plate type tool make windows of any plate shape. Saved windows also load without the mod. The host needs it on to place them.";row.default_number=1;
 curved_setting=settingsmod::register_setting(&row);
 AnyModSettingV1 frame;frame.mod_id="anyhelpers";frame.mod_name="AnyHelpers";frame.setting_id="frameless_windows";frame.label="Frameless windows";
 frame.description="Draws windows as glass right to the edge, with no frame. Only changes how they look to you.";frame.default_number=1;
 frameless_setting=settingsmod::register_setting(&frame);std::thread(install).detach();}
static void shutdown(){stop=true;if(ready){remove();remove_window();}}
} // namespace glass
