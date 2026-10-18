#include "effects/view_math.h"
#include "effects/view_gpu.h"
// Scene view (EXPERIMENTAL): a second camera drawn by the game itself.
// renderer._record_commands_gbuffer records every gbuffer pipeline from the
// element lists _build_renderables filled for the frame, with the camera
// constants _build_scene wrote. Those lists are plain data that recording reads
// and does not consume, so after the game's own call the view records the same
// pass again with: a copy of the camera constants rebuilt for another camera,
// AnyAPI's own gbuffer targets (made by the game's create_resource_target, so the
// game binds, clears and tracks them like its own), and ocean, clouds and grass
// switched off. Everything is put back before the game continues. The result is
// lit at the pre-HUD boundary and drawn into a rectangle of the screen.
// Culling is still the player's camera: only what it draws can appear.
namespace scene_view {
static anyapi_runtime::CallCellHook gbuffer_cell;
using SceneFn=void(*)(void*,void*);
using StringCtor=void(*)(void*,const char*);using StringDtor=void(*)(void*);
// gc-x64: hidden result pointer first, every argument by pointer.
using CreateTarget=void(*)(void* result,void* graphics,const int32_t* format,const int32_t* width,const int32_t* height,const void* name);
using CreateTexture=void(*)(void* result,void* graphics,const int32_t* format,const int32_t* usage,const int32_t* width,const int32_t* height,const int32_t* depth,const void* name);
using AllocateConstants=void(*)(void* result,void* graphics,const int32_t* bytes);
struct Natives {uintptr_t graphics_global{};StringCtor string_ctor{};StringDtor string_dtor{};CreateTarget create_target{};CreateTexture create_texture{};AllocateConstants allocate{};};
static Natives natives;
static const unsigned char GBUFFER[]={0x53,0x55,0x56,0x57,0x48,0x81,0xec,0x28,0x0b,0,0,0x48,0x89,0x8c,0x24,0x30,0x0a,0,0,0x48,0x89,0x94,0x24,0x38,0x0a,0,0,0x48,0x8b,0x05,0xde,0x33};
static constexpr size_t GBUFFER_END=13312;
// renderer._create_screen_dependent_graphics_resources: the game's own target creation.
static const unsigned char SCREEN_RESOURCES[]={0x53,0x55,0x56,0x57,0x48,0x81,0xec,0xe8,0x09,0,0,0x48,0x89,0x8c,0x24,0x60,0x09,0,0,0x48,0x8b,0x05,0x8e,0x2f,0,0,0x48,0x8b,0x18,0x48,0x8b,0x2d};
static constexpr size_t SCREEN_RESOURCES_END=12200,BUILD_SCENE_END=6680;
// The pipeline renderers (each starts with its element vector) the gbuffer pass draws.
static constexpr uint16_t VECTORS[]={0x1488,0x14b0,0x14d8,0x1500,0x1528,0x1550,0x1578,0x15a0,0x15c8,0x15f0,0x1618,0x1640,0x1668,0x1690,0x16b8,0x16e0,0x1730,0x1758,0x17d0,0x17f8,0x1820,0x1848,0x1870,0x1898,0x18c0,0x18e8,0x1910,0x1938,0x1960,0x1988,0x19b0,0x19d8,0x1a00,0x1a28,0x1a50,0x1a78,0x1af0,0x1b18,0x1b68,0x1b90,0x1bb8,0x1c80,0x1d98,0x1e60,0x1e88,0x1eb0,0x1ed8,0x1f28,0x2180,0x2720,0x2748,0x2b30,0x2ba8};
static constexpr size_t VECTOR_COUNT=sizeof(VECTORS)/sizeof(VECTORS[0]),VECTOR_BYTES=40;
// Renderer fields (object pointer word of each ref) the gbuffer pass binds.
// Formats: 5 R32F, 7 RGBA16F, 8 D32F. half: the game makes clouds half size.
struct Spec {uint16_t offset;int32_t format;bool half,texture;const char* name;};
static constexpr Spec SPECS[]={{0x1e8,7,false,false,"anyapi view gbuffer 0"},{0x1f8,7,false,false,"anyapi view gbuffer 1"},{0x208,7,false,false,"anyapi view ocean"},{0x218,8,false,false,"anyapi view depth"},
 {0x228,7,true,false,"anyapi view clouds"},{0x238,8,true,false,"anyapi view clouds depth"},{0x2b8,8,false,false,"anyapi view ocean stencil"},{0x2c8,5,false,false,"anyapi view ocean mask"},{0x408,8,false,true,"anyapi view depth copy"}};
static constexpr size_t SPEC_COUNT=sizeof(SPECS)/sizeof(SPECS[0]);
enum : size_t {VIEW_ALBEDO=0,VIEW_NORMAL=1,VIEW_DEPTH=3};
// The refs the game returned. They are kept for the process (never released),
// so the game never frees a target this view still binds.
struct Targets {uintptr_t refs[SPEC_COUNT][2]{};int32_t width{},height{};bool valid{};};
static Targets targets;
struct Constants {uintptr_t resource{},cpu{},gpu{};};
static_assert(sizeof(Constants)==24);
// What the display pass needs from the last recorded view.
static scene_view_gpu::Frame shown;static bool have_view{};static uint64_t frame_counter{};static std::atomic<bool> broken{};
static scene_view_gpu::Gpu gpu;
static bool in_game(uintptr_t p){HMODULE m{};return p&&GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)p,&m)&&m==GetModuleHandleW(nullptr);}
static uintptr_t slot_value(uintptr_t body,size_t end,size_t slot){uintptr_t v{};return body&&safe_read_memory(body+end+slot*8,&v,8)?v:0;}
static uintptr_t slot_function(uintptr_t body,size_t end,size_t slot){uintptr_t cell=slot_value(body,end,slot),fn{};return cell&&safe_read_memory(cell,&fn,8)&&in_game(fn)?fn:0;}
static void* graphics(){uintptr_t g{};return natives.graphics_global&&safe_read_memory(natives.graphics_global,&g,8)?(void*)g:nullptr;}
static bool writable(uintptr_t pointer,size_t bytes){MEMORY_BASIC_INFORMATION m{};return pointer&&VirtualQuery((void*)pointer,&m,sizeof(m))&&m.State==MEM_COMMIT&&!(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&(m.Protect&(PAGE_READWRITE|PAGE_EXECUTE_READWRITE))&&pointer+bytes>=pointer&&pointer+bytes<=uintptr_t(m.BaseAddress)+m.RegionSize;}
static void fail(int32_t code,const char* why){error=code;auto n=++rejected;if(n==1||n%600==0)log_line(ANY_LOG_WARN,"framework",(std::string("SCENE_VIEW bypass=")+why+" error="+std::to_string(code)).c_str());}
// Creates the view's targets at the game's gbuffer size, through the game.
static bool ensure_targets(int32_t w,int32_t h){if(targets.valid&&targets.width==w&&targets.height==h)return true;
 auto g=graphics();if(!g||!natives.create_target||!natives.create_texture||!natives.string_ctor||!natives.string_dtor)return false;
 Targets next;next.width=w;next.height=h;
 for(size_t i=0;i<SPEC_COUNT;++i){auto& s=SPECS[i];int32_t tw=s.half?w/2:w,th=s.half?h/2:h,usage=0,depth=1,format=s.format;unsigned char name[16]{};
  natives.string_ctor(name,s.name);
  if(s.texture)natives.create_texture(next.refs[i],g,&format,&usage,&tw,&th,&depth,name);else natives.create_target(next.refs[i],g,&format,&tw,&th,name);
  natives.string_dtor(name);
  if(!next.refs[i][1])return false;
  if(!s.texture){volumetric::NativeTarget t;if(!volumetric::target(next.refs[i][1],t,safe_read_memory)||int32_t(t.width)!=tw||int32_t(t.height)!=th)return false;}}
 next.valid=true;targets=next;width=uint32_t(w);height=uint32_t(h);
 log_line(ANY_LOG_INFO,"framework",("SCENE_VIEW targets_created=1 width="+std::to_string(w)+" height="+std::to_string(h)).c_str());return true;}
static int guarded(SceneFn original,void* renderer,void* scene){__try{original(renderer,scene);return 0;}__except(EXCEPTION_EXECUTE_HANDLER){return 1;}}
static bool read3(uintptr_t address,double* out){if(!safe_read_memory(address,out,24))return false;for(int i=0;i<3;++i)if(!std::isfinite(out[i])||std::abs(out[i])>1e9)return false;return true;}
// Records the view after the game's own gbuffer pass. before: the element
// vector headers as they were before that pass.
static void record_view(SceneFn original,uintptr_t renderer,uintptr_t scene,const AnySceneViewParametersV1& p,const unsigned char (&before)[VECTOR_COUNT][VECTOR_BYTES]){
 if(!writable(renderer,0x680)||!writable(scene,0x2bf8))return fail(10,"native_objects");
 // The game's gbuffer: its size, and the three formats the view relies on.
 volumetric::NativeTarget main[3];static constexpr uint16_t checks[3]={0x1e8,0x1f8,0x218};static constexpr uint32_t formats[3]={10,10,41};
 for(int i=0;i<3;++i){uintptr_t object{};if(!safe_read_memory(renderer+checks[i],&object,8)||!volumetric::target(object,main[i],safe_read_memory)||main[i].srv_format!=formats[i]||main[i].width!=main[0].width||main[i].height!=main[0].height)return fail(11,"gbuffer_contract");}
 unsigned char flags[4]{};if(!safe_read_memory(renderer+0x63d,flags,4))return fail(12,"flags");for(auto f:flags)if(f>1)return fail(12,"flags");
 if(!ensure_targets(int32_t(main[0].width),int32_t(main[0].height))){broken=true;return fail(13,"create_targets");}
 // Camera constants: copy the game's, rebuild them for the view.
 Constants game{},view{};float camera[scene_view_math::CAMERA_FLOATS]{};
 if(!safe_read_memory(scene+0xf58,&game,sizeof(game))||!game.cpu||!safe_read_memory(game.cpu,camera,sizeof(camera)))return fail(14,"camera_constants");
 // The game camera, read from its own view matrix: whatever moved it (a third
 // person mod, a vehicle seat), the constants are what the frame draws with.
 scene_view_math::Pose mainpose,pose;double origin[3]{},graphics_camera[3]{};
 int layout=scene_view_math::pose_from_constants(camera,mainpose);
 if(layout<0){static int described{};if(described++<3){char text[640];scene_view_math::describe(camera,text,sizeof text);log_line(ANY_LOG_WARN,"framework",(std::string("SCENE_VIEW camera_constants ")+text).c_str());}return fail(1,"camera_layout");}
 if(!read3(scene+0x8,origin)||!read3(scene+0x128,graphics_camera))return fail(15,"camera_pose");
 bool posed=false;
 if(p.mode==1)posed=scene_view_math::relative(mainpose,p.position,p.yaw,p.pitch,pose);
 else{posed=scene_view_math::orient(mainpose,p.yaw,p.pitch,pose);for(int i=0;i<3;++i)pose.position[i]=p.position[i]-origin[i]-(graphics_camera[i]-mainpose.position[i]);}
 if(!posed)return fail(4,"pose");
 double scale=scene_view_math::fov_scale_for(camera,layout,p.fov_degrees*3.14159265358979323846/180);scene_view_math::M4 inverse;
 if(auto f=scene_view_math::rebuild(camera,layout,mainpose,pose,scale,&inverse);f!=scene_view_math::OK){static int described{};if(described++<3){char text[640];scene_view_math::describe(camera,text,sizeof text);log_line(ANY_LOG_WARN,"framework",("SCENE_VIEW camera_rebuild failure="+std::to_string(int(f))+" "+text).c_str());}return fail(f,"camera_rebuild");}
 auto g=graphics();int32_t bytes=704;if(!g||!natives.allocate)return fail(16,"allocate");natives.allocate(&view,g,&bytes);
 if(!view.cpu||!writable(view.cpu,sizeof(camera)))return fail(16,"allocate");
 // The rest of the 704 bytes (shadows, light, fog) stays the game's.
 unsigned char block[704]{};if(!safe_read_memory(game.cpu,block,sizeof(block)))return fail(14,"camera_constants");memcpy(block,camera,sizeof(camera));memcpy((void*)view.cpu,block,sizeof(block));
 // Element vectors: the game's pass must not have reallocated them; recording
 // reads them, but any it emptied are refilled from the saved headers.
 unsigned char after[VECTOR_COUNT][VECTOR_BYTES];
 for(size_t i=0;i<VECTOR_COUNT;++i){if(!safe_read_memory(scene+VECTORS[i],after[i],VECTOR_BYTES)||memcmp(after[i],before[i],8))return fail(17,"element_vectors");}
 // Swap in the view: targets, constants, flags, element counts.
 uintptr_t saved[SPEC_COUNT]{};for(size_t i=0;i<SPEC_COUNT;++i)if(!safe_read_memory(renderer+SPECS[i].offset,&saved[i],8)||!saved[i])return fail(18,"target_fields");
 for(size_t i=0;i<VECTOR_COUNT;++i)memcpy((void*)(scene+VECTORS[i]+8),before[i]+8,8);
 for(size_t i=0;i<SPEC_COUNT;++i)memcpy((void*)(renderer+SPECS[i].offset),&targets.refs[i][1],8);
 memcpy((void*)(scene+0xf58),&view,sizeof(view));
 const unsigned char off[3]{};memcpy((void*)(renderer+0x63d),off,3);// ocean, clouds, grass
 int faulted=guarded(original,(void*)renderer,(void*)scene);
 memcpy((void*)(renderer+0x63d),flags,3);memcpy((void*)(scene+0xf58),&game,sizeof(game));
 for(size_t i=0;i<SPEC_COUNT;++i)memcpy((void*)(renderer+SPECS[i].offset),&saved[i],8);
 for(size_t i=0;i<VECTOR_COUNT;++i){unsigned char now[8]{};memcpy(now,(void*)(scene+VECTORS[i]),8);// a vector reallocated by our pass keeps its new buffer
  if(!memcmp(now,after[i],8))memcpy((void*)(scene+VECTORS[i]),after[i],VECTOR_BYTES);else memcpy((void*)(scene+VECTORS[i]+8),after[i]+8,8);}
 if(faulted){broken=true;log_line(ANY_LOG_ERROR,"framework","SCENE_VIEW disabled=1 reason=fault_in_view_pass");return fail(19,"fault");}
 // Display constants for the pre-HUD pass.
 scene_view_gpu::Frame f;for(int i=0;i<4;++i)for(int j=0;j<4;++j)f.inverse[i*4+j]=float(inverse.m[i][j]);
 for(int i=0;i<3;++i)f.camera[i]=float(pose.position[i]);f.camera[3]=p.exposure;
 double c[7][3]{},sun[3]{};if(!safe_read_memory(scene+0xa90,c,sizeof(c))||!read3(scene+0xa78,sun))return fail(20,"light");
 double l=std::sqrt(sun[0]*sun[0]+sun[1]*sun[1]+sun[2]*sun[2]);if(!(l>1e-6))return fail(20,"light");
 for(int i=0;i<3;++i){f.sun[i]=float(-sun[i]/l);f.view_up[i]=float(c[0][i]);f.view_down[i]=float(c[1][i]);f.sun_light[i]=float(c[4][i]);f.sky_light[i]=float(c[5][i]);f.back_light[i]=float(c[6][i]);}
 f.options[0]=std::max(camera[152],camera[153]);if(!(f.options[0]>0&&f.options[0]<1e7f))f.options[0]=0;f.options[1]=1;
 if(!scene_view_gpu::valid(f))return fail(20,"light");
 shown=f;have_view=true;auto n=++frames;
 double scene_forward[3]{};bool forward_read=read3(scene+0x98,scene_forward);
 if(n==1||n%600==0)log_line(ANY_LOG_INFO,"framework",("SCENE_VIEW frames="+std::to_string(n)+" layout="+std::to_string(layout)+" mode="+std::to_string(p.mode)+" width="+std::to_string(targets.width)+" height="+std::to_string(targets.height)+" fov_scale="+std::to_string(scale)+" far="+std::to_string(f.options[0])+" camera="+std::to_string(mainpose.position[0])+","+std::to_string(mainpose.position[1])+","+std::to_string(mainpose.position[2])+" forward_agreement="+std::to_string(forward_read?scene_view_math::dot3(scene_forward,mainpose.forward):0.)+" location=GBUFFER_REPLAY").c_str());
}
static void gbuffer_hook(void* renderer,void* scene){auto original=(SceneFn)gbuffer_cell.original();auto p=copy();
 if(!p.enabled||broken){have_view=false;original(renderer,scene);return;}
 if(frame_counter++%p.interval){original(renderer,scene);return;}
 unsigned char before[VECTOR_COUNT][VECTOR_BYTES];bool saved=true;for(size_t i=0;i<VECTOR_COUNT;++i)saved=safe_read_memory(uintptr_t(scene)+VECTORS[i],before[i],VECTOR_BYTES)&&saved;
 original(renderer,scene);
 if(!saved)return fail(17,"element_vectors");
 record_view(original,uintptr_t(renderer),uintptr_t(scene),p,before);
}
// Pre-HUD, after antialiasing: light the view and draw it into its rectangle.
static void display(){auto p=copy();if(!p.enabled||broken||!have_view||p.display[2]<=0||p.display[3]<=0){ready=false;return;}
 volumetric::NativeTarget t[3];static constexpr size_t which[3]={VIEW_DEPTH,VIEW_NORMAL,VIEW_ALBEDO};
 for(int i=0;i<3;++i)if(!volumetric::target(targets.refs[which[i]][1],t[i],safe_read_memory))return fail(21,"view_targets");
 auto list=scene_antialiasing::command_list();auto saved=scene_antialiasing::states.find(list);
 if(!scene_antialiasing::tracking||!list||saved==scene_antialiasing::states.end()||!saved->second.ready())return fail(22,"command_bindings");
 std::lock_guard lock(platform::render_mutex);if(!platform::swap||!platform::queue)return fail(23,"device");
 Microsoft::WRL::ComPtr<ID3D12Device> device,maker;Microsoft::WRL::ComPtr<ID3D12Resource> back;
 if(FAILED(list->GetDevice(IID_PPV_ARGS(&device)))||FAILED(platform::swap->GetBuffer(platform::swap->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&back))))return fail(23,"device");
 for(auto& x:t)if(FAILED(((ID3D12Resource*)x.resource)->GetDevice(IID_PPV_ARGS(maker.ReleaseAndGetAddressOf())))||maker.Get()!=device.Get())return fail(23,"device");
 auto desc=back->GetDesc();if(!gpu.ensure(device.Get(),platform::queue.Get(),desc))return fail(24,"gpu_resources");
 double w=double(desc.Width),h=double(desc.Height);D3D12_RECT rect{LONG(p.display[0]*w),LONG(p.display[1]*h),LONG((p.display[0]+p.display[2])*w),LONG((p.display[1]+p.display[3])*h)};
 auto input=[](const volumetric::NativeTarget& x){return scene_view_gpu::Gpu::Input{(ID3D12Resource*)x.resource,D3D12_RESOURCE_STATES(x.state)};};
 auto bindings=saved->second;scene_antialiasing::owned=true;bool drawn=gpu.record(list,back.Get(),rect,input(t[0]),input(t[1]),input(t[2]),shown);if(drawn)bindings.restore(list);scene_antialiasing::owned=false;
 if(!drawn)return fail(25,"gpu_record");
 ready=true;static uint64_t count{};if(++count==1||count%600==0)log_line(ANY_LOG_INFO,"framework",("SCENE_VIEW displayed="+std::to_string(count)+" gpu_ms="+std::to_string(gpu.gpu_milliseconds())+" rect="+std::to_string(rect.left)+","+std::to_string(rect.top)+","+std::to_string(rect.right)+","+std::to_string(rect.bottom)+" location=BEFORE_HUD").c_str());
}
// gbuffer: the call cell of _record_commands_gbuffer (slot 27 of _record_render_commands).
// build_scene: the _build_scene body. screen: candidates for the target-creation body.
static void install(void** gbuffer,uintptr_t build_scene,const std::vector<uintptr_t>& screen){uintptr_t body{};
 if(!gbuffer||!safe_read_memory(uintptr_t(gbuffer),&body,8)||!body||screen.size()!=1){log_line(ANY_LOG_WARN,"framework",("SCENE_VIEW unavailable=1 reason=bodies screen_candidates="+std::to_string(screen.size())).c_str());return;}
 auto csd=screen[0];Natives n;n.graphics_global=slot_value(body,GBUFFER_END,0);
 n.string_ctor=(StringCtor)slot_function(body,GBUFFER_END,1);n.string_dtor=(StringDtor)slot_function(body,GBUFFER_END,3);
 n.create_target=(CreateTarget)slot_function(csd,SCREEN_RESOURCES_END,3);n.create_texture=(CreateTexture)slot_function(csd,SCREEN_RESOURCES_END,14);n.allocate=(AllocateConstants)slot_function(build_scene,BUILD_SCENE_END,16);
 // The same helpers seen from both bodies: proof the slot tables were read right.
 bool consistent=n.graphics_global&&n.graphics_global==slot_value(csd,SCREEN_RESOURCES_END,0)&&n.string_ctor&&(uintptr_t)n.string_ctor==slot_function(csd,SCREEN_RESOURCES_END,2)&&n.string_dtor&&(uintptr_t)n.string_dtor==slot_function(csd,SCREEN_RESOURCES_END,4);
 if(!consistent||!n.create_target||!n.create_texture||!n.allocate||n.create_target==(CreateTarget)n.create_texture){log_line(ANY_LOG_WARN,"framework","SCENE_VIEW unavailable=1 reason=native_slots");return;}
 natives=n;if(!gbuffer_cell.install(gbuffer,(void*)gbuffer_hook)){log_line(ANY_LOG_WARN,"framework","SCENE_VIEW unavailable=1 reason=gbuffer_cell");return;}
 available=true;log_line(ANY_LOG_INFO,"framework","SCENE_VIEW resolved=1 location=GBUFFER_REPLAY experimental=1");}
}
