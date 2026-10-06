#include "runtime_object_core.h"
static std::atomic<bool> g_p33_ready{false};
static std::atomic<uint64_t> g_p33_transitions{0};
static SRWLOCK g_p33_journal_lock=SRWLOCK_INIT,g_p33_handheld_lock=SRWLOCK_INIT;
static P33Invalidations<1024> g_p33_journal;
static AnyHandheldSnapshotV1 g_p33_handheld[2]{};
static void p33_invalidate(AnyObjectTokenV1 token,uint32_t reason=ANY_OBJECT_RETIRED) {
    AcquireSRWLockExclusive(&g_p33_journal_lock);g_p33_journal.push(token,reason);ReleaseSRWLockExclusive(&g_p33_journal_lock);
    g_p33_transitions.fetch_add(1);
}
static void p33_retire_world() {
    uint64_t old=g_p29_world_revision.load();p33_advance_epoch(g_p29_world_revision);
    p33_invalidate({old,0,0,0,0},ANY_WORLD_RETIRED);
}
