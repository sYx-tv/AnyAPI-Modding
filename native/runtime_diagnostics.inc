// Worker-only command processing. Native hooks only update bounded telemetry.
#include "runtime_diagnostics.h"
static P31Window g_p31_window;
static uint64_t g_p31_command_sequence=0;
static P31Counts p31_counts() {
    P31Counts result{};
    for(size_t i=0;i<P31_HOOK_COUNT;++i)result[i]=(uint64_t)InterlockedCompareExchange64(&g_p27_hook_calls[i],0,0);
    return result; // Cross-hook snapshot is approximate, not a transaction.
}
static void p31_diagnostics_poll() {
    // Atomic file replacement by companion; complete small commands only.
    std::ifstream input(g_game_dir/"phase33_command.txt");
    uint64_t command=0,window=0;std::string operation,outcome;size_t slot=0;
    if(!(input>>command>>operation>>window>>slot>>outcome) || command<=g_p31_command_sequence)return;
    input.close(); // Release the file before ACK so Windows can replace the next command.
    g_p31_command_sequence=command;
    char line[640]{};
    bool accepted=false;
    if(operation=="BEGIN") {
        auto before=p31_counts();accepted=g_p31_window.begin(window,slot,before);
        if(accepted){
            std::snprintf(line,sizeof(line),"PHASE33_WINDOW_BEGIN window=%llu expected=%zu target=%s tick=%llu world_revision=%llu",
                (unsigned long long)window,slot,P27_HOOK_SPECS[slot].label,(unsigned long long)GetTickCount64(),(unsigned long long)g_p29_world_revision.load());
            log_line(ANY_LOG_INFO,"framework",line);
            for(size_t i=0;i<P31_HOOK_COUNT;++i){std::snprintf(line,sizeof(line),"PHASE33_WINDOW_BASELINE window=%llu slot=%zu calls=%llu",(unsigned long long)window,i,(unsigned long long)before[i]);log_line(ANY_LOG_INFO,"framework",line);}
        }
    } else if(operation=="END" && slot==g_p31_window.expected && (outcome=="success" || outcome=="failed" || outcome=="unavailable" || outcome=="uncertain")) {
        auto after=p31_counts();P31Counts delta{};bool reset=false;
        accepted=g_p31_window.finish(window,after,delta,reset);
        if(accepted){
            // ALL hooks get rows, including background and unexpected routes.
            for(size_t i=0;i<P31_HOOK_COUNT;++i){
                std::snprintf(line,sizeof(line),"PHASE33_WINDOW_HOOK window=%llu slot=%zu target=%s before=%llu after=%llu delta=%llu last_tick=%llu last_thread=%u expected=%u",
                    (unsigned long long)window,i,P27_HOOK_SPECS[i].label,(unsigned long long)g_p31_window.before[i],(unsigned long long)after[i],(unsigned long long)delta[i],
                    (unsigned long long)g_p31_hook_last_tick[i].load(),g_p29_hook_threads[i].last.load(),i==slot);
                log_line(ANY_LOG_INFO,"framework",line);
            }
            std::snprintf(line,sizeof(line),"PHASE33_WINDOW_END window=%llu expected=%zu outcome=%s expected_delta=%llu status=%s tick=%llu world_revision=%llu attribution=TEMPORAL_ONLY",
                (unsigned long long)window,slot,outcome.c_str(),(unsigned long long)delta[slot],p31_window_status(outcome=="success",outcome=="unavailable",reset,delta[slot]),(unsigned long long)GetTickCount64(),(unsigned long long)g_p29_world_revision.load());
            log_line(ANY_LOG_INFO,"framework",line);
        }
    }
    std::snprintf(line,sizeof(line),"PHASE33_COMMAND command=%llu operation=%s window=%llu accepted=%u",(unsigned long long)command,operation.c_str(),(unsigned long long)window,accepted);
    log_line(ANY_LOG_INFO,"framework",line);
    // ACK only after the full baseline/result is persisted to the log.
    std::ofstream ack(g_game_dir/"phase33_ack.txt",std::ios::trunc);ack<<command<<" "<<accepted<<"\n";
}
