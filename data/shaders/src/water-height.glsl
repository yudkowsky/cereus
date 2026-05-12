struct GerstnerWave
{
    vec2 direction;
    float wavelength;
    float amplitude;
    float speed;
    float steepness;
};

GerstnerWave waves[6] = GerstnerWave[6]
(
    GerstnerWave(normalize(vec2( 1.0,  0.3)), 8.0, 0.08, 2.2, 0.6),
    GerstnerWave(normalize(vec2( 0.6,  1.0)), 5.5, 0.05, 1.8, 0.6),
    GerstnerWave(normalize(vec2( 0.8, -0.5)), 3.5, 0.03, 1.5, 0.5),
    GerstnerWave(normalize(vec2(-0.4,  0.9)), 2.2, 0.02, 1.2, 0.4),
    GerstnerWave(normalize(vec2(-0.9, -0.2)), 1.4, 0.01, 0.9, 0.3),
    GerstnerWave(normalize(vec2( 0.3, -0.8)), 0.9, 0.01, 0.7, 0.25)
);

// returns offset to flat grid position. will change x and z, not just y
vec3 waterDisplacement(vec2 grid_xz, float time)
{
    vec3 displacement = vec3(0.0);

    for (int wave_index = 0; wave_index < 6; wave_index++)
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

    for (int wave_index = 0; wave_index < 6; wave_index++)
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
