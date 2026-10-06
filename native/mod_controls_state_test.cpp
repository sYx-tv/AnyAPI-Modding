#define NOMINMAX
#include "mod_controls_state.h"
#include <cassert>
#include <iostream>
int main(){using namespace modcontrols;auto directory=std::filesystem::temp_directory_path()/(L"AnyAPI.BindingsTest."+std::to_wstring(GetCurrentProcessId()));State s;s.file=directory/L"bindings.tsv";
 ModControlActionV1 a;a.mod_id="anymap";a.mod_name="AnyMap";a.action_id="toggle_map";a.label="Open / close map";a.default_key='M';assert(s.add(&a)==1);assert(!s.add(&a));auto invalid=a;invalid.mod_id="../bad";assert(!s.add(&invalid));invalid=a;invalid.default_key=VK_ESCAPE;assert(!s.add(&invalid));
 a.mod_id="testmod";a.mod_name="Test Mod";a.action_id="other";a.default_key='P';assert(s.add(&a)==2);
 s.begin();assert(s.stage(0,'K'));assert(s.dirty()&&!s.defaults());assert(s.actions[0].committed=='M');assert(!s.stage(0,'P'));assert(s.actions[0].draft=='K');s.cancel();assert(!s.dirty()&&s.actions[0].committed=='M');
 s.begin();assert(s.stage(0,'K'));assert(s.apply());assert(!s.dirty()&&s.actions[0].committed=='K');s.reset();assert(s.dirty()&&s.defaults());s.cancel();assert(s.actions[0].draft=='K');
 State loaded;loaded.file=s.file;assert(!loaded.load());a.mod_id="anymap";a.mod_name="AnyMap";a.action_id="toggle_map";a.default_key='M';assert(loaded.add(&a)==1&&loaded.actions[0].committed=='K');assert(loaded.saved.at("testmod\tother")=='P');loaded.begin();assert(loaded.stage(0,0));assert(loaded.apply());assert(loaded.saved.at("testmod\tother")=='P');
 loaded.reset();assert(loaded.apply());assert(loaded.actions[0].committed=='M');
 // Force persistence failure with a directory as the destination; committed memory must stay intact.
 auto destination=directory/L"blocked";std::filesystem::create_directory(destination);loaded.file=destination;assert(loaded.stage(0,'L'));assert(!loaded.apply());assert(loaded.actions[0].committed=='M'&&loaded.dirty());
 std::ofstream corrupt(s.file,std::ios::app);corrupt<<"bad\nanymap\ttoggle_map\t27\nanymap\ttoggle_map\t123junk\n";corrupt.close();State checked;checked.file=s.file;assert(checked.load()==3);assert(key_name('M')=="M"&&key_name(0)=="Unbound");
 // Remove only explicitly created fixture files; no recursive deletion.
 std::filesystem::remove(destination);std::filesystem::remove(s.file);std::filesystem::remove(directory);
 std::cout<<"PASS: registration, draft/commit/cancel/reset, conflicts, unbind, persistence reload, dormant mods, malformed rows, failed-save retention\n";}
