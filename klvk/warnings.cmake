# YAE includes extra CMake files from both the module and its consuming project.

# Clang 19 added -Wmissing-designated-field-initializers to -Wextra. Vulkan's
# create-info structs are meant to be filled in partly - the fields it names are
# exactly the ones the caller wants left zeroed - so it fires on almost every file
# that touches the API.
#
# PUBLIC because anything built on klvk fills in the same structs, and the
# alternative is the same three-line pragma in every one of their files too.
target_compile_options(klvk PUBLIC $<$<CXX_COMPILER_ID:Clang>:-Wno-missing-designated-field-initializers>)
