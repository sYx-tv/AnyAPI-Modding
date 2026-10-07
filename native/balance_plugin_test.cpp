#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "anyapi_gpu_draw_v1.h"
#include "anyapi_creation_balance_v1.h"
#include "anyhelpers_settings_v1.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <cmath>
static void(*renderer)(const AnyFrameV1*,void*);static unsigned draws,lines;static bool available=true;static AnyCreationBalanceSnapshotV1 snapshot;
static std::vector<std::wstring> texts;static std::map<std::string,uint64_t> tokens;static std::map<uint64_t,double> values;static uint64_t revision;
static bool register_renderer(void(*fn)(const AnyFrameV1*,void*),void*){renderer=fn;return true;}
static bool emit(const AnyGpuCommandV1* c){++draws;for(float v:c->rect)assert(std::isfinite(v));if(c->kind==ANY_GPU_LINE)++lines;if(c->text)texts.emplace_back(c->text,c->text_length);return true;}
static bool copy(AnyCreationBalanceSnapshotV1* out){*out=snapshot;return available;}
static uint64_t add_setting(const AnyModSettingV1* d){auto t=tokens.size()+1;tokens[d->setting_id]=t;values[t]=d->default_number;return t;}
static bool get(uint64_t t,AnySettingValueV1* out){out->number=values[t];return true;}static uint64_t rev(){return revision;}
static const AnyCreationBalanceV1 api{sizeof(AnyCreationBalanceV1),1,copy};static const AnyGpuDrawV1 gpu{sizeof(AnyGpuDrawV1),1,register_renderer,nullptr,emit,nullptr};
static const AnyHelpersSettingsV1 settings{sizeof(AnyHelpersSettingsV1),1,add_setting,get,rev};
int wmain(int argc,wchar_t** argv){assert(argc==3);auto fixture=LoadLibraryW(argv[1]);assert(fixture);auto s=AnyAPI_Services();assert(s);assert(s->publish("anyapi.creation_balance",1,&api));assert(s->publish("anyapi.gpu_draw",1,&gpu));assert(s->publish("anyhelpers.settings",1,&settings));
 auto ui=(void(*)(int32_t))GetProcAddress(fixture,"FixtureUiState");auto mod=LoadLibraryW(argv[2]);assert(mod);AnyModHostV1 host;AnyModCallbacksV1 cb;assert(((AnyModInitV1)GetProcAddress(mod,"AnyAPI_ModInit"))(&host,&cb));((void(*)())GetProcAddress(mod,"AnyAPI_ModReady"))();assert(renderer&&tokens.size()==7);
 AnyFrameV1 frame;frame.width=1920;frame.height=1080;frame.focused=1;ui(1);snapshot.centre={0,0,.1};snapshot.base={0,-.2,.1};snapshot.axes[0]={.1,0,.1};snapshot.axes[1]={0,.1,.1};snapshot.axes[2]={-.1,.1,.1};snapshot.height_m=1.25;
 auto render=[&](){draws=lines=0;texts.clear();renderer(&frame,nullptr);};render();assert(draws>8&&lines>=4);bool labelled=false;for(auto& t:texts)if(t==L"CENTRE OF MASS")labelled=true;assert(labelled);
 available=false;render();assert(!draws);available=true;ui(2);render();assert(!draws);ui(1);frame.focused=0;render();assert(!draws);frame.focused=1;
 values[tokens["enabled"]]=0;++revision;render();assert(!draws);values[tokens["enabled"]]=1;values[tokens["summary"]]=0;values[tokens["axes"]]=0;values[tokens["height_guide"]]=0;++revision;snapshot.centre.depth=-1;render();assert(!draws);
 values[tokens["bounds"]]=1;++revision;for(auto& p:snapshot.corners)p={0,0,.1};render();assert(lines==12);
 values[tokens["bounds"]]=0;values[tokens["summary"]]=1;++revision;snapshot.valid_fields=ANY_BALANCE_BODY_MASS;snapshot.body_mass_kg=250;render();bool mass=false;for(auto& t:texts)if(t.find(L"250.0 kg")!=std::wstring::npos)mass=true;assert(mass);
 frame.width=320;frame.height=240;render();assert(draws);
}
