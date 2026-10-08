#define NOMINMAX
#include <windows.h>
#include "map_minimap_transform.h"
#include <cassert>
#include <iostream>
using namespace Gdiplus;
static void check(Bitmap& b,int x,int y,ARGB expected){Color c;assert(b.GetPixel(x,y,&c)==Ok);assert(c.GetValue()==expected);}
int main(){GdiplusStartupInput in;ULONG_PTR token;assert(GdiplusStartup(&token,&in,nullptr)==Ok);{
 // An asymmetric terrain tile: green land at -X, blue water at +X,
 // yellow road ahead at +Z. Test actual image sampling AND vector overlays.
 Bitmap tile(120,120,PixelFormat32bppARGB);{Graphics t(&tile);t.Clear(Color::Black);SolidBrush land(Color::Green),water(Color::Blue),road(Color::Yellow);t.FillRectangle(&land,10,55,10,10);t.FillRectangle(&water,100,55,10,10);t.FillRectangle(&road,55,100,10,10);}
 for(int quarter=0;quarter<2;++quarter){Bitmap rendered(160,160,PixelFormat32bppARGB);{Graphics g(&rendered);g.Clear(Color::Black);g.SetInterpolationMode(InterpolationModeNearestNeighbor);auto m=atlas::minimap_matrix(quarter*std::acos(-1.)*.5,false,80,80);assert(g.SetTransform(&m)==Ok);assert(g.DrawImage(&tile,Rect(-60,-60,120,120),0,0,120,120,UnitPixel)==Ok);Pen line(Color::Red,3);g.DrawLine(&line,0,0,30,0);}
 if(!quarter){check(rendered,35,80,Color::Green);check(rendered,125,80,Color::Blue);check(rendered,80,35,Color::Yellow);check(rendered,100,80,Color::Red);}
 else{check(rendered,80,125,Color::Green);check(rendered,80,35,Color::Blue);check(rendered,35,80,Color::Yellow);check(rendered,80,60,Color::Red);}
 }
 // Fixed north-up matches the same world handedness as heading-up.
 auto north=atlas::minimap_matrix(1,true,80,80);PointF point(10,20);assert(north.TransformPoints(&point,1)==Ok);assert(point.X==90&&point.Y==60);
 Bitmap full(160,160,PixelFormat32bppARGB);{Graphics g(&full);g.Clear(Color::Black);g.SetInterpolationMode(InterpolationModeNearestNeighbor);auto m=atlas::fullmap_matrix(20,120);g.SetTransform(&m);g.DrawImage(&tile,Rect(20,20,120,120),0,0,120,120,UnitPixel);}
 check(full,35,80,Color::Green);check(full,125,80,Color::Blue);check(full,80,35,Color::Yellow);
 }GdiplusShutdown(token);std::cout<<"PASS: actual terrain pixels and route strokes match +X camera-right/+Z forward at zero and right-quarter-turn; north-up corrected\n";}
