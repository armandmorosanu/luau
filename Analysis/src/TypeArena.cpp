// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details

#include "Luau/TypeArena.h"

#include "Luau/RecursionCounter.h"
#include "Luau/VisitType.h"

LUAU_FASTFLAGVARIABLE(DebugLuauFreezeArena);

namespace Luau
{

void TypeArena::clear()
{
    types.clear();
    typePacks.clear();
}

TypeId TypeArena::addTV(Type&& tv)
{
    TypeId allocated = types.allocate(std::move(tv));

    asMutable(allocated)->owningArena = this;

    return allocated;
}

TypeId TypeArena::freshType(NotNull<BuiltinTypes> builtins, TypeLevel level)
{
    TypeId allocated = types.allocate(FreeType{level, builtins->neverType, builtins->unknownType});

    asMutable(allocated)->owningArena = this;

    return allocated;
}

TypeId TypeArena::freshType(NotNull<BuiltinTypes> builtins, Scope* scope)
{
    TypeId allocated = types.allocate(FreeType{scope, builtins->neverType, builtins->unknownType});

    asMutable(allocated)->owningArena = this;

    return allocated;
}

TypeId TypeArena::freshType(NotNull<BuiltinTypes> builtins, Scope* scope, TypeLevel level)
{
    TypeId allocated = types.allocate(FreeType{scope, level, builtins->neverType, builtins->unknownType});

    asMutable(allocated)->owningArena = this;

    return allocated;
}

TypePackId TypeArena::freshTypePack(Scope* scope, Polarity polarity)
{
    TypePackId allocated = typePacks.allocate(FreeTypePack{scope, polarity});

    asMutable(allocated)->owningArena = this;

    return allocated;
}

TypePackId TypeArena::addTypePack(std::initializer_list<TypeId> types)
{
    TypePackId allocated = typePacks.allocate(TypePack{std::move(types)});

    asMutable(allocated)->owningArena = this;

    return allocated;
}

TypePackId TypeArena::addTypePack(std::vector<TypeId> types, std::optional<TypePackId> tail)
{
    TypePackId allocated = typePacks.allocate(TypePack{std::move(types), tail});

    asMutable(allocated)->owningArena = this;

    return allocated;
}

TypePackId TypeArena::addTypePack(TypePack tp)
{
    TypePackId allocated = typePacks.allocate(std::move(tp));

    asMutable(allocated)->owningArena = this;

    return allocated;
}

TypePackId TypeArena::addTypePack(TypePackVar tp)
{
    TypePackId allocated = typePacks.allocate(std::move(tp));

    asMutable(allocated)->owningArena = this;

    return allocated;
}

TypeId TypeArena::addTypeFunction(const TypeFunction& function, std::initializer_list<TypeId> types)
{
    return addType(TypeFunctionInstanceType{function, std::move(types)});
}

TypeId TypeArena::addTypeFunction(const TypeFunction& function, std::vector<TypeId> typeArguments, std::vector<TypePackId> packArguments)
{
    return addType(TypeFunctionInstanceType{function, std::move(typeArguments), std::move(packArguments)});
}

TypePackId TypeArena::addTypePackFunction(const TypePackFunction& function, std::initializer_list<TypeId> types)
{
    return addTypePack(TypeFunctionInstanceTypePack{NotNull{&function}, std::move(types)});
}

TypePackId TypeArena::addTypePackFunction(const TypePackFunction& function, std::vector<TypeId> typeArguments, std::vector<TypePackId> packArguments)
{
    return addTypePack(TypeFunctionInstanceTypePack{NotNull{&function}, std::move(typeArguments), std::move(packArguments)});
}

void TypeArena::recordSingletonStats(const NotNull<SingletonType> singleton)
{
    auto record = [this](auto&& s)
    {
        using T = std::decay_t<decltype(s)>;

        if constexpr (std::is_same_v<T, BooleanSingleton>)
            boolSingletonsMinted += 1;
        else if constexpr (std::is_same_v<T, StringSingleton>)
        {
            strSingletonsMinted += 1;
            if (!s.value.empty())
                uniqueStrSingletonsMinted.insert(s.value);
        }
        else
            static_assert(always_false_v<T>, "Missing allocation count support for this kind of singleton");
    };

    visit(record, singleton->variant);
}

void freeze(TypeArena& arena)
{
    if (!FFlag::DebugLuauFreezeArena)
        return;

    arena.types.freeze();
    arena.typePacks.freeze();
}

void unfreeze(TypeArena& arena)
{
    arena.sealed = false;
    if (!FFlag::DebugLuauFreezeArena)
        return;

    arena.types.unfreeze();
    arena.typePacks.unfreeze();
}

namespace
{

struct InstanceFinder : TypeOnceVisitor
{
    const TypeArena* arena;
    bool found = false;

    explicit InstanceFinder(const TypeArena* arena)
        : TypeOnceVisitor("InstanceFinder", /* skipBoundTypes */ false)
        , arena(arena)
    {
    }

    template<typename T>
    void check(T t)
    {
        try
        {
            traverse(t);
        }
        catch (RecursionLimitException&)
        {
            found = true;
        }
    }

    template<typename T>
    bool enter(T t) const
    {
        if (found || t->persistent)
            return false;
        const TypeArena* a = t->owningArena;
        return a == arena || !a || !a->sealed || a->instancesReachable;
    }

    bool visit(TypeId ty) override
    {
        return enter(ty);
    }
    bool visit(TypePackId tp) override
    {
        return enter(tp);
    }
    bool visit(TypeId, const PendingExpansionType&) override
    {
        found = true;
        return false;
    }
    bool visit(TypeId, const TypeFunctionInstanceType&) override
    {
        found = true;
        return false;
    }
    bool visit(TypePackId, const TypeFunctionInstanceTypePack&) override
    {
        found = true;
        return false;
    }
};

} // namespace

void seal(TypeArena& arena)
{
    freeze(arena);
    InstanceFinder finder{&arena};
    arena.types.forEach(
        [&](Type* t)
        {
            if (!finder.found)
                finder.check(TypeId(t));
        }
    );
    arena.typePacks.forEach(
        [&](TypePackVar* tp)
        {
            if (!finder.found)
                finder.check(TypePackId(tp));
        }
    );
    arena.instancesReachable = finder.found;
    arena.sealed = true;
}

} // namespace Luau
