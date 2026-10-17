#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <initializer_list>
#include <utility>
// Camera maths for the second scene view. The game writes its camera constants
// (704 bytes) once per frame; the view copies them and replaces the four camera
// matrices and the camera position, so the game's own gbuffer pass draws from
// another point of view. Matrices are handled in one canonical form, M[row][col]
// with column vectors (clip = VP * position); the float layout the game uses in
// its constants is detected from the data, never assumed.
namespace scene_view_math {
struct M4 {double m[4][4]{};};
// Float offsets inside the camera constants.
enum : size_t {VIEW_PROJECTION=0,INVERSE_VIEW_PROJECTION=16,VIEW=32,PROJECTION=48,POSITION=148,CAMERA_FLOATS=176};
inline M4 load(const float* f,int layout){M4 r;for(int i=0;i<4;++i)for(int j=0;j<4;++j)r.m[i][j]=layout?f[i*4+j]:f[j*4+i];return r;}
inline void store(const M4& a,float* f,int layout){for(int i=0;i<4;++i)for(int j=0;j<4;++j)(layout?f[i*4+j]:f[j*4+i])=float(a.m[i][j]);}
inline M4 mul(const M4& a,const M4& b){M4 r;for(int i=0;i<4;++i)for(int j=0;j<4;++j){double s=0;for(int k=0;k<4;++k)s+=a.m[i][k]*b.m[k][j];r.m[i][j]=s;}return r;}
// Gauss-Jordan with partial pivoting; false when singular.
inline bool inverse(const M4& in,M4& out){double a[4][8];for(int i=0;i<4;++i)for(int j=0;j<8;++j)a[i][j]=j<4?in.m[i][j]:(j-4==i?1:0);
 for(int c=0;c<4;++c){int p=c;for(int r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[p][c]))p=r;if(std::abs(a[p][c])<1e-300)return false;if(p!=c)for(int j=0;j<8;++j)std::swap(a[p][j],a[c][j]);
  double d=a[c][c];for(int j=0;j<8;++j)a[c][j]/=d;for(int r=0;r<4;++r)if(r!=c){double f=a[r][c];if(f!=0)for(int j=0;j<8;++j)a[r][j]-=f*a[c][j];}}
 for(int i=0;i<4;++i)for(int j=0;j<4;++j)out.m[i][j]=a[i][j+4];return true;}
