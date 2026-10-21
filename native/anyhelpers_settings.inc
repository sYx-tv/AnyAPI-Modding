// Mod-owned registry and menu. The framework supplies only native widget primitives.
#include "anyhelpers_settings_v2.h"
#include "anyapi_bundle_v1.h"
#include <climits>
namespace settingsmod {
static helpersettings::State state;
static std::mutex mutex;
static const AnyMenuV3* widgets;
static uint64_t register_setting(const AnyModSettingV1* definition){std::lock_guard<std::mutex> lock(mutex);auto token=state.add(definition);if(token){auto& s=state.settings.back();log("SETTING_REGISTER mod="+s.mod+" setting="+s.id+" kind="+std::to_string(s.kind));}else log("SETTING_REGISTER rejected=1",2);return token;}
static bool get(uint64_t token,AnySettingValueV1* value){std::lock_guard<std::mutex> lock(mutex);return state.get(token,value);}
static uint64_t revision(){std::lock_guard<std::mutex> lock(mutex);return state.revision;}
static const AnyHelpersSettingsV1 api{sizeof(AnyHelpersSettingsV1),1,register_setting,get,revision};
struct Visibility {uint64_t first{},second{};double first_value{},second_value{};};
static std::map<uint64_t,Visibility> visibility_rules;
static bool visibility(uint64_t row,uint64_t first,double a,uint64_t second,double b){
 std::lock_guard<std::mutex> lock(mutex);
 if(!row||row>state.settings.size()||first>state.settings.size()||second>state.settings.size()||row==first||row==second||!std::isfinite(a)||!std::isfinite(b))return false;
 auto family=[](const std::string& id){return id.substr(0,id.find('.'));};
 for(auto condition:{first,second})if(condition&&family(state.settings[condition-1].mod)!=family(state.settings[row-1].mod))return false;
 visibility_rules[row]={first,second,a,b};return true;
}
static bool visible(uint64_t token){auto it=visibility_rules.find(token);if(it==visibility_rules.end())return true;auto& r=it->second;return (!r.first||state.settings[r.first-1].draft.number==r.first_value)&&(!r.second||state.settings[r.second-1].draft.number==r.second_value);}
static const AnyHelpersSettingsV2 api2{sizeof(AnyHelpersSettingsV2),2,register_setting,get,revision,visibility};
// Bundled mods (anyapi.bundle_parts): an "Enabled" row at the top of each switchable part's group. The saved row is the
// truth; Apply hands it to AnyAPI, which loads or skips that part on the next launch. Call sync_parts with the mutex held.
static const AnyBundlePartsV1* bundle_parts;static std::vector<std::pair<uint64_t,std::string>> part_rows;
static void sync_parts(){if(!bundle_parts)return;for(auto& [token,id]:part_rows){AnySettingValueV1 value;if(state.get(token,&value)&&!bundle_parts->set_enabled(id.c_str(),value.number!=0))log("BUNDLE_PART_SWITCH id="+id+" saved=0",2);}}
static void add_part_rows(const AnyServicesV1* services){auto parts=services?(const AnyBundlePartsV1*)services->query("anyapi.bundle_parts",1):nullptr;
 if(!parts||parts->struct_size!=sizeof(*parts)||!parts->count||!parts->copy||!parts->set_enabled)return;std::lock_guard<std::mutex> lock(mutex);bundle_parts=parts;
 for(uint32_t i=0,n=parts->count();i<n;++i){AnyBundlePartInfoV1 info;if(!parts->copy(i,&info)||(info.flags&ANY_BUNDLE_PART_REQUIRED)||info.state==ANY_PART_LEGACY)continue;
  auto description=std::string("Part of the ")+info.bundle+" bundle. Turning it on or off applies the next time you start the game.";
  AnyModSettingV1 row;row.mod_id=info.id;row.mod_name=info.name;row.setting_id="bundle.enabled";row.label="Enabled";row.description=description.c_str();row.order=INT32_MIN;row.default_number=1;
  if(auto token=state.add(&row))part_rows.push_back({token,info.id});else log(std::string("BUNDLE_PART_ROW rejected id=")+info.id,2);}
 sync_parts();log("BUNDLE_PARTS rows="+std::to_string(part_rows.size()));}
static uint32_t dirty(void*){std::lock_guard<std::mutex> lock(mutex);return state.dirty();}
static uint32_t defaults(void*){std::lock_guard<std::mutex> lock(mutex);return state.defaults();}
static void event(uint32_t type,void*){std::lock_guard<std::mutex> lock(mutex);
 if(type==ANY_MENU_OPEN)state.begin();else if(type==ANY_MENU_RESET){state.reset();log("SETTINGS_RESET staged=1");}
 else if(type==ANY_MENU_APPLY){bool changed=state.dirty(),ok=state.apply();if(changed)log(ok?"SETTINGS_APPLY saved=1":"SETTINGS_APPLY saved=0 pending_retained=1",ok?0:2);if(changed&&ok)sync_parts();}
 else if(type==ANY_MENU_CANCEL){bool pending=state.dirty();state.cancel();log("SETTINGS_CLOSED pending_discarded="+std::to_string(pending));}
}
static void draw(const AnyMenuFrameV1*,void*){std::lock_guard<std::mutex> lock(mutex);
 if(state.settings.empty()){menu->label("empty","No mods have registered settings yet.");return;}
 std::vector<size_t> sorted;for(size_t i=0;i<state.settings.size();++i)sorted.push_back(i);
 std::sort(sorted.begin(),sorted.end(),[](size_t a,size_t b){auto& x=state.settings[a];auto& y=state.settings[b];if(x.name!=y.name)return x.name<y.name;if(x.order!=y.order)return x.order<y.order;if(x.mod!=y.mod)return x.mod<y.mod;return x.id<y.id;});std::string previous;
 for(auto i:sorted){if(!visible(i+1))continue;auto& s=state.settings[i];auto id="setting."+std::to_string(i+1);if(s.name!=previous){auto title=upper(s.name);menu->heading(("group."+std::to_string(i+1)).c_str(),title.c_str());previous=s.name;}
  auto label=upper(s.label);auto value=s.draft;uint32_t changed{};
  if(s.kind==ANY_SETTING_BOOL){uint32_t boolean=uint32_t(value.number);changed=widgets->toggle_row(id.c_str(),label.c_str(),&boolean);value.number=boolean;}
  else if(s.kind==ANY_SETTING_INTEGER||s.kind==ANY_SETTING_NUMBER)changed=widgets->number_row(id.c_str(),label.c_str(),&value.number,s.minimum,s.maximum,s.step,s.kind==ANY_SETTING_INTEGER);
  else if(s.kind==ANY_SETTING_CHOICE){std::vector<const char*> choices;for(auto& choice:s.choices)choices.push_back(choice.c_str());uint32_t index=uint32_t(value.number);changed=widgets->choice_row(id.c_str(),label.c_str(),choices.data(),uint32_t(choices.size()),&index);value.number=index;}
  else {char text[257]{};memcpy(text,value.text.c_str(),value.text.size()+1);changed=widgets->text_row(id.c_str(),label.c_str(),text,s.text_limit+1);value.text=text;}
  if(changed){bool accepted=state.stage(i,std::move(value));log("SETTING_STAGE mod="+s.mod+" setting="+s.id+" accepted="+std::to_string(accepted));}
 }
 menu->label("help",state.status.empty()?"Changes take effect when you select Apply Changes.":state.status.c_str());
}
static bool init(const AnyServicesV1* services){widgets=(const AnyMenuV3*)services->query("anyapi.menu",3);if(!widgets||widgets->version!=3||widgets->struct_size!=sizeof(*widgets))return false;
 state.file=std::filesystem::path(host.plugin_directory)/L"AnyHelpers"/L"settings.tsv";state.load();
 AnyMenuTabV2 tab;tab.id="mod_settings";tab.title="MOD SETTINGS";tab.order=110;tab.icon=ANY_MENU_ICON_SETTINGS;tab.draw=draw;tab.event=event;tab.dirty=dirty;tab.defaults=defaults;
 return menu->add_tab(&tab)&&services->publish("anyhelpers.settings",1,&api)&&services->publish("anyhelpers.settings",2,&api2);
}
} // namespace settingsmod
