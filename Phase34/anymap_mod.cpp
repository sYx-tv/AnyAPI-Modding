#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <windowsx.h>
#include "anyapi_mod_v1.h"
#include "anyapi_services_v1.h"
#include "anyapi_ui_state_v1.h"
#include "anyapi_menu_v1.h"
#include "mod_controls_v1.h"
#include "anyhelpers_settings_v1.h"
#include <mutex>
#include <sstream>
#include "map_roads.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "map_math.h"
#include "map_minimap_transform.h"
#include "map_zoom.h"
#include "map_markers.h"
#include "map_gpu_graphics.h"
#include "map_motion.h"
using namespace Gdiplus;
static AnyModHostV1 host{};
static const AnyGpuDrawV1* gpu_api{};static void gpu_render(const AnyFrameV1*,void*);
static const AnyUiStateV1* ui_api{};
static bool gameplay_ui(){AnyUiSnapshotV1 state;return ui_api&&ui_api->copy(&state)&&state.kind==ANY_UI_GAMEPLAY;}
static const ModControlsV1* controls{};static const AnyMenuV1* menu_api{};static uint64_t map_action{};
static uint32_t map_key(){return controls&&map_action?controls->key(map_action):'M';}
static std::wstring map_key_name(){char name[128]{};if(controls&&map_action&&controls->key_name(map_action,name,sizeof(name))){int length=MultiByteToWideChar(CP_UTF8,0,name,-1,nullptr,0);std::wstring text(length?length:1,L'\0');if(length)MultiByteToWideChar(CP_UTF8,0,name,-1,text.data(),length);if(length)text.pop_back();return text;}return L"M";}
static void register_settings(const AnyServicesV1*);
// Optional dependencies resolve after all DLLs initialize; base AnyAPI owns no map binding policy.
extern "C" __declspec(dllexport) void AnyAPI_ModReady(){auto services=AnyAPI_Services();if(!services)return;ui_api=(const AnyUiStateV1*)services->query("anyapi.ui_state",1);if(ui_api&&(ui_api->struct_size!=sizeof(*ui_api)||ui_api->version!=1||!ui_api->copy))ui_api=nullptr;menu_api=(const AnyMenuV1*)services->query("anyapi.menu",1);controls=(const ModControlsV1*)services->query("anyhelpers.controls",1);if(!controls)controls=(const ModControlsV1*)services->query("modcontrols.bindings",1);register_settings(services);
 gpu_api=(const AnyGpuDrawV1*)services->query("anyapi.gpu_draw",1);if(gpu_api&&(gpu_api->struct_size!=sizeof(*gpu_api)||gpu_api->version!=1||!gpu_api->register_renderer(gpu_render,nullptr)))gpu_api=nullptr;host.log(0,"anymap",gpu_api?"MAP_RENDER backend=GPU resident_terrain=1 refresh=every_present":"MAP_RENDER backend=CPU compatibility_fallback=1");
 if(controls&&controls->struct_size==sizeof(*controls)&&controls->version==1){ModControlActionV1 action;action.mod_id="anymap";action.mod_name="AnyMap";action.action_id="toggle_map";action.label="Open / close map";action.default_key='M';map_action=controls->register_action(&action);}else controls=nullptr;
 host.log(0,"anymap",map_action?"BINDING_SERVICE registered=1 action=toggle_map default=M":"BINDING_SERVICE available=0 fallback=M");}
struct MapOptions {bool minimap=true,names=true,guide=true,mini_names=false,grid=true,mini_route=true;double size=100,range=1200,marker=1,opacity=100,pos_x=100,pos_y=0;int corner=0;bool north_up=false;std::string waypoint_label="Waypoint";};
static MapOptions options;
static const AnyHelpersSettingsV1* settings_api;static uint64_t settings_tokens[15]{},settings_revision=UINT64_MAX;
// Optional registration keeps a standalone AnyMap's established defaults intact.
static void register_settings(const AnyServicesV1* services){settings_api=(const AnyHelpersSettingsV1*)services->query("anyhelpers.settings",1);
 if(!settings_api||settings_api->struct_size!=sizeof(*settings_api)||settings_api->version!=1){settings_api=nullptr;host.log(0,"anymap","SETTINGS_SERVICE available=0 defaults=1");return;}
 const char* choices[]={"Rotate with player","North up"};const char* corners[]={"Top right","Top left","Bottom right","Bottom left","Custom"};
 const char* ids[]={"show_minimap","minimap_size","minimap_range","minimap_orientation","player_names","marker_scale","route_guide","waypoint_label","minimap_corner","minimap_x","minimap_y","minimap_opacity","minimap_names","map_grid","minimap_route"};
 const char* labels[]={"Show minimap","Minimap size (%)","Minimap range (meters)","Minimap orientation","Show player names","Player marker scale","Show route guidance","Waypoint label","Minimap corner","Custom horizontal position (%)","Custom vertical position (%)","Minimap opacity (%)","Minimap player names","Show map grid","Show route on minimap"};
 uint32_t kinds[]={ANY_SETTING_BOOL,ANY_SETTING_INTEGER,ANY_SETTING_NUMBER,ANY_SETTING_CHOICE,ANY_SETTING_BOOL,ANY_SETTING_NUMBER,ANY_SETTING_BOOL,ANY_SETTING_TEXT,ANY_SETTING_CHOICE,ANY_SETTING_NUMBER,ANY_SETTING_NUMBER,ANY_SETTING_INTEGER,ANY_SETTING_BOOL,ANY_SETTING_BOOL,ANY_SETTING_BOOL};
 double defaults[]={1,100,1200,0,1,1,1,0,0,100,0,100,0,1,1},minimum[]={0,60,400,0,0,.6,0,0,0,0,0,25,0,0,0},maximum[]={1,150,4000,1,1,1.6,1,1,4,100,100,100,1,1,1},step[]={1,10,100,1,1,.1,1,1,1,1,1,5,1,1,1};
 unsigned registered=0;for(int i=0;i<15;++i){AnyModSettingV1 d;d.mod_id="anymap";d.mod_name="AnyMap";d.setting_id=ids[i];d.label=labels[i];d.kind=kinds[i];d.order=i;d.default_number=defaults[i];d.minimum=minimum[i];d.maximum=maximum[i];d.step=step[i];d.default_text="Waypoint";d.text_limit=48;if(i>=8&&i<=10)d.description="Position settings override placement made with Move minimap. Choose Custom for percentage positions.";if(i==3){d.choices=choices;d.choice_count=2;}if(i==8){d.choices=corners;d.choice_count=5;}settings_tokens[i]=settings_api->register_setting(&d);registered+=settings_tokens[i]!=0;}
 auto message="SETTINGS_SERVICE registered="+std::to_string(registered);host.log(registered==15?0:2,"anymap",message.c_str());}
