#include "../balance_math.h"
#include <cassert>
#include <limits>
int main(){AnyCreationBalanceSnapshotV1 s;s.centre_local={1,2,-1};s.bounds_min={-2,0,-4};s.bounds_max={2,4,4};assert(balance::derive(s)&&s.height_m==2&&s.offset_x_m==1&&s.offset_z_m==-1);
 double m[12]={0,0,-1,0,1,0,1,0,0,10,20,30};auto world=balance::transform(m,s.centre_local);assert(world.x==9&&world.y==22&&world.z==29);
 float x,y;assert(balance::pixel({0,0,.5},1920,1080,x,y)&&x==960&&y==540);assert(balance::pixel({-1,1,1},1920,1080,x,y)&&x==0&&y==0);assert(!balance::pixel({0,0,-1},1920,1080,x,y));assert(!balance::pixel({0,0,0},1920,1080,x,y));assert(!balance::pixel({INFINITY,0,1},1920,1080,x,y));assert(!balance::pixel({0,0,1},0,1080,x,y));
 balance::Store store;AnyCreationBalanceSnapshotV1 out;s.sampled_tick=100;store.publish(s);assert(store.copy(&out,250));assert(!store.copy(&out,251));assert(!store.copy(&out,99));out.version=2;assert(!store.copy(&out,100));out.version=1;store.clear();assert(!store.copy(&out,100));
 s.bounds_max.x=-3;assert(!balance::derive(s));s.bounds_max.x=2;s.centre_local.x=std::numeric_limits<double>::quiet_NaN();assert(!balance::derive(s));
}
