#pragma once
#include <vector>
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <stdexcept>
namespace itempreview {
struct Vertex {float x,y,z;std::array<uint8_t,4> color;};
struct Mesh {std::vector<Vertex> vertices;std::vector<uint32_t> indices;};
inline Mesh parse(const std::vector<uint8_t>& bytes) {
 auto need=[&](size_t at,size_t n){if(at>bytes.size()||n>bytes.size()-at)throw std::runtime_error("Truncated item mesh");};
 auto word=[&](size_t at){need(at,4);uint32_t value;memcpy(&value,bytes.data()+at,4);return value;};
 auto number=[&](size_t at){need(at,4);float value;memcpy(&value,bytes.data()+at,4);return value;};
 need(0,12);if(memcmp(bytes.data(),"mesh\x05\0\0\0",8))throw std::runtime_error("Unknown item mesh version");
 uint32_t parts=word(8);if(!parts||parts>256)throw std::runtime_error("Item mesh part count");size_t at=12;Mesh result;
 for(uint32_t part=0;part<parts;++part){auto length=word(at);if(!length||length>1024)throw std::runtime_error("Item mesh name");need(at+4,length+140);at+=4+length;
  // v5 part header: layout/material fields, bounded texture name, bounds, then vertices.
  // Try the established normal-bearing formats before position/colour-only fallback.
  auto texture_length=word(at+68);if(texture_length>1024)throw std::runtime_error("Item mesh texture name");need(at,136+texture_length);at+=136+texture_length;auto size=word(at);at+=4;need(at,size+4);size_t vertex_at=at;at+=size;auto index_size=word(at);at+=4;need(at,index_size);if(!size||size>32*1024*1024||index_size%12)throw std::runtime_error("Item mesh blocks");
  uint32_t maximum=0;for(size_t i=0;i<index_size;i+=4)maximum=std::max(maximum,word(at+i));uint32_t stride=0;
  for(uint32_t candidate:{36u,28u,16u}){if(size%candidate||maximum>=size/candidate||(candidate==16&&stride))continue;bool good=true;
   for(size_t off=0;off<size&&good;off+=candidate){float norm=0;for(int axis=0;axis<3;++axis){float p=number(vertex_at+off+axis*4),n=candidate==16?0.f:number(vertex_at+off+candidate-12+axis*4);if(!std::isfinite(p)||!std::isfinite(n)||std::abs(p)>1000||std::abs(n)>1.1f){good=false;break;}norm+=n*n;}if(norm>1.1f)good=false;}
   if(good){if(stride)throw std::runtime_error("Ambiguous item vertex format");stride=candidate;}}
  if(!stride||result.vertices.size()+size/stride>1000000||result.indices.size()+index_size/4>3000000)throw std::runtime_error("Unsupported item vertex format");auto base=uint32_t(result.vertices.size());
  for(size_t off=0;off<size;off+=stride){Vertex v{number(vertex_at+off),number(vertex_at+off+4),number(vertex_at+off+8)};memcpy(v.color.data(),bytes.data()+vertex_at+off+12,4);result.vertices.push_back(v);}
  for(size_t i=0;i<index_size;i+=4)result.indices.push_back(base+word(at+i));at+=index_size;
 }
 if(bytes.size()-at!=8||result.indices.empty())throw std::runtime_error("Item mesh trailing schema");return result;
}
// Orthographic previews use the real authored geometry and vertex colours. They
// do not construct game items or invoke game graphics on a foreign thread.
inline std::vector<uint8_t> render(const Mesh& mesh) {
 constexpr int side=128;struct Projected {float x,y,z;std::array<uint8_t,4> color;};std::vector<Projected> vertices;float minx=1e9f,miny=1e9f,maxx=-1e9f,maxy=-1e9f;
 for(auto& v:mesh.vertices){Projected p{.866f*v.x-.5f*v.z,.36f*v.x+.8f*v.y+.624f*v.z,-.4f*v.x+.6f*v.y-.693f*v.z,v.color};minx=std::min(minx,p.x);maxx=std::max(maxx,p.x);miny=std::min(miny,p.y);maxy=std::max(maxy,p.y);vertices.push_back(p);}
 float scale=112.f/std::max({maxx-minx,maxy-miny,.00001f});for(auto& v:vertices){v.x=64+(v.x-(maxx+minx)*.5f)*scale;v.y=64-(v.y-(maxy+miny)*.5f)*scale;}
 std::vector<uint8_t> pixels(side*side*4);std::vector<float> depth(side*side,-1e30f);auto edge=[](const Projected& a,const Projected& b,float x,float y){return (x-a.x)*(b.y-a.y)-(y-a.y)*(b.x-a.x);};
 for(size_t i=0;i<mesh.indices.size();i+=3){auto a=vertices[mesh.indices[i]],b=vertices[mesh.indices[i+1]],c=vertices[mesh.indices[i+2]];float area=edge(a,b,c.x,c.y);if(std::abs(area)<.001f)continue;
  int x0=std::clamp(int(std::floor(std::min({a.x,b.x,c.x}))),0,127),x1=std::clamp(int(std::ceil(std::max({a.x,b.x,c.x}))),0,127),y0=std::clamp(int(std::floor(std::min({a.y,b.y,c.y}))),0,127),y1=std::clamp(int(std::ceil(std::max({a.y,b.y,c.y}))),0,127);
  auto& va=mesh.vertices[mesh.indices[i]];auto& vb=mesh.vertices[mesh.indices[i+1]];auto& vc=mesh.vertices[mesh.indices[i+2]];float ux=vb.x-va.x,uy=vb.y-va.y,uz=vb.z-va.z,vx=vc.x-va.x,vy=vc.y-va.y,vz=vc.z-va.z,nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;float length=std::sqrt(nx*nx+ny*ny+nz*nz);float light=length>1e-8f?.45f+.55f*std::abs((nx*.35f+ny*.85f+nz*.4f)/length):1.f;
  for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){float wa=edge(b,c,x+.5f,y+.5f)/area,wb=edge(c,a,x+.5f,y+.5f)/area,wc=1-wa-wb;if(wa<-.0001f||wb<-.0001f||wc<-.0001f)continue;float z=wa*a.z+wb*b.z+wc*c.z;auto at=y*side+x;if(z<depth[at])continue;depth[at]=z;auto p=pixels.data()+at*4;for(int channel=0;channel<3;++channel){float color=(wa*a.color[channel]+wb*b.color[channel]+wc*c.color[channel])*light;p[2-channel]=uint8_t(std::clamp(color,0.f,255.f));}p[3]=255;}
 }return pixels;
}
}
