#include "../anybuildstats_logic.h"
#include <cassert>
using namespace anybuildstats;
int main(){
 // Labels read naturally.
 assert(pretty("fuel_tank")=="Fuel tank"&&pretty("__wheel--large.")=="Wheel large"&&pretty("")==""&&pretty(nullptr)=="");
 // Tallies group, default to Other, and sort by count then name.
 std::vector<Tally> t;count(t,"Wheel");count(t,"Seat");count(t,"Wheel");count(t,"");count(t,"Axle");count(t,"Axle");
 sort_tallies(t);assert(t.size()==4&&t[0].name=="Axle"&&t[0].count==2&&t[1].name=="Wheel"&&t[2].name=="Other"&&t[3].name=="Seat");
 // Number and power formatting.
 assert(grouped(0)=="0"&&grouped(999)=="999"&&grouped(1000)=="1,000"&&grouped(1234567)=="1,234,567"&&grouped(-45000)=="-45,000");
 assert(power_text(750)=="750 W"&&power_text(1500)=="1.5 kW"&&power_text(2500000)=="2.50 MW");
 // Horsepower and the drive line.
 assert(hp_text(745.7)=="1 hp"&&hp_text(472000)=="633 hp");
 Drive none;assert(drive_text(none,1500,true).empty());
 Drive d;d.known=true;d.motor_watts=472000;d.motors=4;d.engine_peak_watts=0;
 assert(drive_text(d,1433,true)=="633 hp · 442 hp/t"&&drive_text(d,0,false)=="633 hp");
 // The sheet carries what was measured and leaves out what was not.
 Stats s;s.components=12;s.nodes=40;s.edges=1200;s.plates=30;s.categories={{"Wheel",4},{"Seat",1}};
 double size[3]={4.5,1.8,2.1};Drive e=d;e.engine_peak_watts=74570;e.engines=1;
 std::string text=sheet(s,1500,true,size,1,e);
 assert(text.find("Mass: 1,500 kg")!=std::string::npos&&text.find("4.50 m (X) x 2.10 m (Z) x 1.80 m high")!=std::string::npos);
 assert(text.find("edges: 1,200")!=std::string::npos&&text.find("Power to the wheels: 733 hp")!=std::string::npos);
 assert(text.find("Engines (1, peak seen): 100 hp")!=std::string::npos&&text.find("Electric motors (4, rated): 633 hp")!=std::string::npos);
 assert(text.find("  Wheel: 4")!=std::string::npos&&text.find("Alternators")==std::string::npos&&text.find("Bodies")==std::string::npos);
 Drive host;host.known=true;assert(sheet(s,1500,true,size,1,host).find("none connected")!=std::string::npos);
 std::string unknown=sheet(Stats{},0,false,size,3);
 assert(unknown.find("Mass")==std::string::npos&&unknown.find("Bodies: 3")!=std::string::npos&&unknown.find("Power")==std::string::npos);
 return 0;
}
