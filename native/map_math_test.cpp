#include "map_math.h"
#include <cassert>
#include <limits>
int main(){atlas::Point p{};
    assert(atlas::project(81000,52000,p)&&p.x==.5&&p.y==.5);
    assert(atlas::project(81000-6508,52000-6508,p)&&p.x==0&&p.y==0);
    assert(atlas::project(81000+6508,52000+6508,p)&&p.x==1&&p.y==1);
    assert(atlas::project(83955.394,55845.817,p)&&p.x>.72&&p.x<.73&&p.y>.79&&p.y<.80);
    auto world=atlas::world(p);assert(std::abs(world.x-83955.394)<1e-8&&std::abs(world.y-55845.817)<1e-8);
    auto south=atlas::rotate({0,-7},atlas::screen_heading(0));assert(south.y< -6.99);
    auto facing_east=atlas::rotate({0,-7},atlas::screen_heading(std::acos(-1.)*.5));assert(facing_east.x>6.99);
    const double pi=std::acos(-1.);
    // World landmarks ahead/right/left must land on the matching screen side
    // at every cardinal and diagonal heading, independently of yaw sign.
    for(double yaw:{-pi,-pi*.75,-pi*.5,-pi*.25,0.,pi*.25,pi*.5,pi*.75,pi}){
        atlas::Point forward{std::sin(yaw)*100,std::cos(yaw)*100};
        atlas::Point right{std::cos(yaw)*100,-std::sin(yaw)*100};
        auto ahead=atlas::minimap_point(forward,yaw);
        auto side=atlas::minimap_point(right,yaw);
        assert(std::abs(ahead.x)<1e-8&&ahead.y< -99);
        assert(side.x>99&&std::abs(side.y)<1e-8);
        auto arrow=atlas::rotate({0,-7},atlas::minimap_marker_heading(yaw,yaw));
        assert(std::abs(arrow.x)<1e-8&&arrow.y< -6.99);
        auto relative=atlas::rotate({0,-7},atlas::minimap_marker_heading(yaw+pi*.5,yaw));assert(relative.x>6.99);
    }
    // Full map and fixed-orientation minimap agree; click inverse and drag
    // must remain in world/atlas coordinates after the vertical correction.
    atlas::View fixed;auto north=fixed.screen({.5,.7});assert(north.y<.5);
    auto eastward=fixed.screen({.7,.5});assert(eastward.x>.5);
    auto recovered=fixed.inverse(north);assert(std::abs(recovered.y-.7)<1e-9);
    fixed.zoom=2;auto original=fixed.screen({.55,.55});fixed.pan({.1,.1});auto moved=fixed.screen({.55,.55});
    assert(std::abs(moved.x-original.x-.1)<1e-9&&std::abs(moved.y-original.y-.1)<1e-9);
    atlas::KeyEdge key;assert(key.event(true,true));assert(!key.event(true,true));assert(!key.event(false,true));
    assert(key.event(true,true));assert(!key.event(false,true));assert(!key.event(true,false));assert(!key.event(true,true));
    assert(!key.event(false,false));assert(key.event(true,true));
    atlas::View view;atlas::Point anchor{.6,.4};auto before=view.inverse(anchor);view.magnify(2,anchor);
    auto after=view.inverse(anchor);assert(std::abs(before.x-after.x)<1e-9&&std::abs(before.y-after.y)<1e-9);
    auto pixel=view.screen(after);assert(std::abs(pixel.x-anchor.x)<1e-9&&std::abs(pixel.y-anchor.y)<1e-9);
    view.magnify(100,anchor);assert(view.zoom==8);view.pan({100,100});assert(view.center.x==.5/8);
    assert(!atlas::project(std::numeric_limits<double>::quiet_NaN(),52000,p));
    auto east=atlas::rotate({0,-7},std::acos(-1.)*.5);assert(east.x>6.99&&std::abs(east.y)<1e-9);
    AnySessionPlayersV1 s{};s.sampled_tick=1000;assert(atlas::fresh(s,1500));assert(!atlas::fresh(s,1501));
    assert(!atlas::fresh(s,999));s.count=65;assert(!atlas::fresh(s,1001));
    s.count=1;s.players[0].valid_fields=PLAYER_NAME;memset(s.players[0].steam_name,'x',256);assert(!atlas::fresh(s,1001));
    s.players[0]={};s.players[0].valid_fields=PLAYER_FACING;s.players[0].yaw_radians=std::numeric_limits<double>::infinity();assert(!atlas::fresh(s,1001));
    s.players[0]={};s.players[0].valid_fields=PLAYER_POSITION;s.players[0].position[2]=std::numeric_limits<double>::quiet_NaN();assert(!atlas::fresh(s,1001));
    s.players[0]={};s.players[0].valid_fields=32;assert(!atlas::fresh(s,1001));
}
