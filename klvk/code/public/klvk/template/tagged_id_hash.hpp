#pragma once

#include "ankerl/unordered_dense.h"
#include "klvk/integral_aliases.hpp"

namespace klvk
{

template <typename Identifier>
struct TaggedIdentifierHash
{
    using is_avalanching = void;

    auto operator()(const Identifier& x) const noexcept -> u64
    {
        return ankerl::unordered_dense::detail::wyhash::hash(&x.GetValue(), sizeof(typename Identifier::Repr));
    }
};
}  // namespace klvk
