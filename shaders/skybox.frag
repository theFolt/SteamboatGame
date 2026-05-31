#version 330 core
in vec3 TexCoords;
out vec4 FragColor;

uniform samplerCube skybox;
uniform vec3 sunDirection;      // kierunek słońca w przestrzeni świata (znormalizowany)
uniform float time;
uniform bool enableSunGlow;

void main() {
    vec3 color = texture(skybox, TexCoords).rgb;
    
    // [IMPROVEMENT] Sun glow (screen-space)
    if (enableSunGlow) {
        vec3 dir = normalize(TexCoords);
        float sunDot = dot(dir, sunDirection);
        float glow = pow(max(0.0, sunDot), 200.0) * 0.8;
        color += vec3(1.0, 0.9, 0.6) * glow;
    }
    
    FragColor = vec4(color, 1.0);
}