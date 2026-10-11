#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
// Subpixel camera jitter for TAA. Offsets the main camera's projection and
// view-projection (scene+0x20: +0x1a0 and +0x220, column-major doubles) in
// the transient scene data before _build_scene builds its constants.
namespace scene_jitter {
// Halton(2,3), 8 samples, centred on the pixel, in pixels.
inline void offset(uint32_t index,double& x,double& y){auto halton=[](uint32_t n,uint32_t base){double f=1,r=0;while(n){f/=base;r+=f*(n%base);n/=base;}return r;};x=halton(index%8+1,2)-.5;y=halton(index%8+1,3)-.5;}
struct State {double written[2][16]{};double applied[2]{};bool active{};};
// Adds jitter*clip.w to clip x/y. Row 3 (clip w) never changes, so a stale
// copy from last frame can be detected exactly and un-jittered first.
inline void shift(double* m,double x,double y){for(int c=0;c<4;++c){m[c*4]+=x*m[c*4+3];m[c*4+1]+=y*m[c*4+3];}}
// Returns false and writes nothing unless both matrices look like the main
// perspective camera: P is perspective and VP's clip-w row is the forward axis.
inline bool apply(unsigned char* scene,uint32_t width,uint32_t height,uint32_t index,State& s){
 if(width<16||height<16||width>16384||height>16384)return false;
 // Only the main world scene: preview and render-to-texture scenes set these flags.
 static const unsigned char normal[4]{};if(memcmp(scene+0x668,normal,4))return false;
 unsigned char* main=scene+0x20;double p[16],vp[16],forward[3];memcpy(p,main+0x1a0,sizeof(p));memcpy(vp,main+0x220,sizeof(vp));memcpy(forward,main+0x78,sizeof(forward));
 for(int i=0;i<16;++i)if(!std::isfinite(p[i])||!std::isfinite(vp[i]))return false;
 if(std::abs(p[0])<1e-8||std::abs(p[5])<1e-8||std::abs(p[11])<.5||std::abs(p[15])>.001)return false;
 double w[3]={vp[3],vp[7],vp[11]},wn=std::sqrt(w[0]*w[0]+w[1]*w[1]+w[2]*w[2]),fn=std::sqrt(forward[0]*forward[0]+forward[1]*forward[1]+forward[2]*forward[2]);
 if(wn<1e-6||fn<.98||fn>1.02||std::abs(w[0]*forward[0]+w[1]*forward[1]+w[2]*forward[2])/(wn*fn)<.999)return false;
 if(s.active&&!memcmp(p,s.written[0],sizeof(p))&&!memcmp(vp,s.written[1],sizeof(vp))){shift(p,-s.applied[0],-s.applied[1]);shift(vp,-s.applied[0],-s.applied[1]);}
 double x,y;offset(index,x,y);x*=2.0/width;y*=-2.0/height;shift(p,x,y);shift(vp,x,y);
 memcpy(main+0x1a0,p,sizeof(p));memcpy(main+0x220,vp,sizeof(vp));memcpy(s.written[0],p,sizeof(p));memcpy(s.written[1],vp,sizeof(vp));s.applied[0]=x;s.applied[1]=y;s.active=true;return true;
}
}
