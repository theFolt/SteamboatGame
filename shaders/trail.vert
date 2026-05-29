#version 330 core
layout (location = 0) in vec2 aPos;   // x, z (w lokalnym układzie łodzi)

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float u_time;
uniform float u_boatSpeed;   // dla amplitudy

out vec3 FragPos;
out vec2 TexCoords;
out float vAlpha;

void main() {
    float z = aPos.y;          // odległość od koła (w tył)
    float x = aPos.x;          // odchylenie od osi łodzi

    // Parametry fali
    float k = 8.0;             // częstotliwość wzdłużna
    float omega = 12.0;        // prędkość kątowa (fale "płyną" do tyłu)
    float m = 5.0;             // częstotliwość poprzeczna (tworzy strukturę V)
    float amp = 0.08 * u_boatSpeed;  // amplituda zależna od prędkości

    // Zanik w miarę oddalania (wykładniczy)
    float attenuation = 1.0 / (1.0 + 2.0 * z * z);

    // Fala poprzeczna + modulacja poprzeczna (cosinus)
    float wave = sin(k * z - omega * u_time) * cos(m * x) * attenuation;

    float y = wave * amp;

    // Ograniczamy zakres – fala istnieje tylko do z = 5.0
    if (z > 5.0) y = 0.0;

    // Alfa maleje wraz z odległością od koła
    vAlpha = (1.0 - z / 5.0) * (1.0 - abs(x) / 2.0);

    vec3 localPos = vec3(x, y, z);
    vec4 worldPos = model * vec4(localPos, 1.0);
    FragPos = worldPos.xyz;

    gl_Position = projection * view * worldPos;

    // Współrzędne tekstury: x dla kierunku, z dla oddalenia
    TexCoords = vec2(x * 0.5 + 0.5, z / 5.0);
}
