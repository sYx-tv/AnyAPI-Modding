#pragma once
#include <cmath>
#include <cstddef>
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
// K03: scene+0x668..0x66b are m_is_underground, m_is_underwater, m_is_nightvision
// and m_is_thermal (render_scene, 0.1.27 game code), not preview flags. Bit i of bypass_mask
// refuses the frame while byte i is set; a byte above 1 always refuses. 0xff is
// the 0.40.1 rule (any flag); the AA runtime passes scene_frame::VIEW_SPECIAL_VISION.
inline bool view_refused(const unsigned char* flags,uint32_t bypass_mask){for(uint32_t i=0;i<4;++i){if(flags[i]>1)return true;if(flags[i]&&(bypass_mask>>i&1))return true;}return false;}
// Returns false and writes nothing unless both matrices look like the main
// perspective camera: P is perspective and VP's clip-w row is the forward axis.
inline bool apply(unsigned char* scene,uint32_t width,uint32_t height,uint32_t index,State& s,uint32_t bypass_mask=0xff){
 if(width<16||height<16||width>16384||height>16384)return false;
 if(view_refused(scene+0x668,bypass_mask))return false;
 unsigned char* main=scene+0x20;double p[16],vp[16],forward[3];memcpy(p,main+0x1a0,sizeof(p));memcpy(vp,main+0x220,sizeof(vp));memcpy(forward,main+0x78,sizeof(forward));
 for(int i=0;i<16;++i)if(!std::isfinite(p[i])||!std::isfinite(vp[i]))return false;
 if(std::abs(p[0])<1e-8||std::abs(p[5])<1e-8||std::abs(p[11])<.5||std::abs(p[15])>.001)return false;
 double w[3]={vp[3],vp[7],vp[11]},wn=std::sqrt(w[0]*w[0]+w[1]*w[1]+w[2]*w[2]),fn=std::sqrt(forward[0]*forward[0]+forward[1]*forward[1]+forward[2]*forward[2]);
 if(wn<1e-6||fn<.98||fn>1.02||std::abs(w[0]*forward[0]+w[1]*forward[1]+w[2]*forward[2])/(wn*fn)<.999)return false;
 if(s.active&&!memcmp(p,s.written[0],sizeof(p))&&!memcmp(vp,s.written[1],sizeof(vp))){shift(p,-s.applied[0],-s.applied[1]);shift(vp,-s.applied[0],-s.applied[1]);}
 double x,y;offset(index,x,y);x*=2.0/width;y*=-2.0/height;shift(p,x,y);shift(vp,x,y);
 memcpy(main+0x1a0,p,sizeof(p));memcpy(main+0x220,vp,sizeof(vp));memcpy(s.written[0],p,sizeof(p));memcpy(s.written[1],vp,sizeof(vp));s.applied[0]=x;s.applied[1]=y;s.active=true;return true;
}
// K32: the jitter state of this thread's frame, for CPU readers of the scene
// matrices after _build_scene (the creation-balance HUD marker, drawn after AA).
// Set by scene_antialiasing::apply_jitter; null in builds and tests without AA.
inline thread_local const State* current{};
// Removes the jitter from a copy of the main view-projection only if it is
// exactly the matrix apply() wrote this frame (a rebuilt matrix is left alone).
inline bool unjitter_view_projection(double* vp){auto s=current;if(!s||!s->active||memcmp(vp,s->written[1],sizeof(s->written[1])))return false;shift(vp,-s->applied[0],-s->applied[1]);return true;}
// AA-1: the editor, placement and inspection overlays are drawn after the
// postprocess with this camera. scene_renderer.m_renderer_editor_overlay,
// _unclipped, _text, _text_background, _fluid_gradient, _fluid_gradient_volume
// and _grid (render_scene, +0x25b8 step 0x28) are vector<element>s, whose
// count is at +0xc (atlas core-runtime: buffer +0, offset +8, count +0xc).
constexpr uintptr_t overlay_first=0x25b8,overlay_stride=0x28,overlay_count_at=0xc;
constexpr uint32_t overlay_renderers=7,overlay_hold_frames=30;
constexpr size_t overlay_block=(overlay_renderers-1)*overlay_stride+overlay_count_at+4; // 0x100 bytes from scene+0x25b8
inline bool overlay_counts(const unsigned char* block,int32_t (&out)[overlay_renderers]){for(uint32_t i=0;i<overlay_renderers;++i){memcpy(&out[i],block+i*overlay_stride+overlay_count_at,4);if(out[i]<0||out[i]>(1<<24))return false;}return true;}
// Jitter stays off while any overlay was drawn in the last overlay_hold_frames
// frames: the vectors can be filled after _build_scene, so the decision for a
// frame uses what the previous frames drew.
struct OverlayGate {uint32_t hold{};uint64_t seen{};
 void observe(bool present){if(present){hold=overlay_hold_frames;++seen;}else if(hold)--hold;}
 bool blocks()const{return hold>0;}};
}
