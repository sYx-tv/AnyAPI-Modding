#pragma once
#include "anyapi_gpu_draw_v1.h"
#include <unordered_map>
// Same map layout/math for CPU fallback previews and GPU game rendering.
// GPU terrain uploads once; subsequent DrawImage calls emit a transformed quad.
class MapGraphics {
 Gdiplus::Graphics* cpu{};const AnyGpuDrawV1* api{};Gdiplus::Matrix matrix;float opacity{1};int width{},height{};
 struct Saved {float matrix[6];int clips;};std::vector<Saved> saved;int clips{};
 static inline std::unordered_map<Gdiplus::Bitmap*,uint64_t> uploaded;
 static uint32_t color(Gdiplus::SolidBrush* b){Gdiplus::Color c;b->GetColor(&c);return c.GetValue();}
 static uint32_t color(Gdiplus::Pen* p){Gdiplus::Color c;p->GetColor(&c);return c.GetValue();}
 void emit(AnyGpuCommandV1 c){matrix.GetElements(c.matrix);c.opacity=opacity;api->emit(&c);}
 AnyGpuCommandV1 shape(uint32_t kind,Gdiplus::RectF r,uint32_t ink){AnyGpuCommandV1 c;c.kind=kind;c.color=ink;c.rect[0]=r.X;c.rect[1]=r.Y;c.rect[2]=r.Width;c.rect[3]=r.Height;return c;}
 void polygon(uint32_t ink,Gdiplus::PointF* p,int n,Gdiplus::Pen* pen=nullptr){auto c=shape(ANY_GPU_POLYGON,{},ink);c.points=(AnyGpuPointV1*)p;c.point_count=n;if(pen){c.flags=ANY_GPU_STROKE;c.stroke=pen->GetWidth();}emit(c);}
public:
 explicit MapGraphics(Gdiplus::Graphics& g):cpu(&g){}
 explicit MapGraphics(const AnyGpuDrawV1* a,int w,int h,float alpha=1):api(a),opacity(alpha),width(w),height(h){}
 bool gpu()const{return api!=nullptr;}
 static void reset_uploads(){uploaded.clear();}
 template<class T>void SetSmoothingMode(T v){if(cpu)cpu->SetSmoothingMode(v);}
 template<class T>void SetInterpolationMode(T v){if(cpu)cpu->SetInterpolationMode(v);}
 template<class T>void SetTextRenderingHint(T v){if(cpu)cpu->SetTextRenderingHint(v);}
 void Clear(Gdiplus::Color c){if(cpu)cpu->Clear(c);else if(c.GetA()){auto command=shape(ANY_GPU_RECT,{0,0,float(width),float(height)},c.GetValue());emit(command);}}
 Gdiplus::GraphicsState Save(){if(cpu)return cpu->Save();Saved s{};matrix.GetElements(s.matrix);s.clips=clips;saved.push_back(s);return Gdiplus::GraphicsState(saved.size());}
 void Restore(Gdiplus::GraphicsState state){if(cpu){cpu->Restore(state);return;}if(!state||state>saved.size())return;auto s=saved[state-1];while(clips>s.clips){AnyGpuCommandV1 c;c.kind=ANY_GPU_CLIP_POP;emit(c);--clips;}matrix.SetElements(s.matrix[0],s.matrix[1],s.matrix[2],s.matrix[3],s.matrix[4],s.matrix[5]);saved.resize(state-1);}
 void SetClip(Gdiplus::RectF r){if(cpu)cpu->SetClip(r);else{emit(shape(ANY_GPU_CLIP_PUSH,r,0));++clips;}}
 void ScaleTransform(float x,float y){if(cpu)cpu->ScaleTransform(x,y);else matrix.Scale(x,y);}
 void TranslateTransform(float x,float y){if(cpu)cpu->TranslateTransform(x,y);else matrix.Translate(x,y);}
 void MultiplyTransform(Gdiplus::Matrix* m){if(cpu)cpu->MultiplyTransform(m);else matrix.Multiply(m);}
 void FillRectangle(Gdiplus::SolidBrush* b,float x,float y,float w,float h){if(cpu)cpu->FillRectangle(b,x,y,w,h);else emit(shape(ANY_GPU_RECT,{x,y,w,h},color(b)));}
 void FillEllipse(Gdiplus::SolidBrush* b,float x,float y,float w,float h){if(cpu)cpu->FillEllipse(b,x,y,w,h);else emit(shape(ANY_GPU_ELLIPSE,{x,y,w,h},color(b)));}
 void DrawEllipse(Gdiplus::Pen* p,float x,float y,float w,float h){if(cpu)cpu->DrawEllipse(p,x,y,w,h);else{auto c=shape(ANY_GPU_ELLIPSE,{x,y,w,h},color(p));c.flags=ANY_GPU_STROKE;c.stroke=p->GetWidth();emit(c);}}
 void DrawRectangle(Gdiplus::Pen* p,Gdiplus::RectF r){if(cpu)cpu->DrawRectangle(p,r);else{auto c=shape(ANY_GPU_RECT,r,color(p));c.flags=ANY_GPU_STROKE;c.stroke=p->GetWidth();emit(c);}}
 void DrawRectangle(Gdiplus::Pen* p,float x,float y,float w,float h){DrawRectangle(p,{x,y,w,h});}
 void DrawLine(Gdiplus::Pen* p,float x,float y,float xx,float yy){if(cpu)cpu->DrawLine(p,x,y,xx,yy);else{auto c=shape(ANY_GPU_LINE,{x,y,xx,yy},color(p));c.stroke=p->GetWidth();if(p->GetDashStyle()==Gdiplus::DashStyleDash)c.flags=ANY_GPU_DASH;emit(c);}}
 void DrawLine(Gdiplus::Pen* p,Gdiplus::PointF a,Gdiplus::PointF b){DrawLine(p,a.X,a.Y,b.X,b.Y);}
 void FillPolygon(Gdiplus::SolidBrush* b,Gdiplus::PointF* points,int n){if(cpu)cpu->FillPolygon(b,points,n);else polygon(color(b),points,n);}
 void DrawPolygon(Gdiplus::Pen* p,Gdiplus::PointF* points,int n){if(cpu)cpu->DrawPolygon(p,points,n);else polygon(color(p),points,n,p);}
 void RoundRect(Gdiplus::RectF r,float radius,Gdiplus::Color ink){if(cpu){Gdiplus::GraphicsPath path;float d=radius*2;path.AddArc(r.X,r.Y,d,d,180,90);path.AddArc(r.GetRight()-d,r.Y,d,d,270,90);path.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);path.AddArc(r.X,r.GetBottom()-d,d,d,90,90);path.CloseFigure();Gdiplus::SolidBrush b(ink);cpu->FillPath(&b,&path);}else{auto c=shape(ANY_GPU_ROUND_RECT,r,ink.GetValue());c.radius=radius;emit(c);}}
 void DrawString(const wchar_t* text,int n,Gdiplus::Font* font,Gdiplus::RectF r,Gdiplus::StringFormat* f,Gdiplus::SolidBrush* b){if(cpu){cpu->DrawString(text,n,font,r,f,b);return;}auto c=shape(ANY_GPU_TEXT,r,color(b));c.text=text;c.text_length=n<0?uint32_t(wcslen(text)):uint32_t(n);c.font_size=font->GetSize();if(font->GetStyle()&Gdiplus::FontStyleBold)c.flags|=ANY_GPU_BOLD;if(f){if(f->GetAlignment()==Gdiplus::StringAlignmentCenter)c.flags|=ANY_GPU_CENTER;if(f->GetLineAlignment()==Gdiplus::StringAlignmentCenter)c.flags|=ANY_GPU_VCENTER;if(f->GetFormatFlags()&Gdiplus::StringFormatFlagsNoWrap)c.flags|=ANY_GPU_NOWRAP;}emit(c);}
 void DrawString(const wchar_t* text,int n,Gdiplus::Font* font,Gdiplus::PointF at,Gdiplus::SolidBrush* b){if(cpu)cpu->DrawString(text,n,font,at,b);else{Gdiplus::StringFormat f;f.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);DrawString(text,n,font,{at.X,at.Y,16384,font->GetSize()*2},&f,b);}}
 void DrawString(const wchar_t* text,int n,Gdiplus::Font* font,Gdiplus::PointF at,Gdiplus::StringFormat* f,Gdiplus::SolidBrush* b){if(cpu)cpu->DrawString(text,n,font,at,f,b);else DrawString(text,n,font,{at.X,at.Y,16384,font->GetSize()*2},f,b);}
 void MeasureString(const wchar_t* text,int n,Gdiplus::Font* font,Gdiplus::PointF at,Gdiplus::RectF* r){if(cpu)cpu->MeasureString(text,n,font,at,r);else{float w{},h{};api->measure(text,n<0?uint32_t(wcslen(text)):uint32_t(n),font->GetSize(),font->GetStyle()&Gdiplus::FontStyleBold?ANY_GPU_BOLD:0,&w,&h);*r={at.X,at.Y,w,h};}}
 void DrawImage(Gdiplus::Bitmap* image,Gdiplus::RectF dst,float x,float y,float w,float h,Gdiplus::Unit unit){if(cpu){cpu->DrawImage(image,dst,x,y,w,h,unit);return;}uint64_t id=uint64_t(image);if(!uploaded.count(image)){Gdiplus::Rect rect(0,0,image->GetWidth(),image->GetHeight());Gdiplus::BitmapData data{};if(image->LockBits(&rect,Gdiplus::ImageLockModeRead,PixelFormat32bppPARGB,&data)!=Gdiplus::Ok)return;bool ok=data.Stride>0&&api->texture(id,image->GetWidth(),image->GetHeight(),data.Stride,(uint8_t*)data.Scan0,1);image->UnlockBits(&data);if(!ok)return;uploaded[image]=id;}auto c=shape(ANY_GPU_IMAGE,dst,0xffffffff);c.texture=id;c.source[0]=x;c.source[1]=y;c.source[2]=w;c.source[3]=h;emit(c);}
 void DrawImage(Gdiplus::Bitmap* image,int x,int y){DrawImage(image,{float(x),float(y),float(image->GetWidth()),float(image->GetHeight())},0,0,float(image->GetWidth()),float(image->GetHeight()),Gdiplus::UnitPixel);}
 void DrawImage(Gdiplus::Bitmap* image,Gdiplus::RectF dst){DrawImage(image,dst,0,0,float(image->GetWidth()),float(image->GetHeight()),Gdiplus::UnitPixel);}
};
