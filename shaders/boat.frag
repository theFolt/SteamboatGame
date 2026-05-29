#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 vNormal;

uniform vec3 objectColor;
uniform vec3 viewPos;

void main() {
    // Obliczanie normalnej z pochodnych
    vec3 X = dFdx(FragPos);
    vec3 Y = dFdy(FragPos);
    vec3 Normal = normalize(cross(X, Y));
    
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    
    // Ambient
    vec3 ambient = vec3(0.3) * objectColor;
    
    // Diffuse
    float diff = max(dot(Normal, lightDir), 0.0);
    vec3 diffuse = diff * objectColor * 0.7;
    
    // Specular
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, Normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    vec3 specular = vec3(0.3) * spec;
    
    vec3 result = ambient + diffuse + specular;
    FragColor = vec4(result, 1.0);
}