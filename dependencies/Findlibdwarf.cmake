list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

if(TARGET libdwarf::dwarf-static)
    set(libdwarf_FOUND TRUE)
    set(LIBDWARF_LIBRARIES libdwarf::dwarf-static)
else()
    set(libdwarf_FOUND FALSE)
endif()