static uint64_t painted_at{};
static bool custom_position{};static double custom_x=1,custom_y=0;static void save_position();
// Consumers see only committed copies; drafts cannot change gameplay before Apply.
static bool read_settings(){if(!settings_api)return false;auto revision=settings_api->revision();if(revision==settings_revision)return false;bool initial=settings_revision==UINT64_MAX;settings_revision=revision;int old_corner=options.corner;double old_x=options.pos_x,old_y=options.pos_y;
 for(int i=0;i<15;++i){AnySettingValueV1 v;if(!settings_api->get(settings_tokens[i],&v))continue;switch(i){case 0:options.minimap=v.number!=0;break;case 1:options.size=v.number;break;case 2:options.range=v.number;break;case 3:options.north_up=v.number!=0;break;case 4:options.names=v.number!=0;break;case 5:options.marker=v.number;break;case 6:options.guide=v.number!=0;break;case 7:options.waypoint_label=v.text;break;case 8:options.corner=int(v.number);break;case 9:options.pos_x=v.number;break;case 10:options.pos_y=v.number;break;case 11:options.opacity=v.number;break;case 12:options.mini_names=v.number!=0;break;case 13:options.grid=v.number!=0;break;case 14:options.mini_route=v.number!=0;break;}}
 if(!initial&&(old_corner!=options.corner||old_x!=options.pos_x||old_y!=options.pos_y)){custom_position=false;save_position();}
 auto message="SETTINGS_APPLIED revision="+std::to_string(revision)+" minimap="+std::to_string(options.minimap)+" size="+std::to_string(options.size)+" range="+std::to_string(options.range)+" north_up="+std::to_string(options.north_up)+" names="+std::to_string(options.names)+" marker_scale="+std::to_string(options.marker)+" guidance="+std::to_string(options.guide)+" label_bytes="+std::to_string(options.waypoint_label.size())+" corner="+std::to_string(options.corner)+" opacity="+std::to_string(options.opacity)+" mini_names="+std::to_string(options.mini_names)+" grid="+std::to_string(options.grid)+" mini_route="+std::to_string(options.mini_route);host.log(0,"anymap",message.c_str());return true;}
static HMODULE mod_module{};static ULONG_PTR gdiplus_token{};
static std::mutex state_mutex;static bool last_focus{};
static std::filesystem::path game_dir,cache_dir;static bool visible{};
static atlas::KeyEdge m_key;static atlas::View view;static atlas::SmoothZoom zoom_motion;
static mapmarkers::Store saved_markers;static bool naming{},replace_name{},shift_held[3]{},editor_held[256]{};static uint64_t editing_marker{};static atlas::Point editing_position;static std::string editing_name;static RectF editor_save,editor_cancel,editor_route,editor_delete;
static roads::Graph road_graph;static roads::Route route;
static bool waypoint{},dragging{},dragged{},live{};static atlas::Point target{},last_mouse{};
static uint64_t routed_at{},route_epoch{};static roads::Point routed_from{};
static float map_left{},map_top{},map_side{};
static int frame_width{},frame_height{},marker_scroll{};static uint64_t selected_marker{};
static bool moving_minimap{},moving_drag{};static PointF move_offset;
static RectF marker_list,route_clear,find_player,move_button,reset_position,selected_route,selected_edit,move_done,mini_preview;
static std::vector<std::pair<RectF,uint64_t>> marker_rows;
// Position is local presentation state; changing the position settings overrides a dragged placement.
static void save_position(){auto file=cache_dir/L"minimap-position.dat",temp=cache_dir/L"minimap-position.tmp";std::ofstream f(temp);f.precision(17);f<<"ANYMAP_POSITION 1 "<<custom_position<<' '<<custom_x<<' '<<custom_y;f.flush();bool ok=bool(f);f.close();if(!ok||!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temp.c_str());host.log(2,"anymap","MINIMAP_POSITION save_failed=1");}}

static void load_position(){std::ifstream f(cache_dir/L"minimap-position.dat");std::string tag;int version,active;double x,y;if(f>>tag>>version>>active>>x>>y&&tag=="ANYMAP_POSITION"&&version==1&&std::isfinite(x)&&std::isfinite(y)&&x>=0&&x<=1&&y>=0&&y<=1){custom_position=active==1;custom_x=x;custom_y=y;}}
static PointF minimap_position(int width,int height,int mw,int mh){double x=options.pos_x/100,y=options.pos_y/100;if(options.corner<4){x=options.corner==0||options.corner==2?1:0;y=options.corner>=2?1:0;}if(custom_position){x=custom_x;y=custom_y;}return {float(20+std::max(0,width-mw-40)*x),float(25+std::max(0,height-mh-50)*y)};}
static const AnySessionPlayerV1* local_player();static void save_waypoint();
static void center_player(){auto p=local_player();if(p){atlas::project(p->position[0],p->position[2],view.center);view.clamp();zoom_motion.cancel(view);}}
static void clear_route(){waypoint=false;route={};routed_at=0;if(saved_markers.active)saved_markers.commit(saved_markers.records,0);save_waypoint();}
static void hud_draw(MapGraphics&,bool full=false,float width=320);
static void full_panels(MapGraphics&,int,int);
static void placement_draw(MapGraphics&,int,int);

// Append a timestamped helper event to anymap.log for startup and input diagnosis.
static void log_event(const char* event){host.log(0,"anymap",event);}
static std::array<std::unique_ptr<Bitmap>,3> layers;
static std::unique_ptr<Bitmap> terrain_cache,frame_bitmap;static atlas::View terrain_view;static int terrain_size{};static bool terrain_quality{};
static std::vector<unsigned char> water_mask;
static AnySessionPlayersV1 players{};
static uint32_t ui_flags{};
// Find the snapshot entry marked local that also has a valid world position; return null if unavailable.
static const AnySessionPlayerV1* local_player(){for(uint32_t i=0;i<players.count;++i)if((players.players[i].valid_fields&(PLAYER_LOCAL|PLAYER_POSITION))==(PLAYER_LOCAL|PLAYER_POSITION))return &players.players[i];return nullptr;}
// Convert world X/Z into a pixel on the full map, applying atlas calibration, zoom and pan.
static PointF pixel(double x,double z){atlas::Point uv;atlas::project(x,z,uv);uv=view.screen(uv);return {map_left+float(uv.x)*map_side,map_top+float(uv.y)*map_side};}
// Convert a Windows mouse position into normalized coordinates inside the displayed map rectangle.
static atlas::Point mouse_point(int x,int y){return {(x-map_left)/map_side,(y-map_top)/map_side};}
// Check whether a normalized point falls inside the atlas square, including its edges.
static bool inside(atlas::Point p){return p.x>=0&&p.x<=1&&p.y>=0&&p.y<=1;}
// Save the waypoint enabled flag and world X/Z in the helper folder; never change the game save.
static void save_waypoint(){std::ofstream f(cache_dir/L"waypoint.dat",std::ios::binary);uint32_t magic=0x32505741,valid=waypoint?1:0;f.write((char*)&magic,4);f.write((char*)&valid,4);f.write((char*)&target,sizeof(target));}
// Restore a saved waypoint only when its format, coordinates and atlas bounds are valid.
static void load_waypoint(){std::ifstream f(cache_dir/L"waypoint.dat",std::ios::binary);uint32_t magic{},valid{};atlas::Point p;
 if(f.read((char*)&magic,4)&&f.read((char*)&valid,4)&&f.read((char*)&p,sizeof(p))&&magic==0x32505741&&valid==1){atlas::Point uv;if(atlas::project(p.x,p.y,uv)&&inside(uv)){waypoint=true;target=p;}}}
// Format a nonnegative distance for display in metres or kilometres.
static std::wstring meters(double d){if(d>=1000)return std::to_wstring(int(d/100)/10)+L"."+std::to_wstring(int(d/100)%10)+L" km";return std::to_wstring(int(std::max(0.,d)))+L" m";}
// Recalculate the road path to the saved waypoint at most every 750 ms using the local snapshot; paused full-map data is allowed.
static void update_route(){auto p=local_player();if(!p||!waypoint||(!live&&!visible))return;roads::Point from{p->position[0],p->position[1],p->position[2]};
 uint64_t now=GetTickCount64();if(routed_at&&route_epoch==players.world_epoch&&now-routed_at<750)return;
 route=road_graph.route(from,{target.x,0,target.y});routed_at=now;route_epoch=players.world_epoch;routed_from=from;}
