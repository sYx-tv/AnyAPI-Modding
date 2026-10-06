#include "../phase29_contracts.h"
#include "../phase29_inventory_probe.h"
#include "../phase29_api_audit_state.h"
#include <cassert>
#include <cstring>
#include <thread>
#include <vector>
#include <iostream>
int main() {
    struct Component {P29Identity identity; char class_name[32]{};uintptr_t metadata_definition{},vehicle{};unsigned class_source{};} component;
    std::strcpy(component.class_name,"building_door");component.metadata_definition=11;component.class_source=1;component.vehicle=12;
    component.identity.begin(44,12,1);auto retired=component.identity.ticket;component.identity.retire();
    assert(p29_reset_retired_component(component,44,13,2));
    assert(!component.class_name[0] && !component.metadata_definition && !component.class_source && !component.vehicle);
    assert(!component.identity.accepts(retired) && component.identity.ticket.owner==13);
    std::strcpy(component.class_name,"building");component.metadata_definition=15;
    assert(!p29_reset_retired_component(component,44,13,3));
    assert(!std::strcmp(component.class_name,"building") && component.metadata_definition==15 && component.identity.ticket.generation==2);
    P29ApiAuditRow row;assert(!std::strcmp(row.status(false),"NOT_CALLED"));
    row.guard(true);assert(!std::strcmp(row.status(false),"GUARD_ONLY") && !std::strcmp(row.status(true),"QUARANTINED_GUARD_CHECKED"));
    row.functional(true);assert(!std::strcmp(row.status(false),"EXERCISED_CHECKED_PARTIAL"));
    row.functional(false);assert(!std::strcmp(row.status(true),"FAILED"));
    P29Identity lifetime;
    assert(!lifetime.begin(0,1,1) && !lifetime.begin(10,1,0));
    lifetime.begin(10,1,1);auto old=lifetime.ticket;
    assert(lifetime.accepts(old));lifetime.retire();assert(!lifetime.accepts(old));
    lifetime.begin(10,1,2);assert(!lifetime.accepts(old));
    auto newer=lifetime.ticket;lifetime.begin(10,2,3);assert(!lifetime.accepts(newer));
    P29SnapshotQueue<int,2> queue;
    assert(queue.push(old,11));assert(queue.push(lifetime.ticket,22));
    assert(!queue.push(lifetime.ticket,33) && queue.dropped==1);
    int value=0;assert(queue.pop(value,[&](auto t){return lifetime.accepts(t);}) && value==22 && queue.stale==1);
    assert(!queue.pop(value,[&](auto t){return lifetime.accepts(t);}));
    assert(queue.push(lifetime.ticket,44));lifetime.retire();
    assert(!queue.pop(value,[&](auto t){return lifetime.accepts(t);}) && queue.stale==2);
    P29ThreadRecord threads;std::vector<std::thread> workers;
    for(unsigned i=1;i<=4;++i)workers.emplace_back([&,i]{for(unsigned j=0;j<10000;++j)threads.observe(i);});
    for(auto& worker:workers)worker.join();
    assert(threads.calls==40000 && threads.other_thread_calls==30000 && threads.transitions>0);
    double matrix[12]={1,0,0,0,1,0,0,0,1,4,5,6},translation[3]{};
    assert(p29_translation(matrix,translation) && translation[0]==4 && translation[2]==6);
    matrix[0]=std::numeric_limits<double>::quiet_NaN();assert(!p29_translation(matrix,translation));
    unsigned char inventory[0x60]{},item[0xA0]{};uintptr_t address=(uintptr_t)item,definition=123;
    std::memcpy(inventory+0x48,&address,8);std::memcpy(item+0x88,&definition,8);
    auto read=[&](uintptr_t a,void* out,size_t n){
        for(auto pair:{std::pair{(uintptr_t)inventory,sizeof(inventory)},std::pair{(uintptr_t)item,sizeof(item)}})
            if(a>=pair.first && n<=pair.second && a-pair.first<=pair.second-n){std::memcpy(out,(void*)a,n);return true;}
        return false;
    };
    auto probe=p29_probe_handheld((uintptr_t)inventory,true,read);
    assert(probe.header_readable && !probe.empty && probe.item==address && probe.definition==definition);
    probe=p29_probe_handheld((uintptr_t)inventory,false,read);assert(probe.header_readable && probe.empty);
    probe=p29_probe_handheld(1,true,read);assert(!probe.header_readable);
    address=1;std::memcpy(inventory+0x38,&address,8);probe=p29_probe_handheld((uintptr_t)inventory,false,read);
    assert(probe.header_readable && !probe.empty && !probe.definition_readable);
    std::cout<<"PASS: generation/owner reuse, stale queue filtering, saturation, concurrent hook producers, affine translation and bounded inventory reads\n";
}
