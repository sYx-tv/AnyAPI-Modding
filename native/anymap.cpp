#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <windowsx.h>
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
using namespace Gdiplus;
static DWORD game_pid{};static HANDLE game_process{},channel_handle{};
static const AnyPlayersChannelV1* channel{};static HWND window{},game_window{};
static std::filesystem::path game_dir,cache_dir;static bool visible{};
static atlas::KeyEdge m_key;static atlas::View view;static HWND hud{};
static roads::Graph road_graph;static roads::Route route;
static bool waypoint{},dragging{},dragged{},live{};static atlas::Point target{},last_mouse{};
static uint64_t routed_at{},route_epoch{};static roads::Point routed_from{};
static float map_left{},map_top{},map_side{};
// Append a timestamped helper event to anymap.log for startup and input diagnosis.
static void log_event(const char* event){std::ofstream f(cache_dir/L"anymap.log",std::ios::app);f<<GetTickCount64()<<" "<<event<<"\n";}


static std::array<std::unique_ptr<Bitmap>,3> layers;
static std::vector<unsigned char> water_mask;
static AnySessionPlayersV1 players{};
static uint32_t ui_flags{};
// Find the snapshot entry marked local that also has a valid world position; return null if unavailable.
static const AnySessionPlayerV1* local_player(){for(uint32_t i=0;i<players.count;++i)if((players.players[i].valid_fields&(PLAYER_LOCAL|PLAYER_POSITION))==(PLAYER_LOCAL|PLAYER_POSITION))return &players.players[i];return nullptr;}
// Convert world X/Z into a pixel on the full map, applying atlas calibration, zoom and pan.
static PointF pixel(double x,double z){atlas::Point uv;atlas::project(x,z,uv);uv=view.screen(uv);return {map_left+float(uv.x)*map_side,map_top+float(uv.y)*map_side};}
// Convert a Windows mouse position into normalized coordinates inside the displayed map rectangle.
static atlas::Point mouse_point(LPARAM lp){return {(GET_X_LPARAM(lp)-map_left)/map_side,(GET_Y_LPARAM(lp)-map_top)/map_side};}
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
static void text(Graphics& g,const std::wstring& s,float x,float y,float size,Color color,bool bold=false) {
    FontFamily family(L"Segoe UI");Font font(&family,size,bold?FontStyleBold:FontStyleRegular,UnitPixel);
    SolidBrush brush(color);StringFormat format;format.SetFormatFlags(StringFormatFlagsNoWrap);
    g.DrawString(s.c_str(),int(s.size()),&font,PointF(x,y),&format,&brush);
}
// Draw a filled rounded panel behind labels or the map UI.
static void round_rect(Graphics& g,RectF r,float radius,Color color) {
    GraphicsPath path;float d=radius*2;
    path.AddArc(r.X,r.Y,d,d,180,90);path.AddArc(r.GetRight()-d,r.Y,d,d,270,90);
    path.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);path.AddArc(r.X,r.GetBottom()-d,d,d,90,90);
    path.CloseFigure();SolidBrush brush(color);g.FillPath(&brush,&path);
}
// Draw a small local or remote player arrow, or a dot if facing is unavailable; optionally show the Steam name or override screen heading.
static void marker(Graphics& g,const AnySessionPlayerV1& p,PointF at,bool labels,double heading=std::numeric_limits<double>::quiet_NaN()) {
    const bool local=(p.valid_fields&PLAYER_LOCAL)!=0;const double angle=std::isfinite(heading)?heading:((p.valid_fields&PLAYER_FACING)?atlas::screen_heading(p.yaw_radians):0);
    const float radius=local?1.0f:.92f;
    const atlas::Point shape[]={{0,-7},{4.7,5},{0,2},{-4.7,5}};
    PointF outline[4];for(int i=0;i<4;++i){auto q=atlas::rotate(shape[i],angle);outline[i]={at.X+float(q.x)*radius,at.Y+float(q.y)*radius};}
    SolidBrush shadow(Color(100,0,0,0));g.FillEllipse(&shadow,at.X-7,at.Y-4,14.f,12.f);
    Pen stroke(Color(245,26,44,39),2.0f);stroke.SetLineJoin(LineJoinRound);
    SolidBrush fill(local?Color(255,250,247,220):Color(255,125,230,194));
    if(p.valid_fields&PLAYER_FACING){g.FillPolygon(&fill,outline,4);g.DrawPolygon(&stroke,outline,4);}
    else {g.FillEllipse(&fill,at.X-3,at.Y-3,6.f,6.f);g.DrawEllipse(&stroke,at.X-3,at.Y-3,6.f,6.f);}
    if(labels&&(p.valid_fields&PLAYER_NAME)){
        auto name=utf16(p.steam_name);if(name.empty())return;
        FontFamily family(L"Segoe UI");Font font(&family,12,FontStyleRegular,UnitPixel);RectF measured;
        g.MeasureString(name.c_str(),int(name.size()),&font,PointF(0,0),&measured);
        float width=std::min(235.f,measured.Width+14),x=at.X-width/2,y=at.Y+12;
        round_rect(g,{x,y,width,23},6,Color(224,26,44,39));
        SolidBrush ink(Color(255,244,245,229));StringFormat label;label.SetFormatFlags(StringFormatFlagsNoWrap);
        label.SetTrimming(StringTrimmingEllipsisCharacter);g.DrawString(name.c_str(),int(name.size()),&font,RectF(x+7,y+3,width-14,18),&label,&ink);
    }
}
// Render the full atlas with zoom, road route, waypoint, players, names, status and controls.
static void draw(Graphics& g,int w,int h,bool preview) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);g.Clear(Color(255,12,22,20));
    float side=float(std::min(w-80,h-182)),left=(w-side)/2,top=88;map_left=left;map_top=top;map_side=side;
    round_rect(g,{left-17,top-64,side+34,side+111},18,Color(255,26,41,37));
    text(g,L"ANYMAKER  /  ATLAS",left,top-45,19,Color(255,235,239,219),true);
    text(g,preview?L"VISUAL PREVIEW · SAMPLE PLAYERS":L"M / ESC  CLOSE",left+side-230,top-42,12,Color(255,157,180,165));
    GraphicsState state=g.Save();g.SetClip(RectF(left,top,side,side));
    RectF map_rect(left,top,side,side);
    for(auto& layer:layers)if(layer){float size=float(layer->GetWidth()),source_size=size/float(view.zoom);
        g.DrawImage(layer.get(),map_rect,float(view.center.x)*size-source_size/2,float(view.center.y)*size-source_size/2,source_size,source_size,UnitPixel);}
    Pen grid(Color(20,39,68,54),1);
    for(int i=1;i<8;++i){auto q=view.screen({i/8.,i/8.});g.DrawLine(&grid,left+float(q.x)*side,top,left+float(q.x)*side,top+side);g.DrawLine(&grid,left,top+float(q.y)*side,left+side,top+float(q.y)*side);}
    if(waypoint){
        Pen line(Color(255,217,114,217),4);line.SetLineJoin(LineJoinRound);
        for(size_t i=1;i<route.points.size();++i){auto a=pixel(route.points[i-1].x,route.points[i-1].z),b=pixel(route.points[i].x,route.points[i].z);g.DrawLine(&line,a,b);}
        Pen approach(Color(230,217,114,217),2);approach.SetDashStyle(DashStyleDash);
        if(route.connected&&!route.points.empty()){auto p=local_player();if(p)g.DrawLine(&approach,pixel(p->position[0],p->position[2]),pixel(route.points.front().x,route.points.front().z));g.DrawLine(&approach,pixel(route.points.back().x,route.points.back().z),pixel(target.x,target.y));}
        auto at=pixel(target.x,target.y);SolidBrush pin(Color(255,239,154,227));Pen rim(Color(255,51,37,57),2);
        g.DrawLine(&rim,at.X,at.Y-8,at.X,at.Y+3);g.FillEllipse(&pin,at.X-5,at.Y-13,10.f,10.f);g.DrawEllipse(&rim,at.X-5,at.Y-13,10.f,10.f);
    }
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
    text(g,L"WHEEL  ZOOM  ·  DRAG  PAN  ·  CLICK  WAYPOINT  ·  RIGHT CLICK  CLEAR  ·  F  FIND ME",left,top+side+47,11,Color(255,172,192,171));
    if(waypoint)text(g,route.connected?L"ROAD ROUTE  "+meters(route.length):L"NO CONNECTED ROAD ROUTE — WAYPOINT SAVED",left,top-21,11,Color(255,230,159,224));
    const float scale=side*250.f*float(view.zoom)/float(atlas::half_width*2);
    Pen scale_pen(Color(255,180,198,173),2);float sx=left+side-scale,sy=top+side+30;
    g.DrawLine(&scale_pen,sx,sy,sx+scale,sy);g.DrawLine(&scale_pen,sx,sy-4,sx,sy+3);g.DrawLine(&scale_pen,sx+scale,sy-4,sx+scale,sy+3);
    text(g,L"250 m",sx+scale/2-13,sy-18,10,Color(255,180,198,173));
}
// Windows enumeration callback: locate the visible unowned main window belonging to the game process.
static BOOL CALLBACK find_window(HWND h,LPARAM) {
    DWORD pid{};GetWindowThreadProcessId(h,&pid);
    if(pid==game_pid&&IsWindowVisible(h)&&GetWindow(h,GW_OWNER)==nullptr){game_window=h;return FALSE;}return TRUE;
}
// Open the read-only shared player channel and copy a coherent fresh snapshot plus UI flags using its sequence counter.
static bool read_players() {
    if(!channel){wchar_t name[80]{};swprintf_s(name,L"Local\\AnyAPI.Players.%lu",game_pid);
        channel_handle=OpenFileMappingW(FILE_MAP_READ,FALSE,name);
        if(channel_handle)channel=(const AnyPlayersChannelV1*)MapViewOfFile(channel_handle,FILE_MAP_READ,0,0,sizeof(AnyPlayersChannelV1));}
    if(!channel||channel->magic!=0x41504c59||channel->version!=1||channel->struct_size!=sizeof(AnyPlayersChannelV1))return false;
    for(int attempt=0;attempt<4;++attempt){int64_t before=channel->sequence;if(before&1)continue;
        MemoryBarrier();AnySessionPlayersV1 copy{};memcpy(&copy,&channel->snapshot,sizeof(copy));uint32_t flags=channel->reserved;MemoryBarrier();
        if(before==channel->sequence&&!(before&1)&&atlas::fresh(copy,GetTickCount64())){players=copy;ui_flags=flags;return true;}}
    return false;
}
// Check whether the game or full-map window currently owns keyboard focus.
static bool focused(){DWORD pid{};GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==game_pid||GetForegroundWindow()==window;}
// Hide the full map and return focus to the game when the map owned it; log the close.
static void close_map(){bool own_focus=GetForegroundWindow()==window;visible=false;ShowWindow(window,SW_HIDE);if(game_window&&own_focus)SetForegroundWindow(game_window);log_event("MAP_CLOSED");}
// Open the atlas over the game client area, or close it if already open.
static void toggle_map(){if(visible){close_map();return;}visible=true;EnumWindows(find_window,0);
 RECT r{};if(!game_window||IsIconic(game_window)||!GetClientRect(game_window,&r)){visible=false;return;}POINT at{};ClientToScreen(game_window,&at);
 SetWindowPos(window,HWND_TOPMOST,at.x,at.y,r.right,r.bottom,SWP_SHOWWINDOW);SetForegroundWindow(window);InvalidateRect(window,nullptr,FALSE);log_event("MAP_OPENED");}
