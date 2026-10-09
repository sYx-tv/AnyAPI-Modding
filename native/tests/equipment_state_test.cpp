#include "../equipment_state.h"
#include "../equipment_tools_state.h"
#include "../wheel_math.h"
#include <cassert>
int main(){equipment::Store store;AnyEquipmentSnapshotV1 out,s;s.context=1;s.sampled_tick=1000;s.count=2;s.slots[0].slot_index=-1;s.slots[1].slot_index=4;s.slots[1].item_id=42;
assert(!store.copy(&out,1000));store.publish(s);assert(store.copy(&out,1000));assert(!store.select(2,4,42,1000));assert(!store.select(1,4,99,1000));assert(!store.select(1,20,42,1000));
auto a=store.select(1,4,42,1000);assert(a);auto b=store.select(1,-1,-1,1000);assert(b&&store.state(a)==ANY_EQUIPMENT_EXPIRED);auto job=store.take(1000);assert(job&&job->ticket==b);assert(!store.take(1000));store.finish(b,true);assert(store.state(b)==ANY_EQUIPMENT_APPLIED);
a=store.select(1,4,42,1000);s.slots[1].item_id=99;s.sampled_tick=1010;store.publish(s);assert(!store.take(1010)&&store.state(a)==ANY_EQUIPMENT_EXPIRED);
a=store.select(1,4,99,1010);store.clear();assert(!store.take(1010)&&store.state(a)==ANY_EQUIPMENT_EXPIRED);
store.publish(s);assert(!store.copy(&out,1511)&&out.count==0);assert(!store.select(1,4,99,1511));out.version=3;assert(!store.copy(&out,1010));assert(!store.copy(nullptr,1000));
assert(wheel::sector(0,-100,40,200,8)==0);assert(wheel::sector(100,0,40,200,8)==2);assert(wheel::sector(0,100,40,200,8)==4);assert(wheel::sector(-100,0,40,200,8)==6);assert(wheel::sector(10,10,40,200,8)==-1);assert(wheel::sector(300,0,40,200,8)==-1);assert(wheel::sector(100,0,40,200,0)==-1);
 equipment_tools::Store tools;AnyEquipmentToolsSnapshotV2 ts,to;ts.context=3;ts.sampled_tick=1000;ts.count=2;ts.tools[0].item_id=5;ts.tools[1].item_id=7;tools.publish(ts);
 assert(equipment_tools::tool_class("vehicle_editor_repair"));assert(equipment_tools::tool_class("vehicle_editor_add_edge_2"));assert(!equipment_tools::tool_class("vehicle_editor_add_component"));assert(!equipment_tools::tool_class("vehicle_editor_add_building_component"));assert(!equipment_tools::tool_class("gun_revolver"));assert(!equipment_tools::tool_class("torch"));
 assert(tools.copy(&to,1000)&&to.count==2);assert(!tools.equip(3,99,1000));assert(!tools.equip(2,5,1000));a=tools.equip(3,5,1000);auto tj=tools.take(1000);assert(tj&&tj->item==5&&!tools.take(1000));assert(tools.state(a)==ANY_EQUIPMENT_QUEUED);b=tools.equip(3,7,1000);assert(tools.state(a)==ANY_EQUIPMENT_EXPIRED);tools.finish(a,true);assert(tools.state(a)==ANY_EQUIPMENT_EXPIRED);assert(tools.take(1000));tools.finish(b,true);assert(tools.state(b)==ANY_EQUIPMENT_APPLIED);
 a=tools.equip(3,5,1000);assert(tools.take(1000));ts.count=1;ts.tools[0].item_id=7;tools.publish(ts);assert(tools.state(a)==ANY_EQUIPMENT_EXPIRED);assert(!tools.copy(&to,1501));a=tools.equip(3,7,1000);tools.clear();assert(tools.state(a)==ANY_EQUIPMENT_EXPIRED&&!tools.copy(&to,1000));
 {bool bar[10]{};assert(equipment_tools::reclaim_slot(bar,10,3)==-1);bar[0]=bar[6]=true;assert(equipment_tools::reclaim_slot(bar,10,6)==6);assert(equipment_tools::reclaim_slot(bar,10,3)==0);assert(equipment_tools::reclaim_slot(bar,10,-1)==0);assert(equipment_tools::reclaim_slot(bar,10,12)==0);assert(equipment_tools::reclaim_slot(nullptr,10,0)==-1);assert(equipment_tools::reclaim_slot(bar,0,0)==-1);}
}
