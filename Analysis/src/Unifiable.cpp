// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include <atomic>
#include "Luau/Unifiable.h"
#include "Luau/TypeFwd.h"
#include "Luau/TypePack.h"

namespace Luau
{
namespace Unifiable
{

static std::atomic<int> nextIndex = 0;

int freshIndex()
{
    return ++nextIndex;
}

template<typename Id>
Error<Id>::Error()
    : index(++nextIndex)
{
}

template<typename Id>
std::atomic<int> Error<Id>::nextIndex = 0;

template struct Error<TypeId>;
template struct Error<TypePackId>;

} // namespace Unifiable
} // namespace Luau
