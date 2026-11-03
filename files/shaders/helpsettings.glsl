// ArenaMP RENDER052 migration compatibility hooks.
// The former in-material HDR tonemapper has been removed; HDR/tonemapping now
// belongs exclusively to the OpenMW 0.52 PostProcessor pipeline.
vec3 preLight(vec3 x)
{
    return x;
}

vec3 toneMap(vec3 x)
{
    return x;
}
