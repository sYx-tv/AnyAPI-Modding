#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
namespace volumetric {
// The first 56 DWORDs are inline draw constants. Additional cascade matrices
// are rasterized into a tiny GPU texture using inline constants, avoiding mutable
// CPU upload buffers and preserving the 64-DWORD root-signature limit.
struct Frame {
 std::array<float,16> shadow{};
 std::array<float,4> camera{},right{1,0,0,1},up{0,1,0,1},forward{0,0,1,1};
 std::array<float,4> projection{1,-.1f,1,0},sun{0,1,0,0},sun_color{1,1,1,0};
 std::array<float,4> medium{.002f,0,.02f,.35f},controls{24,500,1,1},fog_color{.4f,.5f,.6f,0};
 std::array<std::array<float,16>,7> additional_shadows{};
};
static_assert(offsetof(Frame,additional_shadows)==56*4);
inline bool valid(const Frame& f){
 auto data=reinterpret_cast<const float*>(&f);for(size_t i=0;i<sizeof(Frame)/sizeof(float);++i)if(!std::isfinite(data[i])||std::abs(data[i])>1e8f)return false;
 auto unit=[](const std::array<float,4>& v){float n=v[0]*v[0]+v[1]*v[1]+v[2]*v[2];return n>.99f&&n<1.01f;};
 return f.forward[3]>=1&&f.forward[3]<=8&&f.forward[3]==std::floor(f.forward[3])&&unit(f.right)&&unit(f.up)&&unit(f.forward)&&unit(f.sun)&&f.right[3]>0&&f.right[3]<10&&f.up[3]>0&&f.up[3]<10&&f.medium[0]>=0&&f.medium[0]<=.1f&&f.medium[2]>=0&&f.medium[2]<=1&&std::abs(f.medium[3])<=.8f&&f.controls[0]>=8&&f.controls[0]<=64&&f.controls[1]>0&&f.controls[1]<=2000&&f.controls[2]>=0&&f.controls[2]<=5&&f.controls[3]>=0&&f.controls[3]<=2&&std::abs(f.projection[0]*f.projection[3]-f.projection[1]*f.projection[2])>1e-8f;
}
}
