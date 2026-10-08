#pragma once
#include "anyapi_players_v1.h"
#include <cmath>
#include <cstring>
#include <algorithm>
// Presentation/calibration belongs to the map mod, not to the player API.
namespace atlas {
constexpr double center_x=81000,center_z=52000,half_width=6508;
struct Point {double x{},y{};};
// Convert finite world X/Z into atlas UV coordinates. Bounds are checked separately by callers.
inline bool project(double x,double z,Point& uv) {
    if(!std::isfinite(x)||!std::isfinite(z))return false;
    uv={.5+(x-center_x)/(half_width*2),.5+(z-center_z)/(half_width*2)};
    return std::isfinite(uv.x)&&std::isfinite(uv.y);
}
// Convert atlas UV coordinates back to world X/Z.
inline Point world(Point uv){return {center_x+(uv.x-.5)*half_width*2,center_z+(uv.y-.5)*half_width*2};}
// Convert native yaw into the north-up map arrow drawing angle.
inline double screen_heading(double yaw){return yaw;}
// Native view forward is +Z and camera-right is +X at zero yaw. Atlas +Z
// points down, so heading-up needs a reflected basis, not a pure rotation.
inline Point minimap_point(Point p,double yaw,bool north_up=false){
    if(north_up)return {p.x,-p.y};
    const double c=std::cos(yaw),s=std::sin(yaw);
    return {p.x*c-p.y*s,-p.x*s-p.y*c};
}
// A positive relative yaw turns the arrow toward screen-right.
inline double minimap_marker_heading(double yaw,double local_yaw){return yaw-local_yaw;}
struct KeyEdge {
    bool held{};
    // Track M key press/release edges so held-key repeats cannot toggle repeatedly and unfocused presses are ignored.
    bool event(bool down,bool focused){bool trigger=down&&!held&&focused;held=down;return trigger;}
};
struct View {
    Point center{.5,.5};double zoom{1};
    // Apply view zoom and pan to atlas UV coordinates.
    Point screen(Point uv)const{return {.5+(uv.x-center.x)*zoom,.5-(uv.y-center.y)*zoom};}
    // Undo view zoom and pan to recover atlas UV coordinates from a displayed point.
    Point inverse(Point p)const{return {center.x+(p.x-.5)/zoom,center.y-(p.y-.5)/zoom};}
    // Keep the visible area inside atlas bounds at the current zoom.
    void clamp(){double r=.5/zoom;center.x=std::clamp(center.x,r,1-r);center.y=std::clamp(center.y,r,1-r);}
    // Zoom between 1x and 8x while preserving the atlas point under the cursor where bounds permit.
    void magnify(double factor,Point anchor){auto uv=inverse(anchor);zoom=std::clamp(zoom*factor,1.,8.);center={uv.x-(anchor.x-.5)/zoom,uv.y+(anchor.y-.5)/zoom};clamp();}
    // Move the view by a displayed drag delta, then clamp to atlas bounds.
    void pan(Point delta){center.x-=delta.x/zoom;center.y+=delta.y/zoom;clamp();}
};
// Rotate a 2D point by radians using screen-coordinate sine and cosine.
inline Point rotate(Point p,double yaw) {
    const double s=std::sin(yaw),c=std::cos(yaw);return {p.x*c-p.y*s,p.x*s+p.y*c};
}
// Validate snapshot version, size, capacity, timestamp age, field flags, finite coordinates and terminated strings.
inline bool fresh(const AnySessionPlayersV1& s,uint64_t now) {
    if(!(s.struct_size==sizeof(s)&&s.version==1&&s.count<=ANYAPI_PLAYERS_CAPACITY&&
      s.sampled_tick&&now>=s.sampled_tick&&now-s.sampled_tick<=500))return false;
    for(uint32_t i=0;i<s.count;++i){const auto& p=s.players[i];
        if(p.valid_fields&~uint32_t(31))return false;
        if((p.valid_fields&PLAYER_POSITION)&&(!std::isfinite(p.position[0])||!std::isfinite(p.position[1])||!std::isfinite(p.position[2])))return false;
        if((p.valid_fields&PLAYER_FACING)&&!std::isfinite(p.yaw_radians))return false;
        if((p.valid_fields&PLAYER_NAME)&&!std::memchr(p.steam_name,0,sizeof(p.steam_name)))return false;
        if((p.valid_fields&PLAYER_STEAM_ID)&&!std::memchr(p.steam_id,0,sizeof(p.steam_id)))return false;
    }
    return true;
}
}

