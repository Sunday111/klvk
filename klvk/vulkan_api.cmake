# YAE includes extra CMake files from both the module and its consuming project.
get_property(klvk_vulkan_api_configured TARGET klvk PROPERTY KLVK_VULKAN_API_CONFIGURED)
if(NOT klvk_vulkan_api_configured)
    option(KLVK_INLINE_VULKAN_WRAPPERS "Inline KLVK's Vulkan API wrappers" OFF)
    target_compile_definitions(klvk PUBLIC KLVK_INLINE_VULKAN_WRAPPERS=$<BOOL:${KLVK_INLINE_VULKAN_WRAPPERS}>)
    set_property(TARGET klvk PROPERTY KLVK_VULKAN_API_CONFIGURED TRUE)
endif()
