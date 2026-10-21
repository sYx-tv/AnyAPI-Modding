#pragma once
#include <algorithm>
// AnyInventory sidebar placement for any backbuffer size and aspect ratio.
// The native inventory scales with screen height and is centred, so its free
// margin shrinks on narrower screens (16:10, 4:3) and on wider storage views.
// The sidebar may cover the margin on its own side and push the native roots
// into the opposite margin; it never pushes them further than that margin.
namespace inventory_layout {
// Native inventory width in screen heights. At 16:9 this leaves 0.12 of the
// width free on each side, which is what the original 1080p tuning assumed.
constexpr double native_width=1.35;
// Extra shift accepted beyond the free margin (the native roots carry padding).
constexpr double slack=.035;
constexpr double edge=24,gap=16,top=56,bottom=44,min_width=380,max_scale=2,min_scale=.5,min_fit_scale=.6;
struct Result {float scale;int x,y,width,height;double shift;bool fits;};
inline Result fit(int screen_w,int screen_h,double width_option,bool left){
 double W=screen_w,H=screen_h;Result r{};
 // Scale by the tighter of height and width against a 1920x1080 reference.
 double scale=std::clamp(std::min(H/1080,W/1920),min_scale,max_scale);
 double margin=std::max(0.,(1-native_width*H/W)/2),room=(2*margin+slack)*W;
 // Shrink the whole panel when the space beside the inventory is too small for
 // the narrowest layout. Below min_fit_scale text gets unreadable: overlap instead.
 double needed=(min_width+edge+gap)*scale;r.fits=room>=needed;
 if(!r.fits&&room/(min_width+edge+gap)>=min_fit_scale){scale=std::max(min_fit_scale,room/(min_width+edge+gap));r.fits=true;}
 double width=std::min(width_option*scale,r.fits?room-(edge+gap)*scale:width_option*scale);
 width=std::clamp(width,min_width*scale,std::max(min_width*scale,W-40));width=std::min(width,W-40);
 r.scale=float(scale);r.width=int(width);r.height=int(H-(top+bottom)*scale);r.y=int(top*scale);
 r.x=left?int(edge*scale):screen_w-r.width-int(edge*scale);
 double panel=(r.width+(edge+gap)*scale)/W;
 r.shift=std::clamp(panel-margin,0.,std::min(.25,margin+slack));
 return r;
}
}
