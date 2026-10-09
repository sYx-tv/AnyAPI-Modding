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
 // The sheet carries what was measured and leaves out what was not.
 Stats s;s.components=12;s.nodes=40;s.edges=1200;s.plates=30;s.motor_watts=20000;s.categories={{"Wheel",4},{"Seat",1}};
 double size[3]={4.5,1.8,2.1};std::string text=sheet(s,1500,true,size,1);
 assert(text.find("Mass: 1,500 kg")!=std::string::npos&&text.find("4.50 m (X) x 2.10 m (Z) x 1.80 m high")!=std::string::npos);
 assert(text.find("edges: 1,200")!=std::string::npos&&text.find("Electric motors: 20.0 kW")!=std::string::npos&&text.find("13.3 kW/t")!=std::string::npos);
 assert(text.find("  Wheel: 4")!=std::string::npos&&text.find("Alternators")==std::string::npos&&text.find("Bodies")==std::string::npos);
 std::string unknown=sheet(Stats{},0,false,size,3);
 assert(unknown.find("Mass")==std::string::npos&&unknown.find("Bodies: 3")!=std::string::npos&&unknown.find("power to weight")==std::string::npos);
 return 0;
}