// Validate and convert a UTF-8 Steam name into Windows text; return empty on invalid input.
static std::wstring utf16(const char* text) {
    int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,nullptr,0);
    if(size<=0)return {};std::wstring result(size,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,result.data(),size);result.pop_back();return result;
}
// Validate a 4096x4096 game TXTR layer, soften its palette and copy its pixels into an owned bitmap.
static std::unique_ptr<Bitmap> texture(const std::filesystem::path& path,int layer) {
    std::ifstream stream(path,std::ios::binary);uint32_t header[6]{};
    if(!stream.read((char*)header,sizeof(header))||header[0]!=0x52545854||header[1]!=2||header[2]!=2||header[4]!=1)return {};
    unsigned width=header[3]&0xffff,height=header[3]>>16;
    if(width!=4096||height!=4096||header[5]!=width*height*4)return {};
    std::vector<unsigned char> pixels(header[5]);if(!stream.read((char*)pixels.data(),pixels.size()))return {};
    if(layer==0)water_mask.resize(width*height);
    for(size_t i=0;i<pixels.size();i+=4){
        unsigned r=pixels[i],g=pixels[i+1],b=pixels[i+2],a=pixels[i+3];
        if(layer==0){
            water_mask[i/4]=(b>r+8&&b>=g)?1:0;
            // Same geography, softer terrain/sea; no generated replacement map.
            if(r>238&&g>238&&b>238){r=222;g=230;b=209;}
            else if(b>r+8&&b>=g){r=137;g=176;b=191;}
            else {double shade=(r+g+b)/765.;r=unsigned(105+shade*88);g=unsigned(122+shade*78);b=unsigned(104+shade*72);}
        }else if(layer==1){
            // The tree texture is a coverage mask: white is clear land, black includes sea.
            a=water_mask[i/4]?0:unsigned(a*(255-r)/255.*.22);r=61;g=104;b=69;
        }else {
            if(r==0&&g==0&&b==0)a=0;
            else if(b>r+8){r=87;g=121;b=131;a=unsigned(a*.28);}
            else if(r>g+20&&b>g+20){r=95;g=105;b=94;a=unsigned(a*.68);}
            else {r=128;g=121;b=98;a=unsigned(a*.43);}
        }
        pixels[i]=static_cast<unsigned char>(b);pixels[i+1]=static_cast<unsigned char>(g);
        pixels[i+2]=static_cast<unsigned char>(r);pixels[i+3]=static_cast<unsigned char>(a);
    }
    Bitmap source(width,height,width*4,PixelFormat32bppARGB,pixels.data());
    auto result=std::make_unique<Bitmap>(4096,4096,PixelFormat32bppARGB);
    Graphics g(result.get());g.SetCompositingMode(CompositingModeSourceCopy);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.DrawImage(&source,0,0,4096,4096);return result;
}
// Draw one nonwrapping line of UI text with the requested colour, size and weight.
static void text(MapGraphics& g,const std::wstring& s,float x,float y,float size,Color color,bool bold=false,float width=0) {
    FontFamily family(L"Segoe UI");Font font(&family,size,bold?FontStyleBold:FontStyleRegular,UnitPixel);
    SolidBrush brush(color);StringFormat format;format.SetFormatFlags(StringFormatFlagsNoWrap);
    if(width>0){format.SetTrimming(StringTrimmingEllipsisCharacter);g.DrawString(s.c_str(),int(s.size()),&font,RectF(x,y,width,size*1.8f),&format,&brush);}else g.DrawString(s.c_str(),int(s.size()),&font,PointF(x,y),&format,&brush);
}
// Draw a filled rounded panel behind labels or the map UI.
static void round_rect(MapGraphics& g,RectF r,float radius,Color color) {
    g.RoundRect(r,radius,color);
}
// Draw a small local or remote player arrow, or a dot if facing is unavailable; optionally show the Steam name or override screen heading.
static void marker(MapGraphics& g,const AnySessionPlayerV1& p,PointF at,bool labels,double heading=std::numeric_limits<double>::quiet_NaN()) {
    const bool local=(p.valid_fields&PLAYER_LOCAL)!=0;const double angle=std::isfinite(heading)?heading:((p.valid_fields&PLAYER_FACING)?atlas::screen_heading(p.yaw_radians):0);
    const float radius=float((local?1.0:.92)*options.marker);
    const atlas::Point shape[]={{0,-7},{4.7,5},{0,2},{-4.7,5}};
    PointF outline[4];for(int i=0;i<4;++i){auto q=atlas::rotate(shape[i],angle);outline[i]={at.X+float(q.x)*radius,at.Y+float(q.y)*radius};}
    SolidBrush shadow(Color(100,0,0,0));g.FillEllipse(&shadow,at.X-7,at.Y-4,14.f,12.f);
    Pen stroke(Color(245,26,44,39),2.0f);stroke.SetLineJoin(LineJoinRound);
    SolidBrush fill(local?Color(255,250,247,220):Color(255,125,230,194));
    if(p.valid_fields&PLAYER_FACING){g.FillPolygon(&fill,outline,4);g.DrawPolygon(&stroke,outline,4);}
    else {g.FillEllipse(&fill,at.X-3,at.Y-3,6.f,6.f);g.DrawEllipse(&stroke,at.X-3,at.Y-3,6.f,6.f);}
    if(labels&&options.names&&(p.valid_fields&PLAYER_NAME)){
        auto name=utf16(p.steam_name);if(name.empty())return;
        FontFamily family(L"Segoe UI");Font font(&family,12,FontStyleRegular,UnitPixel);RectF measured;
        g.MeasureString(name.c_str(),int(name.size()),&font,PointF(0,0),&measured);
        float width=std::min(235.f,measured.Width+14),x=at.X-width/2,y=at.Y+12;
        round_rect(g,{x,y,width,23},6,Color(224,26,44,39));
        SolidBrush ink(Color(255,244,245,229));StringFormat label;label.SetFormatFlags(StringFormatFlagsNoWrap);
        label.SetTrimming(StringTrimmingEllipsisCharacter);g.DrawString(name.c_str(),int(name.size()),&font,RectF(x+7,y+3,width-14,18),&label,&ink);
    }
}
// Marker editing is a map-owned modal. Its UTF-8 value is staged until a durable save.
static void begin_marker(uint64_t id,atlas::Point at){editing_marker=id;editing_position=at;auto m=saved_markers.find(id);editing_name=m?m->name:"Marker "+std::to_string(saved_markers.next);naming=true;replace_name=true;dragging=false;zoom_motion.cancel(view);saved_markers.error.clear();memset(editor_held,0,sizeof(editor_held));}
static bool finish_marker(){if(!saved_markers.save(editing_marker,editing_position,editing_name)){log_event("MARKER_SAVE failed=1 pending_retained=1");return false;}naming=false;log_event("MARKER_SAVE saved=1");return true;}
static std::string route_name(){auto m=saved_markers.find(saved_markers.active);return m&&waypoint&&std::hypot(m->position.x-target.x,m->position.y-target.y)<.01?m->name:options.waypoint_label;}
static void marker_editor(MapGraphics& g,int w,int h){if(!naming)return;SolidBrush shade(Color(155,0,0,0));g.FillRectangle(&shade,0,0,w,h);float width=std::min(480.f,float(w)-40),x=(w-width)/2,y=(h-240.f)/2;
 round_rect(g,{x,y,width,240},4,Color(255,30,30,30));text(g,editing_marker?L"EDIT MARKER":L"NAME THIS MARKER",x+22,y+20,18,Color(255,241,242,222),true);
 round_rect(g,{x+20,y+61,width-40,45},8,Color(255,13,29,25));auto value=utf16(editing_name.c_str())+L"_";FontFamily family(L"Segoe UI");Font font(&family,16,FontStyleRegular,UnitPixel);SolidBrush ink(Color(255,236,237,218));StringFormat format;format.SetFormatFlags(StringFormatFlagsNoWrap);format.SetTrimming(StringTrimmingEllipsisCharacter);g.DrawString(value.c_str(),int(value.size()),&font,RectF(x+30,y+74,width-60,26),&format,&ink);
 text(g,saved_markers.error.empty()?L"Type a name  ·  Enter saves  ·  Esc cancels":utf16(saved_markers.error.c_str()),x+22,y+119,12,Color(255,185,211,193));
 float cell=(width-52)/4;editor_save={x+20,y+160,cell,44};editor_cancel={x+28+cell,y+160,cell,44};editor_route={x+36+2*cell,y+160,cell,44};editor_delete={x+44+3*cell,y+160,cell,44};
 const RectF buttons[]={editor_save,editor_cancel,editor_route,editor_delete};const wchar_t* labels[]={L"Save",L"Cancel",L"Route",L"Delete"};
 for(int i=0;i<4;++i){bool enabled=i!=3||editing_marker;round_rect(g,buttons[i],7,i==0?Color(255,9,79,119):Color(255,39,39,39));text(g,labels[i],buttons[i].X+12,buttons[i].Y+13,14,enabled?Color(255,236,238,220):Color(255,104,130,118));}
}
// Cache resampled terrain until view/size/quality changes. Animated frames use a
// lighter filter; the settled view is refreshed with the previous high-quality filter.
static Bitmap* terrain(int side){bool quality=!zoom_motion.active;if(!terrain_cache||terrain_size!=side||terrain_view.center.x!=view.center.x||terrain_view.center.y!=view.center.y||terrain_view.zoom!=view.zoom||terrain_quality!=quality){
 if(!terrain_cache||terrain_size!=side)terrain_cache=std::make_unique<Bitmap>(side,side,PixelFormat32bppPARGB);Graphics g(terrain_cache.get());g.Clear(Color(255,26,41,37));g.SetInterpolationMode(quality?InterpolationModeHighQualityBicubic:InterpolationModeBilinear);if(layers[0]){float size=float(layers[0]->GetWidth()),source=size/float(view.zoom);g.DrawImage(layers[0].get(),RectF(0,0,float(side),float(side)),float(view.center.x)*size-source/2,float(view.center.y)*size-source/2,source,source,UnitPixel);}terrain_size=side;terrain_view=view;terrain_quality=quality;}
 return terrain_cache.get();}
