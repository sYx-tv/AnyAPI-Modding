#define NOMINMAX
#include "anyhelpers_settings_state.h"
#include <cassert>
#include <iostream>
int main(){using namespace helpersettings;auto dir=std::filesystem::temp_directory_path()/(L"AnyHelpers.State."+std::to_wstring(GetCurrentProcessId()));State s;s.file=dir/L"settings.tsv";
 AnyModSettingV1 d;d.mod_id="fixture";d.mod_name="Fixture";d.setting_id="enabled";d.label="Enabled";d.default_number=1;
 assert(s.add(&d)==1&&!s.add(&d));assert(!s.stage(0,{2,{}}));assert(s.stage(0,{0,{}}));AnySettingValueV1 out;assert(s.get(1,&out)&&out.number==1);s.cancel();assert(!s.dirty());
 d.setting_id="count";d.kind=ANY_SETTING_INTEGER;d.minimum=10;d.maximum=100;d.step=10;d.default_number=50;assert(s.add(&d)==2);assert(!s.stage(1,{20.5,{}}));assert(s.stage(1,{24,{}})&&s.settings[1].draft.number==20);
 d.setting_id="range";d.kind=ANY_SETTING_NUMBER;d.minimum=.6;d.maximum=1.6;d.step=.1;d.default_number=1;assert(s.add(&d)==3);assert(!s.stage(2,{std::numeric_limits<double>::infinity(),{}}));assert(s.stage(2,{1.24,{}})&&std::abs(s.settings[2].draft.number-1.2)<1e-10);
 const char* choices[]={"First","Second"};d.setting_id="choice";d.kind=ANY_SETTING_CHOICE;d.choices=choices;d.choice_count=2;d.default_number=0;assert(s.add(&d)==4);assert(!s.stage(3,{2,{}})&&!s.stage(3,{.5,{}}));assert(s.stage(3,{1,{}}));
 d.setting_id="text";d.kind=ANY_SETTING_TEXT;d.default_text="Waypoint";d.text_limit=16;assert(s.add(&d)==5);assert(s.stage(4,{0,""}));assert(!s.stage(4,{0,std::string("\xc0\xaf")}));assert(!s.stage(4,{0,std::string("a\0b",3)}));assert(!s.stage(4,{0,std::string(17,'a')}));
 s.saved["dormant\tvalue"]={ANY_SETTING_TEXT,{0,"kept\t\n"}};assert(s.apply()&&s.revision==1&&!s.dirty());assert(s.get(5,&out)&&!out.text[0]);assert(s.apply()&&s.revision==1);
 State restore;restore.file=s.file;restore.load();assert(!restore.rejected&&restore.saved.size()==6);assert(restore.add(&d)==1&&restore.get(1,&out)&&!out.text[0]);assert(restore.saved["dormant\tvalue"].value.text=="kept\t\n");
 restore.reset();assert(restore.dirty()&&restore.get(1,&out)&&!out.text[0]);restore.cancel();assert(!restore.dirty());restore.stage(0,{0,"Pending"});auto revision=restore.revision;restore.file=dir/L"missing"/L"settings.tsv";
 std::ofstream(dir/L"missing")<<"blocked directory";assert(!restore.apply()&&restore.dirty()&&restore.revision==revision&&restore.get(1,&out)&&!out.text[0]);
 d.kind=ANY_SETTING_BOOL;d.setting_id="text";d.default_number=1;assert(s.settings.size()==5);State changed;changed.file=s.file;changed.load();assert(changed.add(&d)==1&&changed.rejected==1&&changed.get(1,&out)&&out.number==1);
 std::filesystem::remove(dir/L"missing");std::filesystem::remove(s.file);std::filesystem::remove(dir);
 std::cout<<"PASS: typed ranges, defaults, draft isolation, durable save/reload, empty UTF-8, invalid text, dormant mods, schema changes and failed-save retention\n";}
