#include "runtime_pipeline_map.h"
static P33ReportLimiter g_p33_limiter;
static bool p33_summary_due() {
    uint64_t entered=0;
    for(size_t i=0;i<165;++i)entered+=g_p29_hook_threads[i].calls.load()!=0;
    uint64_t transition=entered+g_p29_world_revision.load()*166;
    uint64_t errors=g_p29_inventory_failures.load()+g_p29_transform_invalid.load()+
        uint64_t(InterlockedCompareExchange64(&g_p27_protection_failures,0,0));
    return g_p33_limiter.due(GetTickCount64(),transition,errors);
}
static void p33_report() {
    char line[640]{};
    std::snprintf(line,sizeof(line),"PHASE33_EXTENSION slots=7 version=1 world_epoch=%llu ready=%u external_mods=0 inventory=HANDHELD_CACHE_ONLY quantity=UNKNOWN identity=UNKNOWN role=UNKNOWN",
        (unsigned long long)g_p29_world_revision.load(),g_p33_ready.load()?1u:0u);
    log_line(ANY_LOG_INFO,"framework",line);
    for(const auto& pipeline:P33_PIPELINES){
        std::snprintf(line,sizeof(line),"PHASE33_PIPELINE event=%s type_id=%u handler_slot=%zu handler_calls=%llu sender=NOT_INSTRUMENTED parser=NOT_INSTRUMENTED payload=NOT_VALIDATED outcome=NOT_VALIDATED attribution=TEMPORAL_ONLY",
            pipeline.event,pipeline.type_id,pipeline.slot,(unsigned long long)g_p29_hook_threads[pipeline.slot].calls.load());
        log_line(ANY_LOG_INFO,"framework",line);
    }
}
