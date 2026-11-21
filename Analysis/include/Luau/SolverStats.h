// EXPERIMENT: process-wide solver statistics, printed by luau-lsp analyze when LUAU_SOLVER_STATS is set.
#pragma once

#include "Luau/TimeTrace.h"

#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace Luau::stats
{

enum Id : int
{
    // timers (inclusive, outermost call per thread only)
    TModuleCheck,
    TGenerate,
    TSolve,
    TTypeCheck2,
    TDispatch,
    TUnblock,
    TGeneralize,
    TSubtyping,
    TNormalize,
    TInhabited,
    TSimplify,
    TReduce,
    TOverload,
    TFcPre,
    TFcResolve,
    TFcUnify,
    TFcInst2,
    TFcQueuer,
    TCollect,
    TReducer,
    TDispatchKind0, // + constraint variant index
    TimerEnd = TDispatchKind0 + 32,
    // counters
    CPasses = TimerEnd,
    CForcePasses,
    CChecks,
    CSkips,
    CAttempts,
    CSuccesses,
    CForcedAttempts,
    CForcedFails,
    CEraseShift,
    CPushes,
    CPeakUnsolved,
    CKind0, // + constraint variant index: attempts per kind
    CKindOk0 = CKind0 + 32,
    CReduceCalls = CKindOk0 + 32,
    CReduceCacheHit,
    CReduceEmpty,
    CReduceSteps,
    CQNodes,
    CQPersistent,
    CQForeign,
    CICNodes,
    CICPersistent,
    CICForeign,
    CSubTop,
    CSubCov,
    CSubCacheHit,
    CSubEnvHit,
    CSubIdentity,
    CSubPersistentPair,
    CAmgNodes,
    CAmgPersistent,
    CAmgForeign,
    CAmgFnStop,
    IdEnd,
    InTC2Base = IdEnd, // + timer id: time spent while inside TypeChecker2
    AllEnd = InTC2Base + TimerEnd,
};

inline std::atomic<uint64_t> values[AllEnd];
inline thread_local int depth[TimerEnd];

inline void add(int id, uint64_t n = 1)
{
    values[id].fetch_add(n, std::memory_order_relaxed);
}

inline void max(int id, uint64_t n)
{
    uint64_t cur = values[id].load(std::memory_order_relaxed);
    while (n > cur && !values[id].compare_exchange_weak(cur, n, std::memory_order_relaxed))
    {
    }
}

struct Named
{
    uint64_t calls = 0;
    uint64_t ns = 0;
};
inline std::mutex namedMutex;
inline std::map<std::string, Named> named;

inline void addNamed(const std::string& name, uint64_t ns)
{
    std::lock_guard g(namedMutex);
    Named& n = named[name];
    n.calls++;
    n.ns += ns;
}

struct VisitCounts
{
    uint64_t instances = 0;
    uint64_t nodes = 0;
    uint64_t persistent = 0;
    uint64_t frozen = 0;
};
inline std::map<std::string, VisitCounts> visits;

inline void addVisit(const std::string& name, uint64_t nodes, uint64_t persistent, uint64_t frozen)
{
    std::lock_guard g(namedMutex);
    VisitCounts& v = visits[name];
    v.instances++;
    v.nodes += nodes;
    v.persistent += persistent;
    v.frozen += frozen;
}

// Counts one node a visitor walks into; T is TypeId or TypePackId.
struct NodeCounter
{
    uint64_t nodes = 0;
    uint64_t persistent = 0;
    uint64_t frozen = 0;

    template<typename T>
    void count(T t)
    {
        nodes++;
        if (t->persistent)
            persistent++;
        else if (t->owningArena && t->owningArena->types.isFrozen())
            frozen++;
    }

    void flush(const std::string& name) const
    {
        if (nodes)
            addVisit(name, nodes, persistent, frozen);
    }
};

struct Timer
{
    int id;
    double start = 0;
    explicit Timer(int id)
        : id(id)
    {
        if (depth[id]++ == 0)
            start = TimeTrace::getClock();
    }
    ~Timer()
    {
        if (--depth[id] == 0)
        {
            uint64_t ns = uint64_t((TimeTrace::getClock() - start) * 1e9);
            add(id, ns);
            if (depth[TTypeCheck2] > 0)
                add(InTC2Base + id, ns);
        }
    }
};

} // namespace Luau::stats

#define LUAU_STATS_TIMER(id) ::Luau::stats::Timer luauStatsTimer_##__LINE__(id)
