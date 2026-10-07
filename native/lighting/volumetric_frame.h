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
 std::array<std::array<float,16>,4> local_shadows{};
 // Four float4s: position/radius, direction/cos(cone), RGB/spot flag,
 // shadow slot/depth mode/intensity/reserved. All positions graphics-relative.
 std::array<std::array<float,16>,8> lights{};
 float local_shadow_count{};
};
static_assert(offsetof(Frame,additional_shadows)==56*4);
inline bool valid(const Frame& f){
 auto data=reinterpret_cast<const float*>(&f);for(size_t i=0;i<sizeof(Frame)/sizeof(float);++i)if(!std::isfinite(data[i])||std::abs(data[i])>1e8f)return false;
 auto unit=[](const std::array<float,4>& v){float n=v[0]*v[0]+v[1]*v[1]+v[2]*v[2];return n>.99f&&n<1.01f;};
 if(f.local_shadow_count!=std::floor(f.local_shadow_count)||f.fog_color[3]!=std::floor(f.fog_color[3]))return false;
 for(unsigned i=0;i<unsigned(std::clamp(f.fog_color[3],0.f,8.f));++i){auto& l=f.lights[i];if(l[3]<=.01f||l[3]>2000||l[8]<0||l[9]<0||l[10]<0)return false;if(l[11]!=0&&l[11]!=1)return false;if(l[11]){float n=l[4]*l[4]+l[5]*l[5]+l[6]*l[6];if(n<.99f||n>1.01f||l[7]<0||l[7]>=1)return false;}if(l[12]!=-1&&(l[12]<8||l[12]>=8+f.local_shadow_count||l[12]!=std::floor(l[12])||(l[13]!=1&&l[13]!=2)))return false;}
 return f.local_shadow_count>=0&&f.local_shadow_count<=4&&f.fog_color[3]>=0&&f.fog_color[3]<=8&&f.sun_color[3]>=0&&f.sun_color[3]<=15&&f.forward[3]>=1&&f.forward[3]<=8&&f.forward[3]==std::floor(f.forward[3])&&unit(f.right)&&unit(f.up)&&unit(f.forward)&&unit(f.sun)&&f.right[3]>0&&f.right[3]<10&&f.up[3]>0&&f.up[3]<10&&f.medium[0]>=0&&f.medium[0]<=.1f&&f.medium[2]>=0&&f.medium[2]<=1&&std::abs(f.medium[3])<=.8f&&f.controls[0]>=8&&f.controls[0]<=64&&f.controls[1]>0&&f.controls[1]<=2000&&f.controls[2]>=0&&f.controls[2]<=15&&f.controls[3]>=0&&f.controls[3]<=2&&std::abs(f.projection[0]*f.projection[3]-f.projection[1]*f.projection[2])>1e-8f;
}
}
