// Glass struts: a strut painted the glass colour is drawn see-through, with the same tint as the stock windows, so
// a cockpit can be framed with struts you can see out of. Only the look changes: the strut keeps its collision,
// strength and replicated paint, so players without the mod see a strut in that paint colour.
// The edge mesh builder (vehicle_edge_util.add_static_geometry) adds every strut face through
// vehicle_plates_edges_builder.add_edge_triangle; while it builds a glass strut, those faces go to the builder's
// window glass list instead (add_triangle_glass, the call window plates make), tinted by get_window_glass_color(intact).
#include "anymaker_sdk_runtime.hpp"
#include "route_cell.h"
#include "anyhelpers_bindings.h"
#include <atomic>
namespace glass {
constexpr int32_t kGlassColor=70;   // palette index (rom/textures/color_palette.txtr) that marks a glass strut: the pale aqua (126,186,181)
struct Color8 {uint8_t r,g,b,a;};
using StaticGeometryFn=void(*)(const void*,const void*,const int32_t*,const int32_t*,void*);
using EdgeTriangleFn=void(*)(void*,const void*,const void*,const void*,const void*,const int32_t*);
using GlassTriangleFn=void(*)(void*,const void*,const void*,const void*,const void*,const Color8*);
using GlassColorFn=void(*)(Color8*,const int32_t*);
static anymaker::cell_hook h_static,h_triangle;
static GlassTriangleFn glass_triangle;static GlassColorFn glass_color;
static std::atomic<bool> ready{},stop{};
static thread_local bool building_glass{};static thread_local Color8 tint{};
static std::atomic<uint64_t> seen[4]{};   // palette indices 0 to 255 seen on struts, logged once each
static std::atomic<uint64_t> glass_struts{};

static void hk_static(const void* node_0,const void* node_1,const int32_t* color,const int32_t* size,void* builder){
 auto original=(StaticGeometryFn)h_static.original;int32_t c=color?*color:-1;
 if(c>=0&&c<256){uint64_t bit=1ull<<(c&63);if(!(seen[c>>6].fetch_or(bit)&bit))log("GLASS_STRUT_COLOR_SEEN index="+std::to_string(c)+(c==kGlassColor?" glass=1":""));}
 if(c!=kGlassColor||!ready){original(node_0,node_1,color,size,builder);return;}
 int32_t intact=0;glass_color(&tint,&intact);
 if(!glass_struts.fetch_add(1))log("GLASS_STRUT_FIRST tint="+std::to_string(tint.r)+","+std::to_string(tint.g)+","+std::to_string(tint.b)+","+std::to_string(tint.a));
 building_glass=true;original(node_0,node_1,color,size,builder);building_glass=false;}
static void hk_triangle(void* builder,const void* p0,const void* p1,const void* p2,const void* normal,const int32_t* color){
 if(building_glass){glass_triangle(builder,p0,p1,p2,normal,&tint);return;}
 ((EdgeTriangleFn)h_triangle.original)(builder,p0,p1,p2,normal,color);}

static void remove(){h_triangle.remove((void*)&hk_triangle);h_static.remove((void*)&hk_static);}
static void install(){using namespace anymaker::experimental;namespace b=anyhelpers::bind;
 for(int attempt=0;attempt<180&&!stop;++attempt){if(attempt)Sleep(1000);
  if(!matching_build()){if(!attempt)log("GLASS_OFF reason=game_build_mismatch",1);return;}
  void** static_cell=route::cell(b::edge_static_geometry);void** triangle_cell=route::cell(b::edge_triangle);
  void* triangle=route::function(b::triangle_glass);void* color=route::function(b::window_glass_color);
  if(!static_cell||!triangle_cell||!triangle||!color){if(attempt==20||attempt==179)log(std::string("GLASS_LOOKUP found=")+(static_cell?"1":"0")+(triangle_cell?"1":"0")+(triangle?"1":"0")+(color?"1":"0"),attempt==179?2:1);continue;}
  glass_triangle=(GlassTriangleFn)triangle;glass_color=(GlassColorFn)color;
  // The triangle hook goes in first: until the strut hook is in, nothing marks a strut as glass.
  if(!h_triangle.install(triangle_cell,(void*)&hk_triangle)||!h_static.install(static_cell,(void*)&hk_static)){log("GLASS_OFF reason=hook_cell_not_writable",2);remove();return;}
  ready=true;log("GLASS_READY color_index="+std::to_string(kGlassColor)+" after_s="+std::to_string(attempt));return;}}
static void start(){std::thread(install).detach();}
static void shutdown(){stop=true;if(ready)remove();}
} // namespace glass
