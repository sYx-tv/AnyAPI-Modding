#pragma once
#include "map_math.h"
#include <cmath>
// Zoom requests accumulate at the target; rendered frames ease toward it while
// preserving the world point beneath the cursor. Panning cancels pending motion.
namespace atlas {
struct SmoothZoom {
 double target=1;Point anchor{.5,.5},world_anchor{.5,.5};bool active{};uint64_t tick{};
 void cancel(const View& view){target=view.zoom;active=false;tick=0;}
 void request(const View& view,double factor,Point at){if(!std::isfinite(factor)||factor<=0)return;if(!active)target=view.zoom;target=std::clamp(target*factor,1.,8.);anchor=at;world_anchor=view.inverse(at);active=std::abs(target-view.zoom)>1e-7;}
 bool advance(View& view,uint64_t now){if(!active){tick=now;return false;}double dt=tick&&now>tick?std::min((now-tick)/1000.,.1):1./60;tick=now;double before=view.zoom;view.zoom=std::exp(std::log(view.zoom)+(std::log(target)-std::log(view.zoom))*(1-std::exp(-dt/.055)));if(std::abs(std::log(target/view.zoom))<.0005){view.zoom=target;active=false;}view.center={world_anchor.x-(anchor.x-.5)/view.zoom,world_anchor.y+(anchor.y-.5)/view.zoom};view.clamp();return before!=view.zoom;}
};
}
