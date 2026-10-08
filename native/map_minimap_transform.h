#pragma once
#include "map_math.h"
#include <gdiplus.h>
namespace atlas {
// Explicit GDI+ matrix avoids rotation/translation ordering ambiguity. The same
// camera basis positions terrain, road routes, pins and screen-space players.
inline Gdiplus::Matrix fullmap_matrix(float top,float side){
    return Gdiplus::Matrix(1,0,0,-1,0,2*top+side);
}
inline Gdiplus::Matrix minimap_matrix(double yaw,bool north_up,float x,float y){
    auto right=minimap_point({1,0},yaw,north_up);
    auto down=minimap_point({0,1},yaw,north_up);
    return Gdiplus::Matrix(float(right.x),float(right.y),float(down.x),float(down.y),x,y);
}
}
