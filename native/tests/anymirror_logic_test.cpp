#include "../anymirror_logic.h"
#include <cassert>
#include <cmath>
using namespace anymirror;
int main(){
 // Mirroring flips one axis around the plane and is its own inverse, on and between grid lines.
 Plane x{0,0};Grid p{{3,-2,5}};Grid m=mirror(p,x);assert(m.v[0]==-3&&m.v[1]==-2&&m.v[2]==5&&mirror(m,x)==p);
 Plane half{2,7};Grid one{{1,1,1}};Grid q=mirror(one,half);assert(q.v[2]==6&&q.v[0]==1&&mirror(q,half)==one);
 // A twin is placed for an off-plane edge, reversed or not.
 Grid a{{1,0,0}},b{{4,2,0}},m0,m1;assert(mirrored_edge(a,b,x,m0,m1)&&m0==(Grid{{-1,0,0}})&&m1==(Grid{{-4,2,0}}));
 assert(same_edge(a,b,b,a)&&!same_edge(a,b,a,a));
 // Edges in the plane, or crossing it symmetrically, are their own mirror: nothing extra.
 assert((!mirrored_edge(Grid{{0,0,0}},Grid{{0,5,1}},x,m0,m1)));
 assert((!mirrored_edge(Grid{{-2,1,0}},Grid{{2,1,0}},x,m0,m1)));
 // A crossing edge that is not symmetric still gets a twin.
 assert((mirrored_edge(Grid{{-1,1,0}},Grid{{3,1,0}},x,m0,m1))&&m0==(Grid{{1,1,0}})&&m1==(Grid{{-3,1,0}}));
 // Out of range input or a bad axis never produces an edge.
 assert((!mirrored_edge(Grid{{kLimit,0,0}},b,x,m0,m1)));assert((!mirrored_edge(a,b,Plane{3,0},m0,m1)));
 // Centring uses the middle of the bounds; odd sums sit between grid lines.
 Grid lo{{-4,0,2}},hi{{3,6,8}};assert(centred(0,lo,hi).twice==-1&&centred(1,lo,hi).twice==6&&centred(2,lo,hi).axis==2);
 assert(centred(9,lo,hi).axis==0);
 // Nudging moves by half steps and clamps.
 assert((nudged(Plane{1,4},1).twice==5&&nudged(Plane{1,4},-2).twice==2&&nudged(Plane{0,kLimit-2},10).twice==kLimit-1));
 // The wall is a thin slab at the plane, spanning the bounds plus the margin on the other axes.
 Plane at{0,3};Box w=wall(at,lo,hi,0.25,2,0.02);
 assert(std::fabs(w.min[0]-(0.375-0.01))<1e-12&&std::fabs(w.max[0]-(0.375+0.01))<1e-12);
 assert(std::fabs(w.min[1]-(-0.5))<1e-12&&std::fabs(w.max[1]-2.0)<1e-12&&std::fabs(w.min[2]-0.0)<1e-12&&std::fabs(w.max[2]-2.5)<1e-12);
 return 0;
}
