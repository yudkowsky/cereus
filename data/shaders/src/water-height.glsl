struct GerstnerWave
{
    vec2 direction;
    float wavelength;
    float amplitude;
    float speed;
    float steepness;
};

#define WAVE_COUNT 4

GerstnerWave waves[WAVE_COUNT] = GerstnerWave[WAVE_COUNT]
(
    GerstnerWave(normalize(vec2( 1.0,  0.20)), 0.40, 0.007, 0.19, 1.0),
    GerstnerWave(normalize(vec2( 1.0, -0.35)), 0.35, 0.006, 0.17, 1.1),
    GerstnerWave(normalize(vec2( 1.0,  0.45)), 0.30, 0.005, 0.15, 1.1),
    GerstnerWave(normalize(vec2( 1.0, -0.10)), 0.25, 0.005, 0.13, 1.1)
);

// returns offset to flat grid position. will change x and z, not just y
vec3 waterDisplacement(vec2 grid_xz, float time)
{
    vec3 displacement = vec3(0.0);

    for (int wave_index = 0; wave_index < WAVE_COUNT; wave_index++)
    {
        GerstnerWave wave = waves[wave_index];
        float wavenumber = 6.2831853 / wave.wavelength;
        float dispersion = wave.speed * wavenumber;
        float phase      = wavenumber * dot(wave.direction, grid_xz) - dispersion * time;
        float cosine     = cos(phase);
        float sine       = sin(phase);
        float pinch      = wave.steepness * wave.amplitude;

        displacement.x += pinch * wave.direction.x * cosine;
        displacement.z += pinch * wave.direction.y * cosine;
        displacement.y += wave.amplitude * sine;
    }
    return displacement;
}

vec3 waterNormal(vec2 grid_xz, float time)
{
    float bump_x = 0.0;
    float bump_z = 0.0;
    float horizontality = 1.0;

    for (int wave_index = 0; wave_index < WAVE_COUNT; wave_index++)
    {
        GerstnerWave wave = waves[wave_index];
        float wavenumber = 6.2831853 / wave.wavelength;
        float dispersion = wave.speed * wavenumber;
        float phase      = wavenumber * dot(wave.direction, grid_xz) - dispersion * time;
        float cosine     = cos(phase);
        float sine       = sin(phase);
        float slope      = wavenumber * wave.amplitude;
        float sharpness  = wave.steepness * slope;

        bump_x -= wave.direction.x * slope * cosine;
        bump_z -= wave.direction.y * slope * cosine;
        horizontality -= sharpness * sine;
    }
    return normalize(vec3(bump_x, horizontality, bump_z));
}
