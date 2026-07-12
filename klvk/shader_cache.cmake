find_library(KLVK_SHADERC_LIBRARY NAMES shaderc_shared shaderc REQUIRED)
target_link_libraries(klvk PRIVATE ${KLVK_SHADERC_LIBRARY})
