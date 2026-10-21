#include "inventory_layout.h"
#include <cassert>
#include <cstdio>
// Every common resolution keeps the panel on screen, the native inventory inside
// its opposite margin and, where there is room, the panel beside the inventory.
int main(){const int sizes[][2]={{1280,720},{1366,768},{1600,900},{1920,1080},{2560,1440},{3840,2160},{1280,800},{1440,900},{1680,1050},{1920,1200},{2560,1600},{2560,1080},{3440,1440},{5120,1440},{1024,768},{1280,1024},{1600,1200},{640,480}};
 for(auto& s:sizes)for(double option:{380.,480.,720.})for(bool left:{false,true}){auto r=inventory_layout::fit(s[0],s[1],option,left);
  double margin=std::max(0.,(1-inventory_layout::native_width*s[1]/double(s[0]))/2);
  assert(r.scale>=.5f&&r.scale<=2.f);assert(r.x>=0&&r.x+r.width<=s[0]&&r.width>0);assert(r.y>=0&&r.y+r.height<=s[1]);
  if(r.width/r.scale<378)std::printf("FAIL %dx%d %f %d %f\n",s[0],s[1],option,r.width,r.scale);assert(r.height/r.scale>=612);
  assert(r.shift>=0&&r.shift<=.25&&r.shift<=margin+inventory_layout::slack+1e-9);
  if(r.fits)assert((r.width+40*r.scale)/s[0]<=2*margin+inventory_layout::slack+1e-6);
  if(option==480&&!left)std::printf("%dx%d scale=%.3f width=%d shift=%.3f fits=%d\n",s[0],s[1],r.scale,r.width,r.shift,r.fits);}
 auto r=inventory_layout::fit(1920,1080,480,false);assert(r.scale==1.f&&r.width==480&&r.x==1416&&r.y==56&&r.height==980);
 std::puts("inventory_layout ok");}
