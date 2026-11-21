// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Substitution.h"
#include "Luau/ToString.h"
#include "Luau/VisitType.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>

// Build with LUAU_CHECK_WALK_CUTS to check every early stop the new solver's type walks take at
// persistent types or other arenas: the skipped part is walked in full, and anything there the
// walk exists to find, and the rest of the walk did not find, aborts with a message
// (LUAU_WALK_CUTS_NOABORT=1 counts instead). Per-walk counts print to stderr at exit.

namespace Luau::walkcut
{

#ifdef LUAU_CHECK_WALK_CUTS
inline constexpr bool enabled = true;
#else
inline constexpr bool enabled = false;
#endif

enum Walk
{
    InstantiationQueuer,
    InstanceCollector,
    InfiniteTypeFinder,
    FindSimplificationBlockers,
    ReferenceCountInitializer,
    ApplyMappedGenerics,
    Replacer,
    WalkCount
};

inline const char* const walkNames[WalkCount] = {
    "InstantiationQueuer",
    "InstanceCollector",
    "InfiniteTypeFinder",
    "FindSimplificationBlockers",
    "ReferenceCountInitializer",
    "ApplyMappedGenerics",
    "Replacer",
};

struct Counts
{
    std::atomic<uint64_t> cuts[WalkCount] = {};
    std::atomic<uint64_t> hits[WalkCount] = {};
    std::atomic<uint64_t> fired[WalkCount] = {};

    ~Counts()
    {
        if (!enabled)
            return;
        for (int w = 0; w < WalkCount; ++w)
            fprintf(
                stderr,
                "WALKCUT %s cuts=%llu hits=%llu fired=%llu\n",
                walkNames[w],
                (unsigned long long)cuts[w].load(),
                (unsigned long long)hits[w].load(),
                (unsigned long long)fired[w].load()
            );
    }
};

inline Counts counts;

// LUAU_WALK_CUTS_OFF=1 turns every cut off, for comparing results with and without them.
inline bool cutsDisabled()
{
    if constexpr (!enabled)
        return false;
    static const bool off = []
    {
        const char* v = getenv("LUAU_WALK_CUTS_OFF");
        return v && *v == '1';
    }();
    return off;
}

// A cut whose skipped part was walked.
inline void noteCut(Walk w)
{
    counts.cuts[w]++;
}

// Something the walk looks for lies in a skipped part; the rest of the walk may still find it.
inline void noteHit(Walk w)
{
    counts.hits[w]++;
}

// The cut changed the walk's result.
inline void fire(Walk w, const std::string& what)
{
    counts.fired[w]++;
    fprintf(stderr, "LUAU_CHECK_WALK_CUTS: %s skipped %s\n", walkNames[w], what.substr(0, 400).c_str());
    const char* noAbort = getenv("LUAU_WALK_CUTS_NOABORT");
    if (!noAbort || *noAbort != '1')
    {
        fflush(stderr);
        abort();
    }
}

inline void fire(Walk w, const char* what)
{
    fire(w, std::string(what));
}

template<typename T>
std::string describe(T found, const TypeArena* own)
{
    const char* where = found->persistent ? "persistent" : found->owningArena == own ? "this arena" : "another arena";
    return toString(found) + " (" + where + ")";
}

inline void fire(Walk w, TypeId found, const TypeArena* own = nullptr)
{
    fire(w, describe(found, own));
}

inline void fire(Walk w, TypePackId found, const TypeArena* own = nullptr)
{
    fire(w, describe(found, own));
}

// Walks what a Substitution skipped at a cut, following its own stop rules with the cut off.
// One instance per substitution object, so its seen set bounds the total extra work.
struct SubstitutionCutChecker : TypeOnceVisitor
{
    Tarjan& sub;
    bool& cutsOff;
    Walk walk;
    const void* root = nullptr;

    SubstitutionCutChecker(Tarjan& sub, bool& cutsOff, Walk walk)
        : TypeOnceVisitor("SubstitutionCutChecker", /* skipBoundTypes */ false)
        , sub(sub)
        , cutsOff(cutsOff)
        , walk(walk)
    {
    }

    template<typename T>
    void check(T t)
    {
        if (seen.contains((void*)t))
            return;
        noteCut(walk);
        root = t;
        traverse(t);
    }

    template<typename T>
    bool step(T t)
    {
        if (t != root && sub.isDirty(t))
        {
            noteHit(walk);
            fire(walk, t);
            return false;
        }
        cutsOff = true;
        bool stop = sub.ignoreChildren(t);
        cutsOff = false;
        return !stop;
    }

    bool visit(TypeId ty) override
    {
        return step(ty);
    }
    bool visit(TypeId ty, const BoundType&) override
    {
        return true;
    }
    bool visit(TypePackId tp) override
    {
        return step(tp);
    }
    bool visit(TypePackId tp, const BoundTypePack&) override
    {
        return true;
    }
};

} // namespace Luau::walkcut
