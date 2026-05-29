#version 330 core
in vec3 FragPos;
in vec2 TexCoords;
out vec4 FragColor;

uniform vec3 viewPos;
uniform sampler2D u_noiseTexture; // opcjonalna tekstura piany

void main() {
    float alpha = 0.6 * (1.0 - TexCoords.y); // zanika w miarę oddalania
    vec3 color = mix(vec3(0.6, 0.8, 0.9), vec3(1.0, 1.0, 1.0), TexCoords.x); // delikatna piana
    FragColor = vec4(color, alpha);
}