// Render the full atlas with zoom, road route, waypoint, players, names, status and controls.
static void draw(MapGraphics& g,int w,int h,bool preview) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);g.Clear(Color(242,17,17,20));
    float side=float(std::min(w-560,h-182));if(side<280)side=float(std::min(w-40,h-182));float left=(w-side)/2,top=88;map_left=left;map_top=top;map_side=side;
    round_rect(g,{left-12,top-64,side+24,side+105},4,Color(255,30,30,30));
    text(g,L"Anymap",left,top-45,19,Color(255,235,239,219),true);
    text(g,preview?L"VISUAL PREVIEW · SAMPLE PLAYERS":(map_key_name()+L" / ESC  CLOSE"),left+side-230,top-42,12,Color(255,157,180,165));
    GraphicsState state=g.Save();g.SetClip(RectF(left,top,side,side));
    RectF map_rect(left,top,side,side);
    if(g.gpu()){float size=float(layers[0]->GetWidth()),source=size/float(view.zoom);g.DrawImage(layers[0].get(),RectF(left,top,side,side),float(view.center.x)*size-source/2,float(view.center.y)*size-source/2,source,source,UnitPixel);}else g.DrawImage(terrain(int(side)),int(left),int(top));
    Pen grid(Color(20,39,68,54),1);
    if(options.grid)for(int i=1;i<8;++i){auto q=view.screen({i/8.,i/8.});g.DrawLine(&grid,left+float(q.x)*side,top,left+float(q.x)*side,top+side);g.DrawLine(&grid,left,top+float(q.y)*side,left+side,top+float(q.y)*side);}
    if(waypoint){
        Pen line(Color(255,217,114,217),4);line.SetLineJoin(LineJoinRound);
        for(size_t i=1;i<route.points.size();++i){auto a=pixel(route.points[i-1].x,route.points[i-1].z),b=pixel(route.points[i].x,route.points[i].z);g.DrawLine(&line,a,b);}
        Pen approach(Color(230,217,114,217),2);approach.SetDashStyle(DashStyleDash);
        if(route.connected&&!route.points.empty()){auto p=local_player();if(p)g.DrawLine(&approach,pixel(p->position[0],p->position[2]),pixel(route.points.front().x,route.points.front().z));g.DrawLine(&approach,pixel(route.points.back().x,route.points.back().z),pixel(target.x,target.y));}
        auto at=pixel(target.x,target.y);SolidBrush pin(Color(255,239,154,227));Pen rim(Color(255,51,37,57),2);
        g.DrawLine(&rim,at.X,at.Y-8,at.X,at.Y+3);g.FillEllipse(&pin,at.X-5,at.Y-13,10.f,10.f);g.DrawEllipse(&rim,at.X-5,at.Y-13,10.f,10.f);
    }
    for(auto& pin:saved_markers.records){auto at=pixel(pin.position.x,pin.position.y);SolidBrush fill(Color(255,244,186,101));Pen edge(Color(255,42,49,37),2);g.DrawLine(&edge,at.X,at.Y,at.X,at.Y-9);g.FillEllipse(&fill,at.X-5,at.Y-17,10.f,10.f);g.DrawEllipse(&edge,at.X-5,at.Y-17,10.f,10.f);auto title=utf16(pin.name.c_str());FontFamily family(L"Segoe UI");Font font(&family,12,FontStyleRegular,UnitPixel);RectF measured;g.MeasureString(title.c_str(),int(title.size()),&font,PointF(),&measured);float width=std::min(210.f,measured.Width+14);round_rect(g,{at.X+8,at.Y-22,width,23},6,Color(235,29,46,39));SolidBrush ink(Color(255,251,224,176));StringFormat format;format.SetFormatFlags(StringFormatFlagsNoWrap);format.SetTrimming(StringTrimmingEllipsisCharacter);g.DrawString(title.c_str(),int(title.size()),&font,RectF(at.X+15,at.Y-19,width-14,18),&format,&ink);}
    for(uint32_t i=0;i<players.count;++i){const auto& p=players.players[i];atlas::Point uv{};
        if(!(p.valid_fields&PLAYER_POSITION)||!atlas::project(p.position[0],p.position[2],uv)||uv.x<0||uv.x>1||uv.y<0||uv.y>1)continue;
        marker(g,p,pixel(p.position[0],p.position[2]),true);
    }
    g.Restore(state);Pen border(Color(255,92,118,95),1);g.DrawRectangle(&border,left,top,side,side);
    unsigned missing_names=0,missing_positions=0;for(uint32_t i=0;i<players.count;++i){
        if(!(players.players[i].valid_fields&PLAYER_NAME))++missing_names;
        if(!(players.players[i].valid_fields&PLAYER_POSITION))++missing_positions;}
    std::wstring footer=std::to_wstring(players.count)+L" PLAYERS  ·  STEAM NAMES";
    if(missing_names)footer+=L"  ·  "+std::to_wstring(missing_names)+L" NAMES UNAVAILABLE";
    if(missing_positions)footer+=L"  ·  "+std::to_wstring(missing_positions)+L" POSITIONS UNAVAILABLE";
    if(players.dropped)footer+=L"  ·  "+std::to_wstring(players.dropped)+L" OVER CAPACITY";
    if(!preview&&!live)footer=players.count?L"PLAYER DATA PAUSED WHILE MAP HAS FOCUS":L"WAITING FOR LIVE PLAYER DATA";
    text(g,footer,left,top+side+15,12,Color(255,172,192,171));
    text(g,L"WHEEL  ZOOM  \u00b7  DRAG  PAN  \u00b7  CLICK  ROUTE  \u00b7  SHIFT + CLICK  ADD MARKER",left,top+side+47,11,Color(255,172,192,171));
    text(g,L"CLICK MARKER  EDIT / ROUTE  ·  RIGHT CLICK  CLEAR ROUTE  ·  F  FIND ME",left,top+side+65,10,Color(255,172,192,171));

    const float scale=side*250.f*float(view.zoom)/float(atlas::half_width*2);
    Pen scale_pen(Color(255,180,198,173),2);float sx=left+side-scale,sy=top+side+30;
    g.DrawLine(&scale_pen,sx,sy,sx+scale,sy);g.DrawLine(&scale_pen,sx,sy-4,sx,sy+3);g.DrawLine(&scale_pen,sx+scale,sy-4,sx+scale,sy+3);
    text(g,L"250 m",sx+scale/2-13,sy-18,10,Color(255,180,198,173));
    full_panels(g,w,h);if(moving_minimap)placement_draw(g,w,h);else marker_editor(g,w,h);
}
// Render remaining route distance, road approach and upcoming turn guidance, plus a relative direction arrow.
static void hud_draw(MapGraphics& g,bool full,float width){g.SetSmoothingMode(SmoothingModeAntiAlias);SolidBrush background(Color(255,30,30,30));g.FillRectangle(&background,0.f,0.f,width,85.f);auto p=local_player();if(!p)return;
 roads::Point at{p->position[0],p->position[1],p->position[2]};double gap=roads::flat(at,{target.x,0,target.y});
 std::wstring action=L"No road connection",detail=utf16(route_name().c_str())+L" "+meters(gap);
 if(gap<15){action=utf16(route_name().c_str())+L" reached";detail=L"Right click the map to clear";}
 else if(route.connected&&route.points.size()>1){
  if(route.start_gap>25){action=L"Join the highlighted road";detail=meters(route.start_gap)+L" to road  ·  "+meters(route.length)+L" remaining";}
  else if(route.length<route.end_gap+30){action=L"Leave road for waypoint";detail=meters(gap)+L" to waypoint";}
  else {action=L"Follow highlighted road";detail=meters(route.length)+L" remaining";
   double covered=0;for(size_t i=2;i<route.points.size();++i){auto a=route.points[i-2],b=route.points[i-1],c=route.points[i];
    covered+=roads::flat(a,b);if(covered>500)break;if(roads::flat(a,b)<.01||roads::flat(b,c)<.01)continue;
    double angle=roads::turn_angle(a,b,c);
    if(std::abs(angle)>.30&&covered>15){action=angle>0?L"Turn right":L"Turn left";detail=L"In "+meters(covered)+L"  ·  "+meters(route.length)+L" remaining";break;}
   }
  }
 }
 if(full){FontFamily family(L"Segoe UI");Font title(&family,17,FontStyleBold,UnitPixel),body(&family,13,FontStyleRegular,UnitPixel);SolidBrush ink(Color(255,239,239,239)),muted(Color(255,189,189,189));StringFormat wrapped;wrapped.SetTrimming(StringTrimmingEllipsisWord);g.DrawString(action.c_str(),int(action.size()),&title,RectF(4,8,width-12,48),&wrapped,&ink);g.DrawString(detail.c_str(),int(detail.size()),&body,RectF(4,58,width-12,40),&wrapped,&muted);}else{text(g,action,15,12,16,Color(255,239,239,239),true,width-24);text(g,detail,15,40,12,Color(255,189,189,189),false,width-24);}
 if(!full)text(g,map_key_name()+L"  VIEW ROUTE",15,60,10,Color(255,163,184,170));
 roads::Point toward{target.x,0,target.y};if(route.connected&&!route.points.empty()){
  toward=route.points.front();if(route.start_gap<=25){double ahead=0;for(size_t i=1;i<route.points.size();++i){ahead+=roads::flat(route.points[i-1],route.points[i]);toward=route.points[i];if(ahead>35)break;}}}
 if(p->valid_fields&PLAYER_FACING){double angle=std::atan2(toward.x-at.x,toward.z-at.z)-p->yaw_radians;atlas::Point shape[]={{0,-10},{6,7},{0,3},{-6,7}};PointF polygon[4];
  for(int i=0;i<4;++i){auto q=atlas::rotate(shape[i],angle);polygon[i]={width-30+float(q.x),(full?104.f:65.f)+float(q.y)};}SolidBrush ink(Color(255,236,146,225));g.FillPolygon(&ink,polygon,4);}
}
// Render the nearby square map, counter-rotating terrain and route with yaw; keep the local arrow up and show other players relative to it.
static void minimap_draw(MapGraphics& g){SolidBrush background(Color(255,30,30,30));g.FillRectangle(&background,0,0,320,waypoint&&options.guide?385:300);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
 auto p=local_player();if(!p)return;atlas::Point uv;atlas::project(p->position[0],p->position[2],uv);constexpr float side=256;float range=float(options.range);
 auto saved=g.Save();g.SetClip(RectF(12,12,side,side));auto transform=atlas::minimap_matrix(p->yaw_radians,options.north_up,140,140);g.MultiplyTransform(&transform);
 float pixels=4096,source=float(range*1.5/(atlas::half_width*2))*pixels;
 if(layers[0])g.DrawImage(layers[0].get(),RectF(-192,-192,384,384),float(uv.x)*pixels-source/2,float(uv.y)*pixels-source/2,source,source,UnitPixel);
 auto at=[&](double x,double z){return PointF(float((x-p->position[0])*side/range),float((z-p->position[2])*side/range));};
 Pen line(Color(255,222,119,220),3);if(options.mini_route)for(size_t i=1;i<route.points.size();++i)g.DrawLine(&line,at(route.points[i-1].x,route.points[i-1].z),at(route.points[i].x,route.points[i].z));
 for(auto& pin:saved_markers.records){auto point=at(pin.position.x,pin.position.y);SolidBrush brush(Color(255,244,186,101));g.FillEllipse(&brush,point.X-3,point.Y-3,6.f,6.f);}
 if(waypoint){auto pin=at(target.x,target.y);SolidBrush brush(Color(255,239,154,227));g.FillEllipse(&brush,pin.X-4,pin.Y-4,8.f,8.f);}g.Restore(saved);
 // Draw arrows in screen space so terrain rotation cannot rotate the local arrow twice.
 auto markers=g.Save();g.SetClip(RectF(12,12,side,side));
 for(uint32_t i=0;i<players.count;++i){const auto& other=players.players[i];if(!(other.valid_fields&PLAYER_POSITION))continue;
  auto offset=at(other.position[0],other.position[2]);auto q=atlas::minimap_point({offset.X,offset.Y},p->yaw_radians,options.north_up);
  marker(g,other,{140+float(q.x),140+float(q.y)},options.mini_names,options.north_up?atlas::screen_heading(other.yaw_radians):atlas::minimap_marker_heading(other.yaw_radians,p->yaw_radians));
 }g.Restore(markers);
 Pen border(Color(255,120,151,125),1);g.DrawRectangle(&border,12.f,12.f,side,side);
 text(g,map_key_name()+L"  "+(waypoint?utf16(route_name().c_str()):L"Anymap"),14,273,12,Color(255,205,205,205),false,270);
 if(waypoint&&options.guide){auto hud=g.Save();g.TranslateTransform(0,300);hud_draw(g);g.Restore(hud);}
}
// Draw game-style controls and clipped labels; hit rectangles are shared with input.
static void ui_button(MapGraphics& g,RectF r,const wchar_t* title,bool selected=false){round_rect(g,r,3,selected?Color(255,9,79,119):Color(255,39,39,39));FontFamily family(L"Segoe UI");Font font(&family,15,FontStyleRegular,UnitPixel);SolidBrush ink(Color(255,235,235,235));StringFormat f;f.SetAlignment(StringAlignmentCenter);f.SetLineAlignment(StringAlignmentCenter);f.SetTrimming(StringTrimmingEllipsisCharacter);g.DrawString(title,-1,&font,r,&f,&ink);}
static void panel_label(MapGraphics& g,const std::wstring& title,RectF r,float size=15){FontFamily family(L"Segoe UI");Font font(&family,size,FontStyleRegular,UnitPixel);SolidBrush ink(Color(255,233,233,233));StringFormat f;f.SetFormatFlags(StringFormatFlagsNoWrap);f.SetTrimming(StringTrimmingEllipsisCharacter);g.DrawString(title.c_str(),int(title.size()),&font,r,&f,&ink);}
// Saved places occupy the left rail; the right rail keeps navigation visible at any zoom.
static void full_panels(MapGraphics& g,int w,int h){float width=std::min(300.f,map_left-36);marker_rows.clear();if(width<160){marker_list={};return;}float lx=map_left-width-16,rx=map_left+map_side+16,top=24;
 round_rect(g,{lx,top,width,float(h)-80},4,Color(255,30,30,30));round_rect(g,{rx,top,width,float(h)-80},4,Color(255,30,30,30));
 text(g,L"MARKERS",lx+16,top+18,17,Color(255,238,238,238));text(g,std::to_wstring(saved_markers.records.size())+(saved_markers.records.size()==1?L" saved place":L" saved places"),lx+16,top+47,13,Color(255,165,165,165));
 marker_list={lx+10,top+80,width-20,std::max(58.f,float(h)-310)};int rows=std::max(1,int(marker_list.Height/58));marker_scroll=std::clamp(marker_scroll,0,std::max(0,int(saved_markers.records.size())-rows));
 if(saved_markers.records.empty()){panel_label(g,L"No saved markers",{lx+16,top+87,width-32,25});text(g,L"Shift-click the map to add one.",lx+16,top+116,12,Color(255,170,170,170));}
 for(int i=marker_scroll;i<int(saved_markers.records.size())&&i<marker_scroll+rows;++i){auto& pin=saved_markers.records[i];RectF row(marker_list.X,marker_list.Y+(i-marker_scroll)*58,marker_list.Width-8,52);round_rect(g,row,3,pin.id==selected_marker?Color(255,9,79,119):Color(255,39,39,39));panel_label(g,utf16(pin.name.c_str()),{row.X+12,row.Y+7,row.Width-24,24});auto p=local_player();std::wstring detail=pin.id==saved_markers.active&&waypoint?L"Active destination":L"Saved marker";if(p)detail+=L"  ·  "+meters(std::hypot(pin.position.x-p->position[0],pin.position.y-p->position[2]));panel_label(g,detail,{row.X+12,row.Y+30,row.Width-24,18},11);marker_rows.push_back({row,pin.id});}
 if(int(saved_markers.records.size())>rows){float travel=marker_list.Height-30;float y=marker_list.Y+travel*marker_scroll/std::max(1,int(saved_markers.records.size())-rows);round_rect(g,{marker_list.GetRight()-4,y,4,30},2,Color(255,85,85,85));}
 selected_route={lx+12,float(h)-170,(width-30)/2,40};selected_edit={selected_route.GetRight()+6,selected_route.Y,selected_route.Width,40};ui_button(g,selected_route,L"Route");ui_button(g,selected_edit,L"Edit marker");text(g,L"Select a marker to view it on the map.",lx+14,float(h)-110,11,Color(255,165,165,165));
 text(g,L"ROUTE",rx+16,top+18,17,Color(255,238,238,238));panel_label(g,waypoint?utf16(route_name().c_str()):L"No destination",{rx+16,top+57,width-32,50},20);
 if(waypoint){auto state=g.Save();g.SetClip(RectF(rx+12,top+112,width-24,120));g.TranslateTransform(rx+12,top+112);hud_draw(g,true,width-24);g.Restore(state);text(g,route.connected?L"Road route":L"Direct destination",rx+16,top+234,13,Color(255,165,165,165));panel_label(g,route.connected?meters(route.length)+L" remaining":(local_player()?meters(std::hypot(target.x-local_player()->position[0],target.y-local_player()->position[2])):L"Waiting for player"),{rx+16,top+260,width-32,26},18);auto p=local_player();if(p)text(g,meters(std::hypot(target.x-p->position[0],target.y-p->position[2]))+L" straight-line distance",rx+16,top+294,12,Color(255,165,165,165));}
 else{text(g,L"Click the map to set a waypoint",rx+16,top+116,12,Color(255,165,165,165));text(g,L"or route to a saved marker.",rx+16,top+139,12,Color(255,165,165,165));}
 route_clear={rx+12,top+326,width-24,40};find_player={rx+12,top+376,width-24,40};move_button={rx+12,float(h)-216,width-24,40};reset_position={rx+12,float(h)-166,width-24,40};ui_button(g,route_clear,L"Clear route");ui_button(g,find_player,L"Center on player");ui_button(g,move_button,L"Move minimap");ui_button(g,reset_position,L"Reset minimap position");text(g,L"More options in Mod Settings",rx+16,float(h)-110,12,Color(255,165,165,165));
}
// Placement uses full-map input capture, leaving ordinary gameplay mouse input untouched.
static void placement_draw(MapGraphics& g,int w,int h){SolidBrush shade(Color(210,15,15,15));g.FillRectangle(&shade,0,0,w,h);float scale=float(options.size/100);int mw=int(320*scale),mh=int((waypoint&&options.guide?385:300)*scale);auto at=minimap_position(w,h,mw,mh);mini_preview={at.X,at.Y,float(mw),float(mh)};
 if(g.gpu()){auto state=g.Save();g.TranslateTransform(at.X,at.Y);g.ScaleTransform(scale,scale);minimap_draw(g);g.Restore(state);}else{Bitmap preview(320,waypoint&&options.guide?385:300,PixelFormat32bppPARGB);{Graphics native(&preview);MapGraphics mini(native);minimap_draw(mini);}g.DrawImage(&preview,mini_preview);}
 Pen border(Color(255,9,110,165),3);g.DrawRectangle(&border,mini_preview);text(g,L"Drag the minimap to move it",float(w)/2-150,35,19,Color(255,240,240,240));move_done={float(w)/2-90,float(h)-80,180,44};ui_button(g,move_done,L"Done",true);
}

