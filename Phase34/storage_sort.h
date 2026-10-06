#pragma once
#include "anyapi_inventory_ui_v1.h"
#include <vector>
#include <string>
#include <algorithm>
#include <functional>
namespace storage_sort {
struct Placement {uint64_t item{},grid{};int x{},y{},rotation{};};
inline int width(const AnyStoredItemV1& a,int r){return r&1?a.height:a.width;}
inline int height(const AnyStoredItemV1& a,int r){return r&1?a.width:a.height;}
inline bool overlap(const Placement& p,const AnyStoredItemV1& a,const AnyStoredItemV1& b){return p.grid==b.grid&&p.x<b.x+width(b,b.rotation)&&b.x<p.x+width(a,p.rotation)&&p.y<b.y+height(b,b.rotation)&&b.y<p.y+height(a,p.rotation);}
inline std::string lower(const char* s){std::string a=s;for(auto& c:a)if(c>='A'&&c<='Z')c+=32;return a;}
inline bool same(const AnyStoredItemV1& a,const Placement& p){return a.grid==p.grid&&a.x==p.x&&a.y==p.y&&a.rotation==p.rotation;}
// Preflight the entire rearrangement. A spare cell breaks cycles without dropping items.
inline bool plan(std::vector<AnyStoredItemV1> items,const std::vector<AnyStorageGridV1>& grids,bool quantity,bool rotate,const std::function<bool(uint64_t,uint64_t,int)>& accepts,std::vector<Placement>& moves,std::string& error){
 moves.clear();std::vector<AnyStoredItemV1> sorted;for(auto& a:items)if(a.side==ANY_INVENTORY_STORAGE)sorted.push_back(a);
 if(sorted.size()>256){error="Too many items to sort";return false;}
 for(auto& a:items)if(a.width<=0||a.height<=0){error="An item size is unavailable";return false;}
 std::stable_sort(sorted.begin(),sorted.end(),[&](auto& a,auto& b){if(quantity&&a.quantity!=b.quantity)return a.quantity>b.quantity;auto x=lower(a.name),y=lower(b.name);return x==y?a.token<b.token:x<y;});
 std::vector<AnyStoredItemV1> packed;std::vector<Placement> targets;
 auto vacant=[&](const Placement& p,const AnyStoredItemV1& a,const std::vector<AnyStoredItemV1>& occupied){for(auto& b:occupied)if(a.token!=b.token&&overlap(p,a,b))return false;return true;};
 auto find_spot=[&](const AnyStoredItemV1& a,uint32_t side,const std::vector<AnyStoredItemV1>& occupied,Placement& result){for(auto& g:grids)if(g.side==side&&!g.fixed_slots)for(int r=0;r<(rotate&&g.allow_rotation?2:1);++r){if(!accepts(a.token,g.token,r))continue;int w=width(a,r),h=height(a,r);for(int y=g.height-h;y>=0;--y)for(int x=0;x+w<=g.width;++x){Placement p{a.token,g.token,x,y,r};if(vacant(p,a,occupied)){result=p;return true;}}}return false;};
 for(auto a:sorted){Placement p;if(!find_spot(a,ANY_INVENTORY_STORAGE,packed,p)){error="Items do not fit in sorted order";return false;}targets.push_back(p);a.grid=p.grid;a.x=p.x;a.y=p.y;a.rotation=p.rotation;packed.push_back(a);}
 auto place=[&](AnyStoredItemV1& a,const Placement& p){moves.push_back(p);a.grid=p.grid;a.x=p.x;a.y=p.y;a.rotation=p.rotation;};
 for(size_t step=0;step<sorted.size()*4+1;++step){bool complete=true,progress=false;for(auto& p:targets){auto it=std::find_if(items.begin(),items.end(),[&](auto& a){return a.token==p.item;});if(same(*it,p))continue;complete=false;if(vacant(p,*it,items)){place(*it,p);progress=true;}}
  if(complete)return true;if(progress)continue;
  bool buffered=false;for(auto& p:targets){auto it=std::find_if(items.begin(),items.end(),[&](auto& a){return a.token==p.item;});if(same(*it,p))continue;Placement spare;
   // Avoid final target cells for storage scratch; player space is the fallback.
   auto occupied=items;occupied.insert(occupied.end(),packed.begin(),packed.end());
   if(find_spot(*it,ANY_INVENTORY_STORAGE,occupied,spare)||find_spot(*it,ANY_INVENTORY_PLAYER,items,spare)){place(*it,spare);buffered=true;break;}
  }if(!buffered){moves.clear();error="Free some inventory space to sort";return false;}
 }
 moves.clear();error="Could not plan this rearrangement";return false;
}
}
