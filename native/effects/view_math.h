#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
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
// The game camera's pose and float layout, read from the view matrix in the
// constants alone: in the right layout its bottom row is (0,0,0,1) and its
// rotation is orthonormal. Position is -R^T t; forward is the rotation row
// along which clip w grows. Returns the layout (0 stored by column, 1 by row)
// or -1.
inline int pose_from_constants(const float* camera,Pose& out){int found=-1;Pose pose;
 for(int layout=0;layout<2;++layout){auto v=load(camera+VIEW,layout);if(std::abs(v.m[3][0])>1e-4||std::abs(v.m[3][1])>1e-4||std::abs(v.m[3][2])>1e-4||std::abs(v.m[3][3]-1)>1e-4)continue;
  double r[3][3];for(int i=0;i<3;++i)for(int k=0;k<3;++k)r[i][k]=v.m[i][k];bool orthonormal=true;
  for(int i=0;i<3;++i)for(int j=0;j<3;++j)if(std::abs(dot3(r[i],r[j])-(i==j))>2e-3)orthonormal=false;if(!orthonormal)continue;
  Pose p;for(int k=0;k<3;++k){p.position[k]=-(r[0][k]*v.m[0][3]+r[1][k]*v.m[1][3]+r[2][k]*v.m[2][3]);p.right[k]=r[0][k];p.up[k]=r[1][k];p.forward[k]=r[2][k];}
  auto vp=load(camera+VIEW_PROJECTION,layout);double ahead[3];for(int k=0;k<3;++k)ahead[k]=p.position[k]+p.forward[k];
  if(clip_w(vp,ahead)-clip_w(vp,p.position)<0)for(auto& x:p.forward)x=-x;
  if(found>=0)return -1;found=layout;pose=p;}
 if(found>=0)out=pose;return found;}
// One line of what the constants look like, for the log when they are refused.
inline void describe(const float* camera,char* text,size_t size){int n=0;
 for(int layout=0;layout<2&&n>=0&&size_t(n)<size;++layout){auto v=load(camera+VIEW,layout),p=load(camera+PROJECTION,layout),vp=load(camera+VIEW_PROJECTION,layout),pv=mul(p,v);double worst=0,scale=0;
  for(int i=0;i<4;++i)for(int j=0;j<4;++j){worst=std::max(worst,std::abs(pv.m[i][j]-vp.m[i][j]));scale=std::max(scale,std::abs(vp.m[i][j]));}
  n+=std::snprintf(text+n,size-size_t(n),"L%d view_row3=%.3g,%.3g,%.3g,%.3g view_row0=%.3g,%.3g,%.3g,%.3g pv_error=%.3g vp_scale=%.3g ",layout,v.m[3][0],v.m[3][1],v.m[3][2],v.m[3][3],v.m[0][0],v.m[0][1],v.m[0][2],v.m[0][3],worst,scale);}
 if(n>=0&&size_t(n)<size)std::snprintf(text+n,size-size_t(n),"position=%.3g,%.3g,%.3g",camera[POSITION],camera[POSITION+1],camera[POSITION+2]);}
inline bool close(const M4& a,const M4& b,double tolerance){double scale=0;for(auto& r:b.m)for(double v:r)scale=std::max(scale,std::abs(v));for(int i=0;i<4;++i)for(int j=0;j<4;++j)if(!(std::abs(a.m[i][j]-b.m[i][j])<=tolerance*std::max(1.,scale)))return false;return true;}
enum Failure : int32_t {OK=0,LAYOUT=1,VIEW_MISMATCH=2,PRODUCT_MISMATCH=3,BASIS=4,SINGULAR=5,NOT_FINITE=6};
// Rewrites `camera` (a copy of the game's constants) for `view`. `main` and
// `layout` come from pose_from_constants. fov_scale multiplies
// the projection's x and y scale (2 halves the field of view). On success the
// result's inverse view-projection is returned canonically in `inverse_out`.
inline Failure rebuild(float* camera,int layout,const Pose& main,const Pose& view,double fov_scale,M4* inverse_out=nullptr){
 if(layout!=0&&layout!=1)return LAYOUT;
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