// Close the atlas and release this mod's generic game-input capture.
static void close_map(){visible=false;dragging=false;naming=false;if(moving_drag)save_position();moving_minimap=false;moving_drag=false;memset(shift_held,0,sizeof(shift_held));zoom_motion.cancel(view);host.capture_input(0);log_event("MAP_CLOSED_DLL");}
// Toggle the atlas without creating a window or changing game focus.
static void toggle_map(){if(visible){close_map();return;}visible=true;zoom_motion.tick=0;host.capture_input(1);log_event("MAP_OPENED_DLL");}
// Handle game-window input; state is serialized with the frame callback.
static uint32_t mod_input(const AnyInputV1* event,void*){
 if(!event||event->struct_size!=sizeof(*event))return 0;std::lock_guard<std::mutex> lock(state_mutex);const auto& e=*event;
 if((ui_api&&!gameplay_ui())||(menu_api&&menu_api->settings_open())){m_key.held=false;if(visible)close_map();return 0;}
 if(e.kind==ANY_FOCUS_LOST){last_focus=false;m_key.held=false;if(visible)close_map();return 0;}
 if(e.kind==ANY_KEY_DOWN||e.kind==ANY_KEY_UP){if(e.key==VK_SHIFT)shift_held[0]=e.kind==ANY_KEY_DOWN;else if(e.key==VK_LSHIFT)shift_held[1]=e.kind==ANY_KEY_DOWN;else if(e.key==VK_RSHIFT)shift_held[2]=e.kind==ANY_KEY_DOWN;}
 if(moving_minimap&&visible){if((e.kind==ANY_KEY_DOWN||e.kind==ANY_KEY_UP)&&e.key==map_key()){if(m_key.event(e.kind==ANY_KEY_DOWN,last_focus))toggle_map();return 1;}PointF mouse(float(e.x),float(e.y));
  if(e.kind==ANY_KEY_DOWN&&e.key==VK_ESCAPE){moving_minimap=false;moving_drag=false;save_position();painted_at=0;return 1;}
  if(e.kind==ANY_MOUSE_DOWN&&e.button==1&&mini_preview.Contains(mouse)){moving_drag=true;move_offset={mouse.X-mini_preview.X,mouse.Y-mini_preview.Y};}
  if(e.kind==ANY_MOUSE_MOVE&&moving_drag){custom_position=true;custom_x=std::clamp(double(mouse.X-move_offset.X-20)/std::max(1.f,float(frame_width)-mini_preview.Width-40),0.,1.);custom_y=std::clamp(double(mouse.Y-move_offset.Y-25)/std::max(1.f,float(frame_height)-mini_preview.Height-50),0.,1.);painted_at=0;}
  if(e.kind==ANY_MOUSE_UP&&e.button==1){if(moving_drag){moving_drag=false;save_position();log_event("MINIMAP_POSITION saved=1");}else if(move_done.Contains(mouse)){moving_minimap=false;save_position();}painted_at=0;}return 1;
 }
 if(naming&&visible){
  if(e.kind==ANY_TEXT_INPUT){if(e.key>=32&&e.key!=127){auto text=replace_name?std::string():editing_name;if(mapmarkers::append(text,e.key)){editing_name=std::move(text);replace_name=false;saved_markers.error.clear();}}return 1;}
  if(e.kind==ANY_KEY_UP){if(e.key<256)editor_held[e.key]=false;return 1;}
  if(e.kind==ANY_KEY_DOWN){if(e.key<256){if(editor_held[e.key])return 1;editor_held[e.key]=true;}if(e.key==VK_ESCAPE){naming=false;saved_markers.error.clear();}else if(e.key==VK_RETURN)finish_marker();else if(e.key==VK_BACK){if(replace_name)editing_name.clear();else mapmarkers::backspace(editing_name);replace_name=false;}return 1;}
  if(e.kind==ANY_MOUSE_UP&&e.button==1){PointF mouse(float(e.x),float(e.y));if(editor_save.Contains(mouse))finish_marker();else if(editor_cancel.Contains(mouse))naming=false;
   else if(editor_route.Contains(mouse)){uint64_t id=editing_marker?editing_marker:saved_markers.next;if(finish_marker()){if(!saved_markers.commit(saved_markers.records,id)){naming=true;return 1;}auto pin=saved_markers.find(id);if(pin){target=pin->position;waypoint=true;route={};routed_at=0;save_waypoint();update_route();log_event("MARKER_ROUTE selected=1");}}}
   else if(editing_marker&&editor_delete.Contains(mouse)){bool active=saved_markers.active==editing_marker;if(saved_markers.erase(editing_marker)){if(active)clear_route();naming=false;log_event("MARKER_DELETE saved=1");}}return 1;}
  return 1;
 }
 if(e.kind==ANY_KEY_DOWN||e.kind==ANY_KEY_UP){
  if(e.key&&e.key==map_key()){if(m_key.event(e.kind==ANY_KEY_DOWN,last_focus))toggle_map();return last_focus?1:0;}
  if(!visible)return 0;
  if(e.kind==ANY_KEY_DOWN){if(e.key==VK_ESCAPE){close_map();return 1;}if(e.key=='F')center_player();
   if(e.key==VK_HOME){view={};zoom_motion.cancel(view);}if(e.key==VK_ADD||e.key==VK_OEM_PLUS)zoom_motion.request(view,1.25,{.5,.5});if(e.key==VK_SUBTRACT||e.key==VK_OEM_MINUS)zoom_motion.request(view,.8,{.5,.5});
  }return 1;
 }
 if(!visible||map_side<=0)return 0;auto p=mouse_point(e.x,e.y);
 PointF mouse(float(e.x),float(e.y));
 if(e.kind==ANY_MOUSE_WHEEL&&marker_list.Contains(mouse)){marker_scroll-=e.wheel>0?1:-1;painted_at=0;return 1;}
 if(e.kind==ANY_MOUSE_UP&&e.button==1&&!dragging){
  for(auto& row:marker_rows)if(row.first.Contains(mouse)){selected_marker=row.second;auto pin=saved_markers.find(selected_marker);if(pin){atlas::project(pin->position.x,pin->position.y,view.center);view.clamp();zoom_motion.cancel(view);}painted_at=0;return 1;}
  if(selected_route.Contains(mouse)){auto pin=saved_markers.find(selected_marker);atlas::Point destination=pin?pin->position:atlas::Point{};if(pin&&saved_markers.commit(saved_markers.records,pin->id)){target=destination;waypoint=true;route={};routed_at=0;save_waypoint();update_route();log_event("MARKER_LIST_ROUTE selected=1");}painted_at=0;return 1;}
  if(selected_edit.Contains(mouse)){auto pin=saved_markers.find(selected_marker);if(pin)begin_marker(pin->id,pin->position);return 1;}
  if(route_clear.Contains(mouse)){clear_route();painted_at=0;return 1;}
  if(find_player.Contains(mouse)){center_player();painted_at=0;return 1;}
  if(move_button.Contains(mouse)){moving_minimap=true;painted_at=0;return 1;}
  if(reset_position.Contains(mouse)){custom_position=false;save_position();painted_at=0;log_event("MINIMAP_POSITION reset=1");return 1;}
 }
 if(e.kind==ANY_MOUSE_WHEEL){if(inside(p))zoom_motion.request(view,std::pow(1.25,e.wheel/120.),p);return 1;}
 if(e.kind==ANY_MOUSE_DOWN&&e.button==1){if(inside(p)){last_mouse=p;dragging=true;dragged=false;zoom_motion.cancel(view);}return 1;}
 if(e.kind==ANY_MOUSE_MOVE&&dragging){atlas::Point delta{p.x-last_mouse.x,p.y-last_mouse.y};if(dragged||std::hypot(delta.x,delta.y)*map_side>5){dragged=true;view.pan(delta);last_mouse=p;}return 1;}
 if(e.kind==ANY_MOUSE_UP&&e.button==1&&dragging){dragging=false;if(!dragged&&inside(p)){auto at=atlas::world(view.inverse(p));uint64_t found{};double distance=16;for(auto& pin:saved_markers.records){auto q=view.screen({.5+(pin.position.x-atlas::center_x)/(atlas::half_width*2),.5+(pin.position.y-atlas::center_z)/(atlas::half_width*2)});double gap=std::hypot(q.x-p.x,q.y-p.y)*map_side;if(gap<distance){distance=gap;found=pin.id;}}
 if(shift_held[0]||shift_held[1]||shift_held[2])begin_marker(0,at);else if(found)begin_marker(found,saved_markers.find(found)->position);else{target=at;waypoint=true;route={};routed_at=0;if(saved_markers.active)saved_markers.commit(saved_markers.records,0);save_waypoint();update_route();}}return 1;}
 if(e.kind==ANY_MOUSE_UP&&e.button==2){waypoint=false;route={};routed_at=0;save_waypoint();return 1;}return 1;
}
static std::vector<uint8_t> canvas_pixels;static uint64_t canvas_revision{};static bool painted_full{};static uint32_t painted_width{},painted_height{};
static atlas::PoseSmoother pose_motion;
// Every presentation submits small GPU commands. Terrain and fonts stay cached;
// player smoothing is presentation-only and preserves the calibrated camera basis.
static void gpu_render(const AnyFrameV1* frame,void*){
 if(!frame||frame->struct_size!=sizeof(*frame)||frame->width<100||frame->height<200||frame->width>8192||frame->height>8192)return;
 std::lock_guard<std::mutex> lock(state_mutex);last_focus=frame->focused!=0;frame_width=frame->width;frame_height=frame->height;read_settings();
 if(!last_focus){pose_motion.reset();if(visible)close_map();return;}
 if((ui_api&&!gameplay_ui())||(menu_api&&menu_api->settings_open())){pose_motion.reset();m_key.held=false;if(visible)close_map();return;}
 live=host.copy_players(&players,&ui_flags);if(!live){players={};route={};routed_at=0;pose_motion.reset();}
 if(live&&(!local_player()||(routed_at&&route_epoch!=players.world_epoch))){route={};routed_at=0;}update_route();
 if(!visible&&(!options.minimap||!live||!local_player()||(!ui_api&&(!(ui_flags&2)||(ui_flags&1))))){pose_motion.reset();return;}
 if(live)pose_motion.update(players,frame->tick);if(visible)zoom_motion.advance(view,frame->tick);
 float scale=float(options.size/100);int w=visible?frame->width:int(320*scale),h=visible?frame->height:int((waypoint&&options.guide?385:300)*scale);
 MapGraphics g(gpu_api,w,h,visible?1.f:float(options.opacity/100));
 if(visible)draw(g,w,h,false);else{auto position=minimap_position(frame->width,frame->height,w,h);g.TranslateTransform(position.X,position.Y);g.ScaleTransform(scale,scale);minimap_draw(g);}
}
// Refresh copied API data and provide a premultiplied bitmap for the game's own back buffer.
static void mod_render(const AnyFrameV1* frame,AnyCanvasV1* canvas,void*){
 if(!frame||!canvas||frame->struct_size!=sizeof(*frame)||frame->width<100||frame->height<200||frame->width>8192||frame->height>8192)return;
 if(gpu_api)return;std::lock_guard<std::mutex> lock(state_mutex);last_focus=frame->focused!=0;frame_width=frame->width;frame_height=frame->height;if(read_settings())painted_at=0;
 if(!last_focus){if(visible)close_map();return;}if((ui_api&&!gameplay_ui())||(menu_api&&menu_api->settings_open())){m_key.held=false;if(visible)close_map();return;}live=host.copy_players(&players,&ui_flags);
 if(!live){players={};route={};routed_at=0;}
 if(live&&(!local_player()||(routed_at&&route_epoch!=players.world_epoch))){route={};routed_at=0;}update_route();
 if(!visible&&(!options.minimap||!live||!local_player()||(!ui_api&&(!(ui_flags&2)||(ui_flags&1)))))return;
 float scale=float(options.size/100);if(visible&&(!painted_at||frame->tick-painted_at>=16))zoom_motion.advance(view,frame->tick);uint32_t w=visible?frame->width:uint32_t(320*scale),h=visible?frame->height:uint32_t((waypoint&&options.guide?385:300)*scale);
 if(!painted_at||frame->tick-painted_at>=(visible?16:33)||painted_full!=visible||painted_width!=w||painted_height!=h){
  if(!frame_bitmap||frame_bitmap->GetWidth()!=w||frame_bitmap->GetHeight()!=h)frame_bitmap=std::make_unique<Bitmap>(w,h,PixelFormat32bppPARGB);auto& bitmap=*frame_bitmap;{Graphics native(&bitmap);MapGraphics g(native);if(visible)draw(g,w,h,false);else {g.ScaleTransform(scale,scale);minimap_draw(g);}}
  Rect rect(0,0,w,h);BitmapData data{};if(bitmap.LockBits(&rect,ImageLockModeRead,PixelFormat32bppPARGB,&data)!=Ok)return;
  canvas_pixels.resize(size_t(w)*h*4);for(uint32_t y=0;y<h;++y)memcpy(canvas_pixels.data()+size_t(y)*w*4,(uint8_t*)data.Scan0+ptrdiff_t(y)*data.Stride,size_t(w)*4);bitmap.UnlockBits(&data);
  if(!visible&&options.opacity<100)for(auto& byte:canvas_pixels)byte=uint8_t(byte*options.opacity/100);
  ++canvas_revision;painted_at=frame->tick;painted_full=visible;painted_width=w;painted_height=h;
 }
 canvas->width=w;canvas->height=h;canvas->pitch=w*4;canvas->pixels=canvas_pixels.data();canvas->revision=canvas_revision;
auto position=minimap_position(frame->width,frame->height,w,h);canvas->x=visible?0:int(position.X);canvas->y=visible?0:int(position.Y);
}
// Release plugin-owned graphics resources if the host performs an orderly shutdown.
static void mod_shutdown(void*){std::lock_guard<std::mutex> lock(state_mutex);host.capture_input(0);for(auto& layer:layers)layer.reset();terrain_cache.reset();frame_bitmap.reset();canvas_pixels.clear();MapGraphics::reset_uploads();if(gdiplus_token){GdiplusShutdown(gdiplus_token);gdiplus_token=0;}}
// Normal DLL plugin entry point, invoked by the host after its build guard succeeds.
extern "C" __declspec(dllexport) bool AnyAPI_ModInit(const AnyModHostV1* api,AnyModCallbacksV1* callbacks){
 if(!api||!callbacks||api->struct_size!=sizeof(*api)||api->abi!=ANYAPI_MOD_ABI||!api->log||!api->copy_players||!api->capture_input||!api->game_directory||!api->plugin_directory)return false;
 host=*api;game_dir=api->game_directory;cache_dir=std::filesystem::path(api->plugin_directory)/L"AnyMap";std::filesystem::create_directories(cache_dir);
 GdiplusStartupInput input;if(GdiplusStartup(&gdiplus_token,&input,nullptr)!=Ok)return false;
 HRSRC resource=FindResourceW(mod_module,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(10));HGLOBAL memory=resource?LoadResource(mod_module,resource):nullptr;
 const char* data=memory?(const char*)LockResource(memory):nullptr;DWORD bytes=resource?SizeofResource(mod_module,resource):0;
 if(!data||!bytes){log_event("INIT_FAILED_ROAD_RESOURCE");mod_shutdown(nullptr);return false;}
 std::istringstream stream(std::string(data,bytes),std::ios::binary);if(!road_graph.load(stream)){log_event("INIT_FAILED_ROAD_CACHE");mod_shutdown(nullptr);return false;}
 const wchar_t* names[]={L"map_background.txtr",L"map_trees.txtr",L"map_lines.txtr"};for(int i=0;i<3;++i){layers[i]=texture(game_dir/L"rom"/L"textures"/names[i],i);if(!layers[i]){log_event("INIT_FAILED_GAME_TEXTURE");mod_shutdown(nullptr);return false;}}
 water_mask.clear();water_mask.shrink_to_fit();auto composed=std::make_unique<Bitmap>(4096,4096,PixelFormat32bppARGB);
 {Graphics g(composed.get());g.Clear(Color(0,0,0,0));for(auto& layer:layers)g.DrawImage(layer.get(),0,0,4096,4096);}for(auto& layer:layers)layer.reset();layers[0]=std::move(composed);
 saved_markers.file=cache_dir/L"markers.tsv";saved_markers.load();auto marker_log="MARKERS_LOADED count="+std::to_string(saved_markers.records.size())+" rejected="+std::to_string(saved_markers.rejected);host.log(0,"anymap",marker_log.c_str());
 load_waypoint();load_position();callbacks->id="anymap";callbacks->render=mod_render;callbacks->input=mod_input;callbacks->shutdown=mod_shutdown;
 log_event("DLL_READY bind=M embedded_roads=1 external_helpers=0");return true;
}
// DllMain records its handle only; allocation, texture loading and API calls occur in ModInit.
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){mod_module=module;DisableThreadLibraryCalls(module);}return TRUE;}

