// stb ships its implementations inside the headers, guarded by a macro that must
// be defined in exactly one translation unit. This is that unit, so consumers can
// include the headers anywhere without arranging it themselves.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO

#include <stb_image.h>
