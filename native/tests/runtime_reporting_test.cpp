#include "../runtime_object_core.h"
#include <cassert>
#include <iostream>
int main(){
    P33ReportLimiter limiter;
    assert(limiter.due(100,1,0));assert(!limiter.due(101,1,0));
    assert(!limiter.due(60099,1,0));assert(limiter.due(60100,1,0));
    assert(limiter.due(60101,2,0));assert(limiter.due(60102,2,1));
    assert(!limiter.due(60103,2,1));assert(limiter.due(2,2,1));
    P33Invalidations<1> journal;journal.sequence=UINT64_MAX;journal.push({},ANY_WORLD_RETIRED);
    AnyInvalidationV1 row{};size_t count=8;uint64_t next=8;
    assert(journal.exhausted && journal.read(UINT64_MAX,&row,1,&count,&next)==ANY_EXT_RESYNC_REQUIRED && !count);
    std::cout<<"PASS: summaries bounded to 60s, transitions/errors immediate, counter/epoch exhaustion fails closed\n";
}
