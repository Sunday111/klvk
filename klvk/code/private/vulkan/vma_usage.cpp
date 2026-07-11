// VMA implementation lives in this translation unit. Function pointers come from volk,
// so static linking against the loader is disabled.
#include <volk.h>

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>  // IWYU pragma: keep