inline double dot3(const double* a,const double* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline void cross3(const double* a,const double* b,double* r){r[0]=a[1]*b[2]-a[2]*b[1];r[1]=a[2]*b[0]-a[0]*b[2];r[2]=a[0]*b[1]-a[1]*b[0];}
inline bool normalize3(double* v){double l=std::sqrt(dot3(v,v));if(!(l>1e-9)||!std::isfinite(l))return false;for(int i=0;i<3;++i)v[i]/=l;return true;}
// Clip w of a point: the view depth for a perspective projection.
inline double clip_w(const M4& vp,const double* p){return vp.m[3][0]*p[0]+vp.m[3][1]*p[1]+vp.m[3][2]*p[2]+vp.m[3][3];}
struct Pose {double position[3]{},right[3]{},up[3]{},forward[3]{};};
// Which float layout makes the constants a perspective camera at `position`
// looking along `forward`: the camera itself has clip w 0 and a point 10 m
// ahead has |w| 10. Returns 0 (stored by column), 1 (by row) or -1.
inline int detect_layout(const float* camera,const double* position,const double* forward){int found=-1;
 for(int layout=0;layout<2;++layout){auto vp=load(camera+VIEW_PROJECTION,layout);double ahead[3];for(int i=0;i<3;++i)ahead[i]=position[i]+forward[i]*10;
  double w0=clip_w(vp,position),w1=std::abs(clip_w(vp,ahead));if(std::isfinite(w0)&&std::isfinite(w1)&&std::abs(w0)<.05&&w1>9.5&&w1<10.5){if(found>=0)return -1;found=layout;}}
 return found;}
inline bool close(const M4& a,const M4& b,double tolerance){double scale=0;for(auto& r:b.m)for(double v:r)scale=std::max(scale,std::abs(v));for(int i=0;i<4;++i)for(int j=0;j<4;++j)if(!(std::abs(a.m[i][j]-b.m[i][j])<=tolerance*std::max(1.,scale)))return false;return true;}
enum Failure : int32_t {OK=0,LAYOUT=1,VIEW_MISMATCH=2,PRODUCT_MISMATCH=3,BASIS=4,SINGULAR=5,NOT_FINITE=6};
// Rewrites `camera` (a copy of the game's constants) for `view`. `main` is the
// game camera's pose in the same space as the constants. fov_scale multiplies
// the projection's x and y scale (2 halves the field of view). On success the
// result's inverse view-projection is returned canonically in `inverse_out`.
inline Failure rebuild(float* camera,const Pose& main,const Pose& view,double fov_scale,M4* inverse_out=nullptr,int* layout_out=nullptr){
 int layout=detect_layout(camera,main.position,main.forward);if(layout<0)return LAYOUT;if(layout_out)*layout_out=layout;
 auto v=load(camera+VIEW,layout),p=load(camera+PROJECTION,layout),vp=load(camera+VIEW_PROJECTION,layout);
 // The view's rotation rows, expressed in the main camera's axes, carry the
 // game's handedness and axis signs over to the new basis unchanged.
 double rows[3][3]{};for(int i=0;i<3;++i){double row[3]={v.m[i][0],v.m[i][1],v.m[i][2]};double a=dot3(row,main.right),b=dot3(row,main.up),g=dot3(row,main.forward);
  double t=-dot3(row,main.position);if(!(std::abs(t-v.m[i][3])<.05+1e-4*std::abs(t)))return VIEW_MISMATCH;
  for(int k=0;k<3;++k)rows[i][k]=a*view.right[k]+b*view.up[k]+g*view.forward[k];}
 if(!close(mul(p,v),vp,1e-3))return PRODUCT_MISMATCH;
 M4 nv=v;for(int i=0;i<3;++i){for(int k=0;k<3;++k)nv.m[i][k]=rows[i][k];nv.m[i][3]=-dot3(rows[i],view.position);}
 M4 np=p;if(!(fov_scale>.05&&fov_scale<50))return BASIS;for(int j=0;j<4;++j){np.m[0][j]*=fov_scale;np.m[1][j]*=fov_scale;}
 M4 nvp=mul(np,nv),ninv;if(!inverse(nvp,ninv))return SINGULAR;
 for(auto* m:{&nv,&np,&nvp,&ninv})for(auto& r:m->m)for(double x:r)if(!std::isfinite(x)||std::abs(x)>1e12)return NOT_FINITE;
 store(nvp,camera+VIEW_PROJECTION,layout);store(ninv,camera+INVERSE_VIEW_PROJECTION,layout);store(nv,camera+VIEW,layout);store(np,camera+PROJECTION,layout);
 for(int i=0;i<3;++i)camera[POSITION+i]=float(view.position[i]);if(inverse_out)*inverse_out=ninv;return OK;}
// The projection scale that gives a vertical field of view (radians); 1 when
// fov is 0 or the constants' projection is unusable.
inline double fov_scale_for(const float* camera,int layout,double fov){if(!(fov>0))return 1;double p11=std::abs(load(camera+PROJECTION,layout).m[1][1]);if(!(p11>1e-6)||!std::isfinite(p11))return 1;return 1/(std::tan(fov*.5)*p11);}
// A pose from yaw (0 looks along +Z, pi/2 along +X) and pitch (up positive),
// with right and up following the game camera's handedness (`main`).
inline bool orient(const Pose& main,double yaw,double pitch,Pose& out){double f[3]={std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)},up[3]={0,1,0},c[3];
 cross3(main.up,main.forward,c);double hand=dot3(c,main.right)<0?-1:1;double r[3];cross3(up,f,r);if(!normalize3(r))return false;for(auto& x:r)x*=hand;double u[3];cross3(f,r,u);for(auto& x:u)x*=hand;
 if(!normalize3(u))return false;for(int i=0;i<3;++i){out.forward[i]=f[i];out.right[i]=r[i];out.up[i]=u[i];}return true;}
// A pose relative to the game camera: offset metres along its right, up and
// forward axes, then turned by yaw and pitch (radians) about its own axes.
inline bool relative(const Pose& main,const double* offset,double yaw,double pitch,Pose& out){double f[3],c[3];cross3(main.up,main.forward,c);double hand=dot3(c,main.right)<0?-1:1;
 for(int i=0;i<3;++i){out.position[i]=main.position[i]+main.right[i]*offset[0]+main.up[i]*offset[1]+main.forward[i]*offset[2];f[i]=main.forward[i]*std::cos(yaw)*std::cos(pitch)+main.right[i]*std::sin(yaw)*std::cos(pitch)+main.up[i]*std::sin(pitch);}
 if(!normalize3(f))return false;double r[3];cross3(main.up,f,r);if(!normalize3(r))return false;for(auto& x:r)x*=hand;double u[3];cross3(f,r,u);for(auto& x:u)x*=hand;if(!normalize3(u))return false;
 for(int i=0;i<3;++i){out.forward[i]=f[i];out.right[i]=r[i];out.up[i]=u[i];}return true;}
}
