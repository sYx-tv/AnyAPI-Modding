#include "dinput8_proxy.cpp"
#include <cassert>
static bool visible=true;static uint32_t binding='Q';static int layouts{},icons{},labels{},ends{},destroyed{},screen_height{};
static bool hint(AnyHudHintV1* out){if(!visible)return false;*out={};out->key=binding;strcpy_s(out->label,"Tool wheel (hold)");return true;}
static void screen(void*,const GeoString16*,const Vec2S32* size,const int32_t*,const int32_t*,const int32_t*,const int32_t*,const int32_t*,const bool*){screen_height=size->y;}
static void layout(void*,const GeoString16*,const Vec2S32* size,const int32_t*,const int32_t*,const int32_t* mode,const Vec2S32*,const int32_t*){assert(size->x==24&&size->y==2&&*mode==0);++layouts;}
static void key_icon(hud_hints::Icon* out,void*,const int32_t* key){assert(*key==int(binding));*out={};out->region=123;}
static void icon(void*,const GeoString16*,const hud_hints::Icon* d,const Vec2S32* size){assert(d->region==123&&size->x==2&&size->y==2);++icons;}
static void dtor(hud_hints::Icon*){++destroyed;}
static void text(void*,const GeoString16*,const GeoString16* label,const Vec2S32* size){assert(size->x==22&&size->y==2&&std::string((char*)label->data,label->length)=="Tool wheel (hold)");++labels;}
static void end(void*){++ends;}
int main(){using namespace hud_hints;platform::plugin_count=1;platform::current_plugin=0;AnyHudHintProviderV1 provider{sizeof(provider),1,hint};assert(services::publish("hud.hints.fixture",1,&provider));platform::current_plugin=-1;
 rows=collect();assert(rows.size()==1&&rows[0].key=='Q');binding='R';rows=collect();assert(rows.size()==1&&rows[0].key=='R');binding=0;assert(collect().empty());binding='R';visible=false;assert(collect().empty());visible=true;platform::plugins[0].faulted=true;assert(collect().empty());platform::plugins[0].faulted=false;rows=collect();
 original_screen=screen;hud_hints::layout=::layout;hud_hints::text=::text;hud_hints::key_icon=::key_icon;hud_hints::icon=::icon;icon_dtor=dtor;original_end=end;ready=true;row_height=2;inside=true;
 Vec2S32 size{24,12};GeoString16 id=make_const_geostring("control_hints");int32_t a=0;bool no=false;screen_hook(nullptr,&id,&size,&a,&a,&a,&a,&a,&no);assert(size.y==12&&screen_height==14);append(nullptr);assert(layouts==1&&icons==1&&labels==1&&ends==1&&destroyed==1);
 inside=false;screen_hook(nullptr,&id,&size,&a,&a,&a,&a,&a,&no);assert(screen_height==12);end_hook(nullptr);assert(ends==2&&layouts==1);
 assert(native_key('Q')=='Q'&&native_key(VK_ESCAPE)==256&&native_key(VK_F1)==290&&native_key(VK_RCONTROL)==345);
}
