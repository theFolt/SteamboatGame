#version 330 core
layout (location = 0) in vec3 aPos;

out vec3 FragPos;
out float vHeight;
out vec4 clipSpace;
out vec2 texCoords;
out vec3 viewDir;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float u_time;
uniform bool u_isWater;
uniform vec3 viewPos;
uniform float u_waveTransition;

// --- BUFOR HISTORII STATKU (Kelvin Wake / Zasada Huygensa) ---
const int MAX_POINTS = 90;
uniform vec3  u_histPosR[MAX_POINTS];
uniform vec3  u_histPosL[MAX_POINTS];
uniform float u_histSpeed[MAX_POINTS];
uniform float u_histTime[MAX_POINTS];
uniform int   u_histCount;

// --- Parametry Fal Ślądu ---
uniform float u_wakeBaseAmplitude;
uniform float u_waveNumber;
uniform float u_waveOmega;
uniform float u_wakeSpreadFactor;

// Parametry fal oceanicznych (tło)
const int   NUM_WAVES     = 6;
const vec2  waveDirs[6]   = vec2[](
    vec2(0.8,  0.6), vec2(-0.6, 0.8), vec2(0.5, -0.7),
    vec2(-0.7,-0.5), vec2(0.3,  0.9), vec2(-0.4,-0.9)
);
const float waveAmps[6]   = float[](0.12, 0.10, 0.08, 0.09, 0.06, 0.07);
const float waveFreqs[6]  = float[](1.2,  1.3,  0.9,  1.0,  1.5,  1.1);
const float waveSpeeds[6] = float[](0.8,  0.7,  0.6,  0.65, 0.75, 0.55);
const float waveSteep[6]  = float[](0.4,  0.35, 0.3,  0.38, 0.25, 0.32);

void main()
{
    vec3 pos = aPos;

    if (!u_isWater)
    {
        vHeight   = 0.0;
        texCoords = vec2(0.0);
        vec4 wp   = model * vec4(aPos, 1.0);
        FragPos   = wp.xyz;
        clipSpace = projection * view * wp;
        viewDir   = viewPos - wp.xyz;
        gl_Position = clipSpace;
        return;
    }

    // ------------------------------------------------------------------
    // 1. Fale oceaniczne (tło) — Gerstner
    // ------------------------------------------------------------------
    vec2 posXZ  = pos.xz;
    vec2 offset = vec2(0.0);
    float oceanH = 0.0;

    for (int i = 0; i < NUM_WAVES; i++)
    {
        vec2  dir   = waveDirs[i];
        float amp   = waveAmps[i];
        float freq  = waveFreqs[i];
        float spd   = waveSpeeds[i];
        float steep = waveSteep[i];

        float phase = freq * dot(dir, posXZ) + spd * u_time;
        oceanH  += amp  * u_waveTransition * sin(phase);
        offset  += steep * amp * dir * cos(phase) * u_waveTransition;
    }

    pos.x  += offset.x;
    pos.z  += offset.y;
    pos.y  += oceanH;
    vec3 worldPos = (model * vec4(pos, 1.0)).xyz;

    // ------------------------------------------------------------------
    // 2. Fale generowane z bufora historii (Ślad Statku)
    // ------------------------------------------------------------------
    float totalWakeH = 0.0;
    vec2 totalWakeOffset = vec2(0.0);

    // Fizyka fali (domyślne wartości do przesłania z C++)
    float k = u_waveNumber;     
    float omega = u_waveOmega;

    for (int i = 0; i < u_histCount; i++)
    {
        // Ile czasu minęło od uderzenia w wodę w tym punkcie?
        float dt = u_time - u_histTime[i];
        if (dt < 0.0) continue;

        float speed = u_histSpeed[i];
        if (abs(speed) < 0.1) continue;

        // Spread factor kontroluje jak szybko powiększa się fala
        float spread = 0.5 + u_wakeSpreadFactor * dt; 

        float baseAmplitude = u_wakeBaseAmplitude;

        // --- Ślad prawego koła ---
        vec2 posR = u_histPosR[i].xz;
        float distR = length(worldPos.xz - posR);
        float attR = exp(-distR / spread) * exp(-dt * 0.7); 
        float phaseR = k * distR - omega * dt;
        float hR = baseAmplitude * abs(speed) * attR * sin(phaseR);

        // --- Ślad lewego koła ---
        vec2 posL = u_histPosL[i].xz;
        float distL = length(worldPos.xz - posL);
        float attL = exp(-distL / spread) * exp(-dt * 0.7);
        float phaseL = k * distL - omega * dt;
        float hL = baseAmplitude * abs(speed) * attL * sin(phaseL);

        totalWakeH += (hR + hL);

        // Poziome przesunięcie (choppy)
        if (distR > 0.001) totalWakeOffset -= normalize(worldPos.xz - posR) * hR * 0.1;
        if (distL > 0.001) totalWakeOffset -= normalize(worldPos.xz - posL) * hL * 0.1;
    }

    worldPos.y += totalWakeH;
    worldPos.x += totalWakeOffset.x;
    worldPos.z += totalWakeOffset.y;

    // ------------------------------------------------------------------
    // Wyjścia
    // ------------------------------------------------------------------
    vHeight   = oceanH + totalWakeH;
    texCoords = worldPos.xz * 0.1;
    FragPos   = worldPos;
    clipSpace = projection * view * vec4(worldPos, 1.0);
    viewDir   = viewPos - worldPos;
    gl_Position = clipSpace;
}