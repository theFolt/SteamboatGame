#version 330 core
in vec3 TexCoords;
out vec4 FragColor;

uniform vec3 sunColor = vec3(1.0, 0.9, 0.6);
uniform vec3 skyColor = vec3(0.5, 0.7, 0.9);
uniform vec3 horizonColor = vec3(0.8, 0.85, 0.9);
uniform float u_time;

void main() {
    vec3 dir = normalize(TexCoords);
    float height = dir.y;
    
    // Gradient nieba
    float t = smoothstep(-0.1, 0.4, height);
    vec3 color = mix(horizonColor, skyColor, t);
    
    // Słońce
    vec3 sunDir = normalize(vec3(0.5, 0.8, 0.3));
    float sunAngle = dot(dir, sunDir);
    float sun = smoothstep(0.997, 0.999, sunAngle);
    color += sun * sunColor;
    
    // Chmury (proste)
    float clouds = smoothstep(0.3, 0.7, height) * 
                   (sin(dir.x * 10.0 + u_time * 0.1) * 0.5 + 0.5) *
                   (sin(dir.z * 8.0 + u_time * 0.15) * 0.5 + 0.5);
    color = mix(color, vec3(0.95), clouds * 0.3);
    
    FragColor = vec4(color, 1.0);
}