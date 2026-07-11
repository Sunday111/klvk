#pragma once

#ifndef KLVK_INLINE_VULKAN_WRAPPERS
#define KLVK_INLINE_VULKAN_WRAPPERS 0
#endif

#if KLVK_INLINE_VULKAN_WRAPPERS
#define KLVK_VK_INLINE inline
#else
#define KLVK_VK_INLINE
#endif
