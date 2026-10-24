// Bent windows support, with AnyMirror's auto-split (it cuts a bent window loop into flat pieces joined by hidden struts):
// - Hidden struts: a strut painted kHiddenColor is not drawn (vehicle_edge_util.add_static_geometry returns at once). It still
//   holds plates and collides, and players without the mod see a strut in that paint colour.
// - Frameless windows (setting, on by default, local look only): vehicle_plate_util.add_static_geometry builds a window plate
//   as a solid plate (type basic) whose faces (add_plate_triangle) all go to the window glass list (add_triangle_glass) with
//   the window's tint, so the glass runs edge to edge and pieces meeting at a hidden strut read as one pane. A window the
//   stock shape rule refuses is always built this way: the stock window frame math (inset corners) blows up on thin or
//   sharp shapes and drew huge black glass over the world in game (2026-10-24).
// - Any window shape (setting, on by default): vehicle_plate_util.get_is_suitable_for_window is the window shape rule, run only
//   when a window is placed or a plate is switched to window (the tool on the client, the event handler on the host). It is
//   answered yes. Saves do not re-check it, so the windows load in the unmodded game too.
#include "anymaker_sdk_runtime.hpp"
#include "route_cell.h"
#include "anyhelpers_bindings.h"
#include <atomic>
namespace windows {
constexpr int32_t kHiddenColor=68;  // palette index (paint tool C68, the pale green 185,206,172): hidden strut
struct Color8 {uint8_t r,g,b,a;};
using StaticGeometryFn=void(*)(const void*,const void*,const int32_t*,const int32_t*,void*);
using TriangleFn=void(*)(void*,const void*,const void*,const void*,const void*,const int32_t*);
using GlassTriangleFn=void(*)(void*,const void*,const void*,const void*,const void*,const Color8*);
using GlassColorFn=void(*)(Color8*,const int32_t*);
using PlateGeometryFn=void(*)(const void*,const void*,const int32_t*,const int32_t*,const void*,const int32_t*,const int32_t*,const int32_t*,const int32_t*,const void*,void*);
using SuitableFn=void(*)(bool*,const void*,const void*,const int32_t*,const void*);
static anymaker::cell_hook h_static,h_window,h_plate,h_plate_triangle;
static uint64_t shape_setting,frameless_setting;
static GlassTriangleFn glass_triangle;static GlassColorFn glass_color;
static std::atomic<bool> ready{},stop{};
static thread_local bool building_frameless{};static thread_local Color8 window_tint{};
static std::atomic<uint64_t> hidden_count{},frameless_count{},shape_count{},odd_count{};

static bool setting_on(uint64_t token){AnySettingValueV1 v;return !token||!settingsmod::get(token,&v)||v.number!=0;}
static void hk_static(const void* node_0,const void* node_1,const int32_t* color,const int32_t* size,void* builder){
 if(color&&*color==kHiddenColor){if(!hidden_count.fetch_add(1))log("HIDDEN_STRUT_FIRST");return;}
 ((StaticGeometryFn)h_static.original)(node_0,node_1,color,size,builder);}
static void hk_plate(const void* nodes,const void* normal,const int32_t* front,const int32_t* back,const void* dir,const int32_t* size,const int32_t* type,
 const int32_t* border,const int32_t* glass_state,const void* impacts,void* builder){auto original=(PlateGeometryFn)h_plate.original;
 if(!type||*type!=1||!glass_state){original(nodes,normal,front,back,dir,size,type,border,glass_state,impacts,builder);return;}
 bool stock_shape=false;((SuitableFn)h_window.original)(&stock_shape,dir,normal,size,nodes);
 if(stock_shape&&!setting_on(frameless_setting)){original(nodes,normal,front,back,dir,size,type,border,glass_state,impacts,builder);return;}
 glass_color(&window_tint,glass_state);
 if(!frameless_count.fetch_add(1))log(std::string("FRAMELESS_WINDOW_FIRST stock_shape=")+(stock_shape?"1":"0"));
 if(!stock_shape&&!odd_count.fetch_add(1))log("ODD_WINDOW_AS_GLASS_FIRST");
 const int32_t basic=0;building_frameless=true;original(nodes,normal,front,back,dir,size,&basic,border,glass_state,impacts,builder);building_frameless=false;}
static void hk_plate_triangle(void* builder,const void* p0,const void* p1,const void* p2,const void* normal,const int32_t* color){
 if(building_frameless){glass_triangle(builder,p0,p1,p2,normal,&window_tint);return;}
 ((TriangleFn)h_plate_triangle.original)(builder,p0,p1,p2,normal,color);}
static void hk_window(bool* ok,const void* dir,const void* normal,const int32_t* size,const void* nodes){
 ((SuitableFn)h_window.original)(ok,dir,normal,size,nodes);
 if(!*ok&&setting_on(shape_setting)){*ok=true;if(!shape_count.fetch_add(1))log("WINDOW_SHAPE_ALLOWED first=1");}}

static void remove(){h_static.remove((void*)&hk_static);h_window.remove((void*)&hk_window);h_plate.remove((void*)&hk_plate);h_plate_triangle.remove((void*)&hk_plate_triangle);}
static void install(){using namespace anymaker::experimental;namespace b=anyhelpers::bind;
 for(int attempt=0;attempt<180&&!stop;++attempt){if(attempt)Sleep(1000);
  if(!matching_build()){if(!attempt)log("WINDOWS_OFF reason=game_build_mismatch",1);return;}
  void** static_cell=route::cell(b::edge_static_geometry);void** window_cell=route::cell(b::suitable_for_window);
  void** plate_cell=route::cell(b::plate_static_geometry);void** plate_triangle_cell=route::cell(b::plate_triangle);
  void* triangle=route::function(b::triangle_glass);void* color=route::function(b::window_glass_color);
  void* found[]={static_cell,window_cell,plate_cell,plate_triangle_cell,triangle,color};bool all=true;std::string mask;
  for(void* p:found){all=all&&p;mask+=p?"1":"0";}
  if(!all){if(attempt==20||attempt==179)log("WINDOWS_LOOKUP found="+mask,attempt==179?2:1);continue;}
  glass_triangle=(GlassTriangleFn)triangle;glass_color=(GlassColorFn)color;
  // The triangle hook goes in before the plate hook that turns it on.
  bool ok=h_static.install(static_cell,(void*)&hk_static)&&h_window.install(window_cell,(void*)&hk_window)&&
   h_plate_triangle.install(plate_triangle_cell,(void*)&hk_plate_triangle)&&h_plate.install(plate_cell,(void*)&hk_plate);
  if(!ok){log("WINDOWS_OFF reason=hook_cell_not_writable",2);remove();return;}
  ready=true;log("WINDOWS_READY hidden_color_index="+std::to_string(kHiddenColor)+" after_s="+std::to_string(attempt));return;}}
static void start(){
 AnyModSettingV1 shape;shape.mod_id="anyhelpers";shape.mod_name="AnyHelpers";shape.setting_id="any_window_shape";shape.label="Any window shape";
 shape.description="Lets the window tool and plate type tool make windows of any flat plate shape. Saved windows also load without the mod. The host needs it on.";
 shape.default_number=1;shape_setting=settingsmod::register_setting(&shape);
 AnyModSettingV1 frame;frame.mod_id="anyhelpers";frame.mod_name="AnyHelpers";frame.setting_id="frameless_windows";frame.label="Frameless windows";
 frame.description="Draws windows as glass right to the edge, with no frame. Only changes how they look to you.";
 frame.default_number=1;frameless_setting=settingsmod::register_setting(&frame);
 std::thread(install).detach();}
static void shutdown(){stop=true;if(ready)remove();}
} // namespace windows
