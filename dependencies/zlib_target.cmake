add_library(ZLIB::ZLIB INTERFACE IMPORTED GLOBAL)
target_link_libraries(ZLIB::ZLIB INTERFACE zlibstatic)
