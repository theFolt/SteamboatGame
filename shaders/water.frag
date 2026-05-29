#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in float vHeight;
in vec4 clipSpace;
in vec2 texCoords;
in vec3 viewDir;

uniform sampler2D reflectionTexture;
uniform sampler2D refractionTexture;
uniform sampler2D dudvMap;
uniform sampler2D normalMap;
uniform float u_time;
uniform vec3 viewPos;
uniform bool u_isWater;
uniform float u_distortionStrength;
uniform float u_textureTiling;
uniform float u_specularPower;
uniform float u_fresnelPower;

void main() {
    if (!u_isWater) {
        // Podstawowe oświetlenie dla obiektów nie-wodnych
        vec3 Normal = normalize(cross(dFdx(FragPos), dFdy(FragPos)));
        vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
        float diff = max(dot(Normal, lightDir), 0.0);
        vec3 ambient = vec3(0.4);
        vec3 diffuse = diff * vec3(1.0, 0.95, 0.9);
        FragColor = vec4(ambient + diffuse, 1.0);
        return;
    }

    // Współrzędne NDC
    vec2 ndc = (clipSpace.xy / clipSpace.w) * 0.5 + 0.5;

    // Wielowarstwowe zniekształcenie DUDV (4 warstwy)
    vec2 distortion1 = texture(dudvMap, vec2(texCoords.x * 4.0 + u_time * 0.01, 
                                              texCoords.y * 4.0)).rg * 2.0 - 1.0;
    vec2 distortion2 = texture(dudvMap, vec2(texCoords.x * 6.0 - u_time * 0.015, 
                                              texCoords.y * 6.0 + u_time * 0.02)).rg * 2.0 - 1.0;
    vec2 distortion3 = texture(dudvMap, vec2(texCoords.x * 8.0 + u_time * 0.025, 
                                              texCoords.y * 8.0 - u_time * 0.018)).rg * 2.0 - 1.0;
    vec2 distortion4 = texture(dudvMap, vec2(texCoords.x * 10.0 - u_time * 0.03, 
                                              texCoords.y * 10.0 + u_time * 0.022)).rg * 2.0 - 1.0;

    vec2 distortion = (distortion1 * 0.4 + distortion2 * 0.3 + distortion3 * 0.2 + distortion4 * 0.1) 
                      * u_distortionStrength;

    vec2 distortedNdc = ndc + distortion;
    distortedNdc = clamp(distortedNdc, 0.001, 0.999);
    
    // Próbkowanie tekstur
    vec3 reflectionColor = texture(reflectionTexture, distortedNdc).rgb;
    vec3 refractionColor = texture(refractionTexture, distortedNdc).rgb;
    
    // Normalna z mapy
    vec3 normal = normalize(texture(normalMap, texCoords + distortion).rgb * 2.0 - 1.0);

    // Fresnel - ulepszony z parametrem
    float fresnel = pow(1.0 - max(0.0, dot(normal, normalize(viewDir))), u_fresnelPower);
    fresnel = clamp(fresnel, 0.0, 1.0);
    
    // Kolor bazowy wody
    vec3 shallowColor = vec3(0.1, 0.7, 0.9);
    vec3 deepColor = vec3(0.0, 0.2, 0.5);
    float mixFactor = clamp(vHeight * 2.0 + 0.5, 0.0, 1.0);
    vec3 waterBaseColor = mix(deepColor, shallowColor, mixFactor);
    
    // Mieszanie kolorów
    vec3 waterColor = mix(refractionColor, reflectionColor, fresnel);
    waterColor = mix(waterColor, waterBaseColor, 0.3);
    
    // Oświetlenie
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 halfVec = normalize(lightDir + normalize(viewDir));
    float spec = pow(max(dot(normal, halfVec), 0.0), u_specularPower);
    waterColor += vec3(1.0) * spec * 0.5;
    
    // Piana
    float foam = clamp((abs(dFdx(vHeight)) + abs(dFdy(vHeight))) * 15.0, 0.0, 1.0);
    waterColor = mix(waterColor, vec3(0.9, 0.95, 1.0), foam);
    
    FragColor = vec4(waterColor, 0.85);
}