// Render remaining route distance, road approach and upcoming turn guidance, plus a relative direction arrow.
static void hud_draw(Graphics& g){g.SetSmoothingMode(SmoothingModeAntiAlias);SolidBrush background(Color(255,26,41,37));g.FillRectangle(&background,0,0,320,85);auto p=local_player();if(!p)return;
 roads::Point at{p->position[0],p->position[1],p->position[2]};double gap=roads::flat(at,{target.x,0,target.y});
 std::wstring action=L"No connected road route",detail=L"Waypoint "+meters(gap);
 if(gap<15){action=L"Waypoint reached";detail=L"Right click the map to clear";}
 else if(route.connected&&route.points.size()>1){
  if(route.start_gap>25){action=L"Join the highlighted road";detail=meters(route.start_gap)+L" to road  ·  "+meters(route.length)+L" remaining";}
  else if(route.length<route.end_gap+30){action=L"Leave road for waypoint";detail=meters(gap)+L" to waypoint";}
  else {action=L"Follow highlighted road";detail=meters(route.length)+L" remaining";
   double covered=0;for(size_t i=2;i<route.points.size();++i){auto a=route.points[i-2],b=route.points[i-1],c=route.points[i];
    covered+=roads::flat(a,b);if(covered>500)break;if(roads::flat(a,b)<.01||roads::flat(b,c)<.01)continue;
    double angle=std::atan2((b.x-a.x)*(c.z-b.z)-(b.z-a.z)*(c.x-b.x),(b.x-a.x)*(c.x-b.x)+(b.z-a.z)*(c.z-b.z));
    if(std::abs(angle)>.30&&covered>15){action=angle>0?L"Turn right":L"Turn left";detail=L"In "+meters(covered)+L"  ·  "+meters(route.length)+L" remaining";break;}
   }
  }
 }
 text(g,action,15,12,16,Color(255,239,220,239),true);text(g,detail,15,38,12,Color(255,189,211,191));
 text(g,L"M  VIEW ROUTE",15,60,10,Color(255,163,184,170));
 roads::Point toward{target.x,0,target.y};if(route.connected&&!route.points.empty()){
  toward=route.points.front();if(route.start_gap<=25){double ahead=0;for(size_t i=1;i<route.points.size();++i){ahead+=roads::flat(route.points[i-1],route.points[i]);toward=route.points[i];if(ahead>35)break;}}}
 if(p->valid_fields&PLAYER_FACING){double angle=p->yaw_radians-std::atan2(toward.x-at.x,toward.z-at.z);atlas::Point shape[]={{0,-10},{6,7},{0,3},{-6,7}};PointF polygon[4];
  for(int i=0;i<4;++i){auto q=atlas::rotate(shape[i],angle);polygon[i]={290+float(q.x),65+float(q.y)};}SolidBrush ink(Color(255,236,146,225));g.FillPolygon(&ink,polygon,4);}
}
// Render the nearby square map, counter-rotating terrain and route with yaw; keep the local arrow up and show other players relative to it.
static void minimap_draw(Graphics& g){g.Clear(Color(255,26,41,37));g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
 auto p=local_player();if(!p)return;atlas::Point uv;atlas::project(p->position[0],p->position[2],uv);constexpr float side=256,range=1200;
 auto saved=g.Save();g.SetClip(RectF(12,12,side,side));auto transform=atlas::minimap_matrix(p->yaw_radians,false,140,140);g.SetTransform(&transform);
 float pixels=4096,source=float(1800/(atlas::half_width*2))*pixels;
 if(layers[0])g.DrawImage(layers[0].get(),RectF(-192,-192,384,384),float(uv.x)*pixels-source/2,float(uv.y)*pixels-source/2,source,source,UnitPixel);
 auto at=[&](double x,double z){return PointF(float((x-p->position[0])*side/range),float((z-p->position[2])*side/range));};
 Pen line(Color(255,222,119,220),3);for(size_t i=1;i<route.points.size();++i)g.DrawLine(&line,at(route.points[i-1].x,route.points[i-1].z),at(route.points[i].x,route.points[i].z));
 if(waypoint){auto pin=at(target.x,target.y);SolidBrush brush(Color(255,239,154,227));g.FillEllipse(&brush,pin.X-4,pin.Y-4,8.f,8.f);}g.Restore(saved);
 // Draw arrows in screen space so terrain rotation cannot rotate the local arrow twice.
 auto markers=g.Save();g.SetClip(RectF(12,12,side,side));
 for(uint32_t i=0;i<players.count;++i){const auto& other=players.players[i];if(!(other.valid_fields&PLAYER_POSITION))continue;
  auto offset=at(other.position[0],other.position[2]);auto q=atlas::minimap_point({offset.X,offset.Y},p->yaw_radians);
  marker(g,other,{140+float(q.x),140+float(q.y)},false,atlas::minimap_marker_heading(other.yaw_radians,p->yaw_radians));
 }g.Restore(markers);
 Pen border(Color(255,120,151,125),1);g.DrawRectangle(&border,12.f,12.f,side,side);
 text(g,L"M  ATLAS",14,273,10,Color(255,188,208,187));
 if(waypoint){g.TranslateTransform(0,300);hud_draw(g);g.ResetTransform();}
}
// Full-map Windows message handler: process M input, zoom, drag, waypoint clicks, snapshot refresh, visibility, painting and shutdown.
static LRESULT CALLBACK procedure(HWND h,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_INPUT){UINT size=sizeof(RAWINPUT);RAWINPUT input{};
        UINT received=GetRawInputData((HRAWINPUT)lp,RID_INPUT,&input,&size,sizeof(RAWINPUTHEADER));
        if(received!=UINT(-1)&&received>=sizeof(RAWINPUTHEADER)+sizeof(RAWKEYBOARD)&&input.header.dwType==RIM_TYPEKEYBOARD){
            auto key=input.data.keyboard;if(key.VKey=='M'&&m_key.event(!(key.Flags&RI_KEY_BREAK),focused()))toggle_map();
        }return DefWindowProcW(h,message,wp,lp);
    }
    if(message==WM_KEYDOWN&&visible){if(wp==VK_ESCAPE){close_map();return 0;}if(wp=='F'){auto p=local_player();if(p){atlas::project(p->position[0],p->position[2],view.center);view.clamp();}}
        if(wp==VK_HOME)view={};if(wp==VK_ADD||wp==VK_OEM_PLUS)view.magnify(1.25,{.5,.5});if(wp==VK_SUBTRACT||wp==VK_OEM_MINUS)view.magnify(.8,{.5,.5});InvalidateRect(h,nullptr,FALSE);return 0;}
    if(message==WM_MOUSEWHEEL&&visible){POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(h,&p);atlas::Point anchor{(p.x-map_left)/map_side,(p.y-map_top)/map_side};
        if(inside(anchor))view.magnify(std::pow(1.25,GET_WHEEL_DELTA_WPARAM(wp)/120.),anchor);InvalidateRect(h,nullptr,FALSE);return 0;}
    if(message==WM_LBUTTONDOWN&&visible&&map_side>0){last_mouse=mouse_point(lp);if(inside(last_mouse)){dragging=true;dragged=false;SetCapture(h);}return 0;}
    if(message==WM_MOUSEMOVE&&dragging){auto p=mouse_point(lp);atlas::Point delta{p.x-last_mouse.x,p.y-last_mouse.y};
        if(dragged||std::hypot(delta.x,delta.y)*map_side>5){dragged=true;view.pan(delta);last_mouse=p;InvalidateRect(h,nullptr,FALSE);}return 0;}
    if(message==WM_LBUTTONUP&&dragging){ReleaseCapture();dragging=false;auto p=mouse_point(lp);if(!dragged&&inside(p)){target=atlas::world(view.inverse(p));waypoint=true;route={};routed_at=0;save_waypoint();update_route();}InvalidateRect(h,nullptr,FALSE);return 0;}
    if(message==WM_CAPTURECHANGED){dragging=false;return 0;}
    if(message==WM_RBUTTONUP&&visible){waypoint=false;route={};routed_at=0;save_waypoint();InvalidateRect(h,nullptr,FALSE);return 0;}
    if(message==WM_TIMER){
        if(WaitForSingleObject(game_process,0)==WAIT_OBJECT_0){DestroyWindow(h);return 0;}
        bool focus=focused();live=read_players();
        if(visible&&!focus)close_map();
        if(live&&(!local_player()||(routed_at&&route_epoch!=players.world_epoch))){route={};routed_at=0;}
        if(!live&&GetForegroundWindow()!=window)players={};update_route();
        if(!game_window||!IsWindow(game_window))EnumWindows(find_window,0);
        RECT r{};POINT origin{};bool sized=game_window&&!IsIconic(game_window)&&GetClientRect(game_window,&r)&&r.right>80&&r.bottom>182;
        if(sized)ClientToScreen(game_window,&origin);
        if(visible&&focus&&sized){SetWindowPos(h,HWND_TOPMOST,origin.x,origin.y,r.right,r.bottom,SWP_NOACTIVATE|SWP_SHOWWINDOW);InvalidateRect(h,nullptr,FALSE);}else ShowWindow(h,SW_HIDE);
        if(!visible&&focus&&sized&&live&&local_player()&&(ui_flags&2)&&!(ui_flags&1)){SetWindowPos(hud,HWND_TOPMOST,origin.x+r.right-340,origin.y+25,320,waypoint?385:300,SWP_NOACTIVATE|SWP_SHOWWINDOW);InvalidateRect(hud,nullptr,FALSE);}else ShowWindow(hud,SW_HIDE);
        return 0;
    }
    if(message==WM_PAINT){PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);
        if(r.right>80&&r.bottom>182){Bitmap buffer(r.right,r.bottom,PixelFormat32bppARGB);Graphics bg(&buffer);draw(bg,r.right,r.bottom,false);Graphics screen(dc);screen.DrawImage(&buffer,0,0);}EndPaint(h,&ps);return 0;}
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_DESTROY){if(hud)DestroyWindow(hud);PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,message,wp,lp);
}
// Minimap Windows message handler: paint the overlay and pass mouse input through to the game.
static LRESULT CALLBACK hud_procedure(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_NCHITTEST)return HTTRANSPARENT;if(m==WM_ERASEBKGND)return 1;
 if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT rect{};GetClientRect(h,&rect);Bitmap buffer(320,rect.bottom,PixelFormat32bppARGB);Graphics bg(&buffer);minimap_draw(bg);Graphics g(dc);g.DrawImage(&buffer,0,0);EndPaint(h,&ps);return 0;}
 return DefWindowProcW(h,m,w,l);
}
// Find the GDI+ PNG encoder and save a rendered preview; report failure to the caller.
static bool save_png(Bitmap& image,const std::filesystem::path& path) {
    UINT count{},bytes{};GetImageEncodersSize(&count,&bytes);std::vector<unsigned char> memory(bytes);
    auto encoders=(ImageCodecInfo*)memory.data();GetImageEncoders(count,bytes,encoders);
    for(UINT i=0;i<count;++i)if(wcscmp(encoders[i].MimeType,L"image/png")==0)return image.Save(path.c_str(),&encoders[i].Clsid)==Ok;return false;
}
// Helper entry point: parse launch options, load map and roads, optionally export a sample preview, then run the windows until the game exits.
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    SetProcessDPIAware();GdiplusStartupInput input;ULONG_PTR token{};if(GdiplusStartup(&token,&input,nullptr)!=Ok)return 1;
    int argc{};auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);std::filesystem::path preview,roads_path;double preview_zoom=1;bool mini_preview=false;
    for(int i=1;i+1<argc;++i){if(!wcscmp(argv[i],L"--pid"))game_pid=wcstoul(argv[++i],nullptr,10);
        else if(!wcscmp(argv[i],L"--game"))game_dir=argv[++i];else if(!wcscmp(argv[i],L"--preview"))preview=argv[++i];
        else if(!wcscmp(argv[i],L"--roads"))roads_path=argv[++i];else if(!wcscmp(argv[i],L"--zoom"))preview_zoom=wcstod(argv[++i],nullptr);}
    for(int i=1;i<argc;++i)if(!wcscmp(argv[i],L"--minimap"))mini_preview=true;
    LocalFree(argv);wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);cache_dir=std::filesystem::path(executable).parent_path();
    road_graph.load(roads_path.empty()?cache_dir/L"roads.bin":roads_path);if(preview.empty())load_waypoint();
    const wchar_t* names[]={L"map_background.txtr",L"map_trees.txtr",L"map_lines.txtr"};
    for(int i=0;i<3;++i){layers[i]=texture(game_dir/L"rom"/L"textures"/names[i],i);if(!layers[i])return 2;}
    water_mask.clear();water_mask.shrink_to_fit();
    auto composed=std::make_unique<Bitmap>(4096,4096,PixelFormat32bppARGB);
    {Graphics cache(composed.get());cache.Clear(Color(0,0,0,0));for(auto& layer:layers)cache.DrawImage(layer.get(),0,0,4096,4096);}
    for(auto& layer:layers)layer.reset();layers[0]=std::move(composed);
    if(!preview.empty()){
        players.count=3;const char* names[]={"Player preview","Guest preview","Traveler preview"};
        const double offsets[][3]={{2955.394,3845.817,.012369},{1900,700,-.6},{700,-2100,2.7}};
        for(int i=0;i<3;++i){auto& p=players.players[i];p.valid_fields=PLAYER_POSITION|PLAYER_FACING|PLAYER_NAME|(i==0?PLAYER_LOCAL:0);
            p.position[0]=atlas::center_x+offsets[i][0];p.position[2]=atlas::center_z+offsets[i][1];p.yaw_radians=offsets[i][2];strcpy_s(p.steam_name,names[i]);}
        if(!road_graph.edges.empty()){auto p=local_player();roads::Point from{p->position[0],5.472,p->position[2]};target={86326.3,49637.6};waypoint=true;route=road_graph.route(from,{target.x,0,target.y});}
        if(std::isfinite(preview_zoom)){view.zoom=std::clamp(preview_zoom,1.,8.);if(view.zoom>1){atlas::project(players.players[0].position[0],players.players[0].position[2],view.center);view.clamp();}}
        Bitmap bitmap(mini_preview?320:1200,mini_preview?385:1040,PixelFormat32bppARGB);Graphics graphics(&bitmap);if(mini_preview)minimap_draw(graphics);else draw(graphics,1200,1040,true);return save_png(bitmap,preview)?0:3;
    }
    game_process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,game_pid);if(!game_process)return 4;
    WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=instance;wc.lpszClassName=L"AnyAPI.Map.native";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);
    window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,wc.lpszClassName,L"Anymaker Atlas",WS_POPUP,0,0,800,800,nullptr,nullptr,instance,nullptr);
    if(!window)return 5;
    RAWINPUTDEVICE keyboard{0x01,0x06,RIDEV_INPUTSINK,window};if(!RegisterRawInputDevices(&keyboard,1,sizeof(keyboard))){log_event("RAW_KEYBOARD_REGISTRATION_FAILED");return 6;}log_event("REVISION2_RAW_KEYBOARD_READY");
    WNDCLASSW hc=wc;hc.lpfnWndProc=hud_procedure;hc.lpszClassName=L"AnyAPI.Map.Navigation";RegisterClassW(&hc);
    hud=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT,hc.lpszClassName,L"Waypoint guidance",WS_POPUP,0,0,320,85,nullptr,nullptr,instance,nullptr);
    SetTimer(window,1,33,nullptr);MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    if(channel)UnmapViewOfFile(channel);if(channel_handle)CloseHandle(channel_handle);CloseHandle(game_process);
    for(auto& layer:layers)layer.reset();GdiplusShutdown(token);return 0;
}

