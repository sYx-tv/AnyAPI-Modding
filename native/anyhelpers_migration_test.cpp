#define NOMINMAX
#include <windows.h>
#include "anyapi_services_v1.h"
#include "mod_controls_v1.h"
#include "anyhelpers_settings_v1.h"
#include "anyapi_menu_v2.h"
#include <filesystem>
#include <fstream>
#include <cassert>
#include <iostream>
static void capture(uint32_t){}static void log(uint32_t,const char*,const char*){}
int wmain(int argc,wchar_t** argv){assert(argc==3);auto dir=std::filesystem::temp_directory_path()/(L"AnyHelpers.Migration."+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directories(dir/L"ModControls");std::filesystem::create_directories(dir/L"AnyHelpers");std::ofstream(dir/L"ModControls"/L"bindings.tsv")<<"anymap\ttoggle_map\t78\n";std::ofstream(dir/L"AnyHelpers"/L"settings.tsv")<<"anymap\twaypoint_label\t5\t52656c6f61646564\n";
 auto fixture=LoadLibraryW(argv[1]),dll=LoadLibraryW(argv[2]);assert(fixture&&dll);AnyModHostV1 host;auto path=dir.wstring();host.plugin_directory=path.c_str();host.log=log;host.capture_input=capture;AnyModCallbacksV1 c;assert(((AnyModInitV1)GetProcAddress(dll,"AnyAPI_ModInit"))(&host,&c)&&!strcmp(c.id,"anyhelpers"));auto services=AnyAPI_Services();auto controls=(const ModControlsV1*)services->query("anyhelpers.controls",1);assert(controls&&controls==services->query("modcontrols.bindings",1));ModControlActionV1 a;a.mod_id="anymap";a.mod_name="AnyMap";a.action_id="toggle_map";a.label="Toggle map";a.default_key='M';assert(controls->register_action(&a)==1&&controls->key(1)=='N');
 auto settings=(const AnyHelpersSettingsV1*)services->query("anyhelpers.settings",1);AnyModSettingV1 d;d.mod_id="anymap";d.mod_name="AnyMap";d.setting_id="waypoint_label";d.label="Waypoint label";d.kind=ANY_SETTING_TEXT;d.default_text="Waypoint";d.text_limit=48;assert(settings->register_setting(&d)==1);AnySettingValueV1 value;assert(settings->get(1,&value)&&!strcmp(value.text,"Reloaded"));
 auto event=(void(*)(uint32_t))GetProcAddress(fixture,"FixtureEvent");auto draw=(void(*)(uint32_t))GetProcAddress(fixture,"FixtureDraw");event(ANY_MENU_OPEN);draw(2);event(ANY_MENU_APPLY);assert(controls->key(1)==0&&std::filesystem::exists(dir/L"AnyHelpers"/L"bindings.tsv"));std::ifstream old(dir/L"ModControls"/L"bindings.tsv");std::string line;std::getline(old,line);assert(line=="anymap\ttoggle_map\t78");old.close();
 for(auto p:{dir/L"AnyHelpers"/L"bindings.tsv",dir/L"AnyHelpers"/L"settings.tsv",dir/L"ModControls"/L"bindings.tsv"})std::filesystem::remove(p);std::filesystem::remove(dir/L"AnyHelpers");std::filesystem::remove(dir/L"ModControls");std::filesystem::remove(dir);std::cout<<"PASS: actual DLL preserves legacy N, loads typed settings, publishes canonical/compatibility services and writes only canonical binding storage\n";}
