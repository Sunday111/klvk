#version 450

// klgl compiled these in as shader defines and recompiled on change;
// specialization constants give the same driver-side constant folding.
layout(constant_id = 0) const int MAX_ITERATIONS = 200;
layout(constant_id = 1) const int INSIDE_OUT_SPACE = 1;
layout(constant_id = 2) const int COLOR_MODE = 0;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 FragColor;

// MAX_ITERATIONS + 1 entries, refreshed by ApplySettings.
layout(std430, set = 0, binding = 0) readonly buffer ColorTable
{
    vec4 uColorTable[];
};

layout(push_constant) uniform PushConstants
{
    vec4 s2w_col0;  // columns of the screen-to-world matrix
    vec4 s2w_col1;
    vec4 s2w_col2;
    vec2 u_resolution;
    vec2 u_julia_constant;
    vec2 u_fractal_power;
} pc;

vec2 insideOutWarp(vec2 pos, vec2 center, float strength) {
    vec2 dir = pos - center;
    float dist = length(dir);

    // Avoid division by zero
    if (dist < 0.001) return pos;

    // Inversion formula: radius squared over distance
    float invertedDist = strength / dist;

    return center + normalize(dir) * invertedDist;
}

vec2 screen_point_to_world(vec2 screen) {
    mat3 u_screen_to_world = mat3(pc.s2w_col0.xyz, pc.s2w_col1.xyz, pc.s2w_col2.xyz);
    return (u_screen_to_world * vec3(screen, 1)).xy;
}

vec2 complexMult(vec2 a, vec2 b) {
    return vec2(
        a.x * b.x - a.y * b.y,
        a.x * b.y + a.y * b.x
    );
}

vec2 complexExp(vec2 z)
{
    return exp(z.x) * vec2(cos(z.y), sin(z.y));
}

vec2 complexPower(vec2 base, vec2 power) {
    float r = length(base);
    float theta = atan(base.x, base.y);
    vec2 log_z = vec2(log(r), theta);
    vec2 exponent = complexMult(power, log_z);
    return complexExp(exponent);
}

vec3 getColor(float smoothIteration) {
    float index = clamp(smoothIteration, 0.0, float(MAX_ITERATIONS));
    int lower = int(floor(index));
    int upper = min(lower + 1, MAX_ITERATIONS);

    float t = index - float(lower); // fractional part

    vec3 colorLower = uColorTable[lower].rgb;
    vec3 colorUpper = uColorTable[upper].rgb;

    return mix(colorLower, colorUpper, t);
}

void main()
{
    // Julia constant
    vec2 c = pc.u_julia_constant;

    // gl_FragCoord is y-down in Vulkan; the transforms are built for y-up GL screens.
    vec2 frag_coord = vec2(gl_FragCoord.x, pc.u_resolution.y - gl_FragCoord.y);
    vec2 world = screen_point_to_world(frag_coord);

    vec2 z = world;
    if (INSIDE_OUT_SPACE != 0)
    {
        vec2 world_center = screen_point_to_world(pc.u_resolution / 2);
        z = insideOutWarp(world, world_center, 1);
    }

    // Julia set iteration
    int i = 0;
    while (i != MAX_ITERATIONS) {
        vec2 p = complexPower(z, pc.u_fractal_power) + c;
        if (dot(p, p) > 4) break;
        z = p;
        ++i;
    }

    if (COLOR_MODE == 1)
    {
        float pp = MAX_ITERATIONS * dot(normalize(world), vec2(1, 0));
        FragColor = vec4(uColorTable[(int(pp) + i) % MAX_ITERATIONS].rgb, 1.0);
    }
    else if (COLOR_MODE == 2)
    {
        FragColor = vec4(uColorTable[(int(MAX_ITERATIONS * (world.x + world.y)) + i) % MAX_ITERATIONS].rgb, 1.0);
    }
    else if (COLOR_MODE == 3)
    {
        float smooth_iteration = float(i) - log2(max(1.0f, log2(sqrt(length(z)))));
        FragColor = vec4(getColor(smooth_iteration), 1.0);
    }
    else if (COLOR_MODE == 4)
    {
        float smooth_iteration = float(i) - log2(log2(length(z)));
        FragColor = vec4(getColor(smooth_iteration), 1.0);
    }
    else
    {
        FragColor = vec4(uColorTable[i].rgb, 1.0);
    }
}
