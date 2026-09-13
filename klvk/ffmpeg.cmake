find_package(PkgConfig REQUIRED)
pkg_check_modules(KLVK_FFMPEG REQUIRED IMPORTED_TARGET GLOBAL
    libavcodec
    libavformat
    libavutil
    libswscale)
target_link_libraries(klvk PRIVATE PkgConfig::KLVK_FFMPEG)
