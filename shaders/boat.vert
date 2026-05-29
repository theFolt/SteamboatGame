#version 330 core
layout (location = 0) in vec3 aPos;

out vec3 FragPos;
out vec3 vNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    vNormal = vec3(0.0); // Będzie obliczana w fragment shaderze przez pochodne
    gl_Position = projection * view * vec4(FragPos, 1.0);
}