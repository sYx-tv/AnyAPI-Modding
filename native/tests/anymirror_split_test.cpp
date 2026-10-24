#include "../anymirror_split.h"
#include <cassert>
using namespace anymirror_split;
static Plan run(std::vector<P> p){return split(p,[](int,int){return true;});}
static bool flat_pieces(const std::vector<P>& p,const Plan& plan){for(auto& piece:plan.pieces)if(!flat_convex(p,piece))return false;return true;}
int main(){
 // A flat, convex loop is left to the game.
 assert(run({{0,0,0},{4,0,0},{4,4,0},{0,4,0}}).pieces.empty());
 // A rectangle bent once is cut along the fold: two flat pieces, one cut between the fold corners.
 std::vector<P> bent{{0,0,0},{4,0,0},{4,2,0},{4,4,2},{0,4,2},{0,2,0}};
 Plan a=run(bent);assert(a.pieces.size()==2&&a.cuts.size()==1&&flat_pieces(bent,a));
 assert((a.cuts[0]==std::pair<int,int>{2,5}||a.cuts[0]==std::pair<int,int>{5,2}));
 // Bent twice (windscreen, slope, roof): one piece per face, cuts on the folds.
 std::vector<P> three{{0,0,0},{4,0,0},{4,2,0},{4,4,2},{4,6,2},{0,6,2},{0,4,2},{0,2,0}};
 Plan b=run(three);assert(b.pieces.size()==3&&b.cuts.size()==2&&flat_pieces(three,b));
 // A flat but concave loop is cut into convex pieces.
 std::vector<P> ell{{0,0,0},{4,0,0},{4,2,0},{2,2,0},{2,4,0},{0,4,0}};
 Plan c=run(ell);assert(c.pieces.size()==2&&flat_pieces(ell,c));
 // A twisted quad becomes two triangles.
 std::vector<P> twist{{0,0,0},{4,0,0},{4,4,1},{0,4,0}};
 Plan d=run(twist);assert(d.pieces.size()==2&&d.cuts.size()==1);
 // Pieces keep the loop's winding (each is a rotation of increasing loop indices).
 for(auto& piece:b.pieces){int drops=0;for(size_t i=0;i<piece.size();++i)drops+=piece[(i+1)%piece.size()]<piece[i];assert(drops==1);}
 // When no strut may be added, nothing is planned.
 assert(split(bent,[](int,int){return false;}).pieces.empty());
 // A long thin strip that bends is not cut into needles: no piece has a corner under 12 degrees, or nothing is planned.
 std::vector<P> strip{{0,0,0},{40,0,0},{80,0,4},{80,1,4},{40,1,0},{0,1,0}};
 Plan e=run(strip);for(auto& piece:e.pieces)assert(corners_ok(strip,piece));
 return 0;
}
