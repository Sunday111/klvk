#pragma once

#include "klvk/integral_aliases.hpp"

namespace klvk
{

enum class SwapchainPresentMode : u8
{
    PreferLowLatency,
    Fifo,
    Mailbox,
    Immediate,
};

}  // namespace klvk
