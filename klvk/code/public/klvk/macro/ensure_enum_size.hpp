#pragma once

#include "magic_enum/magic_enum.hpp"  // IWYU pragma: keep

#ifndef KLVK_ENSURE_ENUM_SIZE
#define KLVK_ENSURE_ENUM_SIZE(Type, ExpectedCount) static_assert(magic_enum::enum_count<Type>() == ExpectedCount)
#else
#error
#endif
