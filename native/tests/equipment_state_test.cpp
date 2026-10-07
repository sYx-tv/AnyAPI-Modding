#include "../equipment_state.h"
#include "../wheel_math.h"
#include <cassert>
int main(){equipment::Store store;AnyEquipmentSnapshotV1 out,s;s.context=1;s.sampled_tick=1000;s.count=2;s.slots[0].slot_index=-1;s.slots[1].slot_index=4;s.slots[1].item_id=42;
assert(!store.copy(&out,1000));store.publish(s);assert(store.copy(&out,1000));assert(!store.select(2,4,42,1000));assert(!store.select(1,4,99,1000));assert(!store.select(1,20,42,1000));
auto a=store.select(1,4,42,1000);assert(a);auto b=store.select(1,-1,-1,1000);assert(b&&store.state(a)==ANY_EQUIPMENT_EXPIRED);auto job=store.take(1000);assert(job&&job->ticket==b);assert(!store.take(1000));store.finish(b,true);assert(store.state(b)==ANY_EQUIPMENT_APPLIED);
a=store.select(1,4,42,1000);s.slots[1].item_id=99;s.sampled_tick=1010;store.publish(s);assert(!store.take(1010)&&store.state(a)==ANY_EQUIPMENT_EXPIRED);
a=store.select(1,4,99,1010);store.clear();assert(!store.take(1010)&&store.state(a)==ANY_EQUIPMENT_EXPIRED);
store.publish(s);assert(!store.copy(&out,1511)&&out.count==0);assert(!store.select(1,4,99,1511));out.version=3;assert(!store.copy(&out,1010));assert(!store.copy(nullptr,1000));
assert(wheel::sector(0,-100,40,200,8)==0);assert(wheel::sector(100,0,40,200,8)==2);assert(wheel::sector(0,100,40,200,8)==4);assert(wheel::sector(-100,0,40,200,8)==6);assert(wheel::sector(10,10,40,200,8)==-1);assert(wheel::sector(300,0,40,200,8)==-1);assert(wheel::sector(100,0,40,200,0)==-1);
}
