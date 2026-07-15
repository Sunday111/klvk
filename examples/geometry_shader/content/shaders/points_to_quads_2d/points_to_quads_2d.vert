#version 450

// One point per object; the geometry shader expands it into a shape.
// Object data is pulled from a storage buffer by gl_VertexIndex.

struct Object
{
    vec4 col0;  // columns of the transform matrix
    vec4 col1;
    vec4 col2;
    uint color;  // packed rgba8
    uint type;
    uint padding0;
    uint padding1;
};

layout(std430, set = 0, binding = 0) readonly buffer Objects
{
    Object objects[];
};

layout(location = 0) out vec4 vs_col0;
layout(location = 1) out vec4 vs_col1;
layout(location = 2) out vec4 vs_col2;
layout(location = 3) out vec4 vs_color;
layout(location = 4) out uint vs_type;

void main()
{
    Object object = objects[gl_VertexIndex];
    vs_col0 = object.col0;
    vs_col1 = object.col1;
    vs_col2 = object.col2;
    vs_color = unpackUnorm4x8(object.color);
    vs_type = object.type;
}
