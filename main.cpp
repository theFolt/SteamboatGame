#include "glad.h"
#include "glfw3.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

#include "boat_geometry.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// --- Struktura FBO ---
struct WaterFBO {
    unsigned int fbo, texture, depthBuffer;
    int width, height;
    void init(int w, int h) {
        width = w/2; height = h/2; // [IMPROVEMENT] half-res reflection
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        glGenRenderbuffers(1, &depthBuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cout << "Błąd: FBO niekompletny!" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    void cleanup() { glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &texture); glDeleteRenderbuffers(1, &depthBuffer); }
    void bind() { glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, width, height); }
    void unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }
};

// --- Parametry fal (CPU-side, muszą być identyczne z shaderem) ---
struct WaveParams { glm::vec2 dir; float amp, freq, speed, steepness; };
const WaveParams waves[] = {
    {glm::vec2(0.8f,  0.6f), 0.12f, 1.2f, 0.8f,  0.4f},
    {glm::vec2(-0.6f, 0.8f), 0.10f, 1.3f, 0.7f,  0.35f},
    {glm::vec2(0.5f, -0.7f), 0.08f, 0.9f, 0.6f,  0.3f},
    {glm::vec2(-0.7f,-0.5f), 0.09f, 1.0f, 0.65f, 0.38f},
    {glm::vec2(0.3f,  0.9f), 0.06f, 1.5f, 0.75f, 0.25f},
    {glm::vec2(-0.4f,-0.9f), 0.07f, 1.1f, 0.55f, 0.32f}
};
float getWaterHeight(float x, float z, float t, bool includeWaves = true) {
    float h = 0.0f;
    if (!includeWaves) return h;
    for (const auto& w : waves)
        h += w.amp * sinf(w.freq * (w.dir.x * x + w.dir.y * z) + w.speed * t);
    return h;
}

// Pobranie wysokości wody z 9 punktów dla obliczenia roll/pitch
void getWaterHeightsGrid(float centerX, float centerZ, float t, float spacing,
    float heights[3][3], bool includeWaves = true) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            float x = centerX + (i - 1) * spacing;
            float z = centerZ + (j - 1) * spacing;
            heights[i][j] = getWaterHeight(x, z, t, includeWaves);
        }
    }
}

// Obliczanie sił hydrostatycznych
void updateBoatPhysics(float dt, float centerX, float centerZ, float t,
    float& heavePos, float& heaveVel,
    float& roll, float& rollVel,
    float& pitch, float& pitchVel,
    float mass, float heaveStiffness, float heaveDamping,
    float rollStiffness, float rollDamping,
    float pitchStiffness, float pitchDamping,
    bool includeWaves) {

    const float spacing = 0.8f; // Odległość między punktami pomiaru
    float heights[3][3];
    getWaterHeightsGrid(centerX, centerZ, t, spacing, heights, includeWaves);

    // Średnia wysokość wody
    float avgHeight = 0.0f;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            avgHeight += heights[i][j];
        }
    }
    avgHeight /= 9.0f;

    // Siła wyporu: F = -k * displacement - c * velocity
    float displacement = avgHeight - heavePos;
    float heaveForce = heaveStiffness * displacement - heaveDamping * heaveVel;

    // Przechylenie boczne (Roll): różnica wysokości lewo-prawo
    float heightLeft = (heights[0][0] + heights[0][1] + heights[0][2]) / 3.0f;
    float heightRight = (heights[2][0] + heights[2][1] + heights[2][2]) / 3.0f;
    float rollTarget = (heightRight - heightLeft) * 0.5f; // Siła do wyrównania
    float rollForce = rollStiffness * (rollTarget - roll) - rollDamping * rollVel;

    // Przechylenie przednie (Pitch): różnica wysokości przód-tył
    float heightFront = (heights[0][0] + heights[1][0] + heights[2][0]) / 3.0f;
    float heightBack = (heights[0][2] + heights[1][2] + heights[2][2]) / 3.0f;
    float pitchTarget = (heightFront - heightBack) * 0.5f;
    float pitchForce = pitchStiffness * (pitchTarget - pitch) - pitchDamping * pitchVel;

    // Integracja: a = F / m
    heaveVel += (heaveForce / mass) * dt;
    heavePos += heaveVel * dt;

    rollVel += (rollForce / mass) * dt;
    roll += rollVel * dt;
    roll = glm::clamp(roll, -0.5f, 0.5f); // Limit do rozsądnych wartości

    pitchVel += (pitchForce / mass) * dt;
    pitch += pitchVel * dt;
    pitch = glm::clamp(pitch, -0.5f, 0.5f);
}

// --- Ładowanie shaderów ---
std::string loadShaderFromFile(const std::string& path) {
    std::ifstream file(path); std::stringstream buf; buf << file.rdbuf(); return buf.str();
}
unsigned int createShaderProgram(const std::string& vertPath, const std::string& fragPath) {
    std::string vertCode = loadShaderFromFile(vertPath);
    std::string fragCode = loadShaderFromFile(fragPath);
    const char* vSrc = vertCode.c_str();
    const char* fSrc = fragCode.c_str();
    char log[512]; int ok;
    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vSrc, NULL); glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(vs, 512, NULL, log); std::cout << "VS error: " << vertPath << "\n" << log << std::endl; }
    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fSrc, NULL); glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) { glGetShaderInfoLog(fs, 512, NULL, log); std::cout << "FS error: " << fragPath << "\n" << log << std::endl; }
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { glGetProgramInfoLog(prog, 512, NULL, log); std::cout << "Link error: " << log << std::endl; }
    glDeleteShader(vs); glDeleteShader(fs);
    return prog;
}

unsigned int createVAO(const float* vertices, size_t vertexCount) {
    unsigned int VAO, VBO;
    glGenVertexArrays(1, &VAO); glGenBuffers(1, &VBO);
    glBindVertexArray(VAO); glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertexCount * sizeof(float), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0); glBindVertexArray(0);
    return VAO;
}

// --- Ulepszona, kafelkowalna proceduralna mapa DuDV ---
unsigned int generateDUDVTexture() {
    unsigned int tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    const int size = 512; std::vector<unsigned char> data(size * size * 3);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            // Przejście na współrzędne kątowe (0 do 2*PI) zapewniające idealny tiling
            float tx = ((float)x / size) * 2.0f * 3.1415926f;
            float ty = ((float)y / size) * 2.0f * 3.1415926f;

            // Nakładanie fal liniowych o różnych częstotliwościach
            float du = sinf(tx * 4.0f - ty * 2.0f) * 0.4f + cosf(tx * 2.0f + ty * 5.0f) * 0.2f;
            float dv = cosf(tx * 3.0f + ty * 3.0f) * 0.4f + sinf(tx * 5.0f - ty * 2.0f) * 0.2f;

            int idx = (y * size + x) * 3;
            data[idx] = (unsigned char)((du * 0.5f + 0.5f) * 255.0f); // R (u-offset)
            data[idx + 1] = (unsigned char)((dv * 0.5f + 0.5f) * 255.0f); // G (v-offset)
            data[idx + 2] = 255;                                          // B
        }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D); return tex;
}

// --- Ulepszona, kafelkowalna proceduralna mapa normalnych wody ---
unsigned int generateNormalTexture() {
    unsigned int tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    const int size = 512; std::vector<unsigned char> data(size * size * 3);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float tx = ((float)x / size) * 2.0f * 3.1415926f;
            float ty = ((float)y / size) * 2.0f * 3.1415926f;

            // Generowanie drobnych zaburzeń kierunkowych (falek)
            float hx = sinf(tx * 6.0f + ty * 3.0f) * 0.5f + sinf(tx * 12.0f - ty * 6.0f) * 0.25f;
            float hy = cosf(tx * 4.0f - ty * 5.0f) * 0.5f + cosf(tx * 8.0f + ty * 10.0f) * 0.25f;

            // Obliczenie wektora normalnego w przestrzeni stycznej (Z skierowane w górę mapy)
            glm::vec3 n = glm::normalize(glm::vec3(hx * 0.25f, hy * 0.25f, 1.0f));

            int idx = (y * size + x) * 3;
            data[idx] = (unsigned char)((n.x * 0.5f + 0.5f) * 255.0f); // R
            data[idx + 1] = (unsigned char)((n.y * 0.5f + 0.5f) * 255.0f); // G
            data[idx + 2] = (unsigned char)((n.z * 0.5f + 0.5f) * 255.0f); // B
        }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D); return tex;
}

// --- NOWOŚĆ: Proceduralna tekstura organicznej piany morskiej ---
unsigned int generateFoamTexture() {
    unsigned int tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    const int size = 512; std::vector<unsigned char> data(size * size * 3);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float tx = ((float)x / size) * 2.0f * 3.1415926f;
            float ty = ((float)y / size) * 2.0f * 3.1415926f;

            // Nakładanie wysokich częstotliwości (szum sinusoidalny Perlin-like)
            float n = sinf(tx * 15.0f) * cosf(ty * 15.0f)
                + sinf(tx * 30.0f + ty * 10.0f) * 0.5f
                + cosf(tx * 8.0f - ty * 45.0f) * 0.25f;
            n = (n / 1.75f) * 0.5f + 0.5f; // Sprowadzenie do przedziału 0.0 - 1.0

            // Przepuszczenie przez filtr nieliniowy, aby uzyskać strukturę pęcherzyków/włókien
            float foamIntensity = 0.0f;
            if (n > 0.52f) {
                foamIntensity = (n - 0.52f) / 0.48f;
                foamIntensity = powf(foamIntensity, 1.5f); // Wyostrzenie krawędzi piany
            }
            unsigned char c = (unsigned char)(foamIntensity * 255.0f);

            int idx = (y * size + x) * 3;
            data[idx] = c; // R
            data[idx + 1] = c; // G
            data[idx + 2] = c; // B
        }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D); return tex;
}

unsigned int loadCubemap(const std::vector<std::string>& faces) {
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);
    int width, height, nrChannels;
    for (unsigned int i = 0; i < faces.size(); i++) {
        unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
        if (data) {
            GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        }
        else {
            std::cout << "Failed to load cubemap texture: " << faces[i] << std::endl;
            return 0; // Return 0 if any texture fails to load
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    return textureID;
}


unsigned int loadTexture(const char* path) {
    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    int w, h, nrCh;
    unsigned char* data = stbi_load(path, &w, &h, &nrCh, 0);
    if (data) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, (nrCh == 4) ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    stbi_image_free(data);
    return tex;
}



// ===================================================================
// MAIN
// ===================================================================
int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Steamboat - Dynamic Wake History", NULL, NULL);
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::cout << "GLAD error\n"; return -1; }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


    unsigned int cubemapTexture = loadCubemap({
    "skybox/right.png", "skybox/left.png",
    "skybox/top.png", "skybox/bottom.png",
    "skybox/front.png", "skybox/back.png"
        });

    unsigned int waterProgram = createShaderProgram("shaders/water.vert", "shaders/water.frag");
    unsigned int skyboxProgram = createShaderProgram("shaders/skybox.vert", "shaders/skybox.frag");
    unsigned int boatProgram = createShaderProgram("shaders/boat.vert", "shaders/boat.frag");

    // --- Uniformy: woda ---
    GLint wModel = glGetUniformLocation(waterProgram, "model");
    GLint wView = glGetUniformLocation(waterProgram, "view");
    GLint wProjection = glGetUniformLocation(waterProgram, "projection");
    GLint wTime = glGetUniformLocation(waterProgram, "u_time");
    GLint wIsWater = glGetUniformLocation(waterProgram, "u_isWater");
    GLint wViewPos = glGetUniformLocation(waterProgram, "viewPos");
    GLint wDistStr = glGetUniformLocation(waterProgram, "u_distortionStrength");
    GLint wTexTile = glGetUniformLocation(waterProgram, "u_textureTiling");
    GLint wSpecPow = glGetUniformLocation(waterProgram, "u_specularPower");
    GLint wFresPow = glGetUniformLocation(waterProgram, "u_fresnelPower");
    GLint wWaveTransition = glGetUniformLocation(waterProgram, "u_waveTransition");

    // --- Uniformy do bufora historii ---
    GLint wHistPosR = glGetUniformLocation(waterProgram, "u_histPosR");
    GLint wHistPosL = glGetUniformLocation(waterProgram, "u_histPosL");
    GLint wHistSpeed = glGetUniformLocation(waterProgram, "u_histSpeed");
    GLint wHistTime = glGetUniformLocation(waterProgram, "u_histTime");
    GLint wHistCount = glGetUniformLocation(waterProgram, "u_histCount");

    // --- Uniformy Fal Ślądu ---
    GLint wWakeBaseAmplitude = glGetUniformLocation(waterProgram, "u_wakeBaseAmplitude");
    GLint wWaveNumber = glGetUniformLocation(waterProgram, "u_waveNumber");
    GLint wWaveOmega = glGetUniformLocation(waterProgram, "u_waveOmega");
    GLint wWakeSpreadFactor = glGetUniformLocation(waterProgram, "u_wakeSpreadFactor");

    // --- Uniformy: łódka ---
    GLint bModel = glGetUniformLocation(boatProgram, "model");
    GLint bView = glGetUniformLocation(boatProgram, "view");
    GLint bProjection = glGetUniformLocation(boatProgram, "projection");
    GLint bColor = glGetUniformLocation(boatProgram, "objectColor");
    GLint bViewPos = glGetUniformLocation(boatProgram, "viewPos");


    // --- Uniformy: skybox ---
    GLint sProjection = glGetUniformLocation(skyboxProgram, "projection");
    GLint sView = glGetUniformLocation(skyboxProgram, "view");
    GLint sTime = glGetUniformLocation(skyboxProgram, "time"); // Changed from "u_time" to match skybox.frag
    GLint sSunDir = glGetUniformLocation(skyboxProgram, "sunDirection");
    GLint sSunGlow = glGetUniformLocation(skyboxProgram, "enableSunGlow");

    // Define a normalized direction for your light source (sun)
    glm::vec3 sunDir = glm::normalize(glm::vec3(0.5f, 0.8f, -0.4f));

    WaterFBO reflectionFBO, refractionFBO;
    reflectionFBO.init(1280, 720);
    refractionFBO.init(1280, 720);

    unsigned int dudvTexture = generateDUDVTexture();
    unsigned int normalTexture = generateNormalTexture();
    unsigned int foamTexture = generateFoamTexture();

    float skyboxVertices[] = {
        // positions          
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

        -1.0f,  1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f
    };

    unsigned int skyboxVAO = createVAO(skyboxVertices, sizeof(skyboxVertices) / sizeof(float));

    const float waterHalfSize = 35.0f;
    const int   segments = 1600;
    std::vector<float> waterVertices;
    waterVertices.reserve(segments * segments * 6 * 3);
    float step = (2.0f * waterHalfSize) / segments;
    float start = -waterHalfSize;
    for (int i = 0; i < segments; ++i) {
        float x0 = start + i * step, x1 = x0 + step;
        for (int j = 0; j < segments; ++j) {
            float z0 = start + j * step, z1 = z0 + step;
            waterVertices.insert(waterVertices.end(), { x0,0,z0, x1,0,z0, x0,0,z1 });
            waterVertices.insert(waterVertices.end(), { x1,0,z0, x1,0,z1, x0,0,z1 });
        }
    }
    unsigned int waterVAO = createVAO(waterVertices.data(), waterVertices.size());

    unsigned int hullVAO = createVAO(hullVertices, hullVertexCount);
    unsigned int deckVAO = createVAO(deckVertices, deckVertexCount);
    unsigned int cabinVAO = createVAO(cabinVertices, cabinVertexCount);
    unsigned int wheelhouseVAO = createVAO(wheelhouseVertices, wheelhouseVertexCount);
    unsigned int funnelVAO = createVAO(funnelVertices, funnelVertexCount);
    unsigned int wheelHubR_VAO = createVAO(wheelHubRightVertices, wheelHubRightVertexCount);
    unsigned int wheelPaddlesR_VAO = createVAO(wheelPaddlesRightVertices, wheelPaddlesRightVertexCount);
    unsigned int wheelHubL_VAO = createVAO(wheelHubLeftVertices, wheelHubLeftVertexCount);
    unsigned int wheelPaddlesL_VAO = createVAO(wheelPaddlesLeftVertices, wheelPaddlesLeftVertexCount);

    // --- Stan łódki ---
    glm::vec3 boatPos(0.0f);
    float boatYaw = 0.0f, boatSpeed = 0.0f, rudder = 0.0f, wheelAngle = 0.0f;
    const float maxSpeed = 3.0f, accel = 0.7f, brakeF = 1.5f;
    const float rudderSpd = 0.4f, rudderDecay = 1.0f, turnSpeed = 1.5f;
    const float dtLimit = 0.05f, eps = 0.01f;

    double lastTime = glfwGetTime();
    float distortionStrength = 0.03f, textureTiling = 0.1f;
    float specularPower = 256.0f, fresnelPower = 5.0f;

    // --- Kontrola Przełącznika Fal ---
    bool enableDefaultWaves = true;
    float waveTransition = 1.0f;
    double lastWaveToggleTime = -10.0;
    const float waveTransitionDuration = 3.0f;
    float boatDraft = 0.2f;

    // --- Parametry Fal Ślądu Statku ---
    float wakeBaseAmplitude = 0.020f;
    float waveNumber = 8.0f;
    float waveOmega = 4.5f;
    float wakeSpreadFactor = 0.05f;

    // =========================================================
    // BUFOR HISTORII ŚLADU WODY
    // =========================================================
    const int MAX_WAKE_POINTS = 90;
    std::vector<float> histPosR(MAX_WAKE_POINTS * 3, 0.0f);
    std::vector<float> histPosL(MAX_WAKE_POINTS * 3, 0.0f);
    std::vector<float> histSpeed(MAX_WAKE_POINTS, 0.0f);
    std::vector<float> histTime(MAX_WAKE_POINTS, 0.0f);
    int histCount = 0;
    double lastRecordTime = 0.0;

    // --- Fizyka Statku: Heave, Roll, Pitch ---
    float heavePos = 0.0f, heaveVel = 0.0f;
    float roll = 0.0f, rollVel = 0.0f;
    float pitch = 0.0f, pitchVel = 0.0f;
    float boatMass = 3.0f;
    float heaveStiffness = 0.5f;
    float heaveDamping = 1.0f;
    float rollStiffness = 1.5f;
    float rollDamping = 0.4f;
    float pitchStiffness = 2.5f;
    float pitchDamping = 2.0f;

    // --- Zmienne kamery (obrót myszą) ---
    float cameraAzimuth = 180.0f;   // stopnie (za łodzią)
    float cameraElevation = 30.0f;    // stopnie
    float cameraDistance = 12.0f;
    const float cameraSensitivity = 0.3f;   // stopnie/piksel
    bool  isDragging = false;
    bool  firstDrag = true;
    double lastMouseX, lastMouseY;

    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        float dt = (float)(currentTime - lastTime);
        lastTime = currentTime;
        if (dt <= 0.0f) dt = 0.001f;
        if (dt > dtLimit) dt = dtLimit;
        float t = (float)currentTime;

        // --- Obsługa myszy do obracania kamery (przytrzymany LMB) ---
        glfwPollEvents();
        double mouseX, mouseY;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        bool lmbPressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

        if (lmbPressed) {
            if (!isDragging) {
                isDragging = true;
                firstDrag = true;         // unikamy skoku przy pierwszym kliknięciu
            }
            if (firstDrag) {
                lastMouseX = mouseX;
                lastMouseY = mouseY;
                firstDrag = false;
            }
            else {
                double dx = mouseX - lastMouseX;
                double dy = mouseY - lastMouseY;
                cameraAzimuth -= (float)dx * cameraSensitivity;
                cameraElevation -= (float)dy * cameraSensitivity;
                // Ograniczenie kąta elewacji (aby nie przechodzić przez biegun)
                cameraElevation = glm::clamp(cameraElevation, -89.0f, 89.0f);
                lastMouseX = mouseX;
                lastMouseY = mouseY;
            }
        }
        else {
            isDragging = false;
        }

        // --- Klawisze ESC i parametry graficzne ---
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);
        if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) distortionStrength += 0.01f;
        if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) distortionStrength -= 0.01f;
        if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) fresnelPower += 0.1f;
        if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS) fresnelPower -= 0.1f;
        distortionStrength = glm::clamp(distortionStrength, 0.01f, 0.1f);
        fresnelPower = glm::clamp(fresnelPower, 1.0f, 10.0f);

        // --- Przełącznik Fal (Klawisz O) ---
        if (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS && currentTime - lastWaveToggleTime > 0.1) {
            enableDefaultWaves = !enableDefaultWaves;
            lastWaveToggleTime = currentTime;
            waveTransition = enableDefaultWaves ? 0.0f : 1.0f;
        }
        if (enableDefaultWaves && waveTransition < 1.0f) {
            waveTransition += (float)(dt / waveTransitionDuration);
            if (waveTransition > 1.0f) waveTransition = 1.0f;
        }
        else if (!enableDefaultWaves && waveTransition > 0.0f) {
            waveTransition -= (float)(dt / waveTransitionDuration);
            if (waveTransition < 0.0f) waveTransition = 0.0f;
        }

        // --- Kontrola Zanurzenia Statku ([ i ]) ---
        if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS) boatDraft += 0.02f;
        if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS) boatDraft -= 0.02f;
        boatDraft = glm::clamp(boatDraft, -0.5f, 1.0f);

        // --- Kontrola Fal Ślądu Statku ---
        if (glfwGetKey(window, GLFW_KEY_5) == GLFW_PRESS) wakeBaseAmplitude += 0.001f;
        if (glfwGetKey(window, GLFW_KEY_6) == GLFW_PRESS) wakeBaseAmplitude -= 0.001f;
        wakeBaseAmplitude = glm::clamp(wakeBaseAmplitude, 0.0f, 0.1f);
        if (glfwGetKey(window, GLFW_KEY_7) == GLFW_PRESS) waveNumber += 0.2f;
        if (glfwGetKey(window, GLFW_KEY_8) == GLFW_PRESS) waveNumber -= 0.2f;
        waveNumber = glm::clamp(waveNumber, 1.0f, 30.0f);
        if (glfwGetKey(window, GLFW_KEY_9) == GLFW_PRESS) wakeSpreadFactor += 0.01f;
        if (glfwGetKey(window, GLFW_KEY_0) == GLFW_PRESS) wakeSpreadFactor -= 0.01f;
        wakeSpreadFactor = glm::clamp(wakeSpreadFactor, 0.0f, 1.0f);
        if (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS) waveOmega += 0.05f;
        if (glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS) waveOmega -= 0.05f;
        waveOmega = glm::clamp(waveOmega, 0.5f, 10.0f);

        // --- Kontrola Parametrów Fizyki Statku (U/I, Y/P) ---
        if (glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS) heaveStiffness += 0.05f;
        if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS) heaveStiffness -= 0.05f;
        heaveStiffness = glm::clamp(heaveStiffness, 0.1f, 10.0f);
        if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS) heaveDamping += 0.02f;
        if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS) heaveDamping -= 0.02f;
        heaveDamping = glm::clamp(heaveDamping, 0.0f, 1.0f);

        // --- Sterowanie łodzią ---
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) boatSpeed += accel * dt;
        else if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) boatSpeed -= accel * dt;
        else {
            if (boatSpeed > 0) boatSpeed -= brakeF * dt * 0.5f;
            else if (boatSpeed < 0) boatSpeed += brakeF * dt * 0.5f;
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) rudder -= rudderSpd * dt;
        else if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) rudder += rudderSpd * dt;
        else {
            if (rudder > 0) rudder -= rudderDecay * dt;
            else if (rudder < 0) rudder += rudderDecay * dt;
        }

        boatSpeed *= (1.0f - 0.02f * dt);
        boatSpeed = glm::clamp(boatSpeed, -maxSpeed * 0.5f, maxSpeed);
        rudder = glm::clamp(rudder, -1.0f, 1.0f);

        boatYaw += rudder * boatSpeed * turnSpeed * dt * 0.5f;
        glm::vec3 forwardVec(sinf(boatYaw), 0.0f, cosf(boatYaw));
        glm::vec3 rightVec(cosf(boatYaw), 0.0f, -sinf(boatYaw));
        boatPos += forwardVec * boatSpeed * dt;

        // --- FIZYKA STATKU ---
        updateBoatPhysics(dt, boatPos.x, boatPos.z, t,
            heavePos, heaveVel,
            roll, rollVel,
            pitch, pitchVel,
            boatMass, heaveStiffness, heaveDamping,
            rollStiffness, rollDamping,
            pitchStiffness, pitchDamping,
            enableDefaultWaves);

        float h = getWaterHeight(boatPos.x, boatPos.z, t, enableDefaultWaves);
        float hL_w = getWaterHeight(boatPos.x - eps, boatPos.z, t, enableDefaultWaves);
        float hR_w = getWaterHeight(boatPos.x + eps, boatPos.z, t, enableDefaultWaves);
        float hF = getWaterHeight(boatPos.x, boatPos.z + eps, t, enableDefaultWaves);
        float hB = getWaterHeight(boatPos.x, boatPos.z - eps, t, enableDefaultWaves);
        glm::vec3 waveNormal = glm::normalize(glm::vec3(-(hR_w - hL_w) / (2 * eps), 1.0f, -(hF - hB) / (2 * eps)));
        float boatY = h + boatDraft + heavePos;
        glm::quat yawQuat = glm::angleAxis(boatYaw, glm::vec3(0, 1, 0));
        glm::quat rollQuat = glm::angleAxis(roll, glm::vec3(0, 0, 1));
        glm::quat pitchQuat = glm::angleAxis(pitch, glm::vec3(1, 0, 0));
        glm::quat boatOrientation = yawQuat * pitchQuat * rollQuat;
        wheelAngle += boatSpeed * 3.0f * dt;

        glm::vec3 boatWorldPos(boatPos.x, boatY, boatPos.z);
        glm::vec3 wheelPosR = boatWorldPos + rightVec * 0.65f;
        glm::vec3 wheelPosL = boatWorldPos - rightVec * 0.65f;
        wheelPosR.y = getWaterHeight(wheelPosR.x, wheelPosR.z, t, enableDefaultWaves);
        wheelPosL.y = getWaterHeight(wheelPosL.x, wheelPosL.z, t, enableDefaultWaves);

        // --- BUFOR HISTORII ŚLADU ---
        if (currentTime - lastRecordTime >= 0.05) {
            int count = std::min(histCount + 1, MAX_WAKE_POINTS);
            for (int i = count - 1; i > 0; --i) {
                histPosR[i * 3 + 0] = histPosR[(i - 1) * 3 + 0];
                histPosR[i * 3 + 1] = histPosR[(i - 1) * 3 + 1];
                histPosR[i * 3 + 2] = histPosR[(i - 1) * 3 + 2];
                histPosL[i * 3 + 0] = histPosL[(i - 1) * 3 + 0];
                histPosL[i * 3 + 1] = histPosL[(i - 1) * 3 + 1];
                histPosL[i * 3 + 2] = histPosL[(i - 1) * 3 + 2];
                histSpeed[i] = histSpeed[i - 1];
                histTime[i] = histTime[i - 1];
            }
            histPosR[0] = wheelPosR.x; histPosR[1] = wheelPosR.y; histPosR[2] = wheelPosR.z;
            histPosL[0] = wheelPosL.x; histPosL[1] = wheelPosL.y; histPosL[2] = wheelPosL.z;
            histSpeed[0] = boatSpeed;
            histTime[0] = t;
            histCount = count;
            lastRecordTime = currentTime;
        }

        // --- Kamera (orbita wokół łodzi) ---
        float azimuthRad = glm::radians(cameraAzimuth);
        float elevationRad = glm::radians(cameraElevation);
        glm::vec3 camDir(
            cos(elevationRad) * sin(azimuthRad),
            sin(elevationRad),
            cos(elevationRad) * cos(azimuthRad)
        );
        glm::vec3 cameraTarget = boatWorldPos + glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 cameraPos = cameraTarget + camDir * cameraDistance;

        glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1280.0f / 720.0f, 0.1f, 100.0f);
        glm::mat4 view = glm::lookAt(cameraPos, cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));

        // --- Odbicie kamery dla FBO ---
        glm::vec3 reflCamPos = glm::vec3(cameraPos.x, -cameraPos.y, cameraPos.z);
        glm::mat4 reflView = glm::lookAt(reflCamPos, cameraTarget, glm::vec3(0.0f, -1.0f, 0.0f));

        // ====================================================
        // 1. Reflection FBO
        // ====================================================
        reflectionFBO.bind();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(skyboxProgram);
        glUniformMatrix4fv(sProjection, 1, GL_FALSE, glm::value_ptr(projection));

        // FIX: Strip translation out of the reflection view matrix
        glm::mat4 reflSkyboxView = glm::mat4(glm::mat3(reflView));
        glUniformMatrix4fv(sView, 1, GL_FALSE, glm::value_ptr(reflSkyboxView));

        glUniform1f(sTime, t);
        glUniform3fv(sSunDir, 1, glm::value_ptr(sunDir));
        glUniform1i(sSunGlow, 1); // Enable the sun glow feature

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
        glUniform1i(glGetUniformLocation(skyboxProgram, "skybox"), 0);

        glDepthFunc(GL_LEQUAL); // FIX: Allow fragments with a depth of 1.0 to pass
        glBindVertexArray(skyboxVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glDepthFunc(GL_LESS);  // Reset back to default

        glEnable(GL_CLIP_DISTANCE0);
        glUseProgram(boatProgram);
        glUniformMatrix4fv(bProjection, 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(bView, 1, GL_FALSE, glm::value_ptr(reflView));
        glUniform3f(bViewPos, reflCamPos.x, reflCamPos.y, reflCamPos.z);
        glm::mat4 reflModel = glm::translate(glm::mat4(1.0f), boatWorldPos);
        reflModel = reflModel * glm::mat4_cast(boatOrientation);
        reflModel = glm::scale(reflModel, glm::vec3(1, -1, 1));
        glUniformMatrix4fv(bModel, 1, GL_FALSE, glm::value_ptr(reflModel));
        glUniform3f(bColor, 0.4f, 0.25f, 0.15f);
        glBindVertexArray(hullVAO); glDrawArrays(GL_TRIANGLES, 0, hullVertexCount / 3);
        reflectionFBO.unbind();
        glDisable(GL_CLIP_DISTANCE0);

        // ====================================================
        // 2. Refraction FBO
        // ====================================================
        refractionFBO.bind();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(skyboxProgram);
        glUniformMatrix4fv(sProjection, 1, GL_FALSE, glm::value_ptr(projection));

        // FIX: Strip translation out of the standard view matrix
        glm::mat4 refrSkyboxView = glm::mat4(glm::mat3(view));
        glUniformMatrix4fv(sView, 1, GL_FALSE, glm::value_ptr(refrSkyboxView));

        glUniform1f(sTime, t);
        glUniform3fv(sSunDir, 1, glm::value_ptr(sunDir));
        glUniform1i(sSunGlow, 1);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
        glUniform1i(glGetUniformLocation(skyboxProgram, "skybox"), 0);

        glDepthFunc(GL_LEQUAL); // FIX
        glBindVertexArray(skyboxVAO); glDrawArrays(GL_TRIANGLES, 0, 36);
        glDepthFunc(GL_LESS);  // FIX
        refractionFBO.unbind();

        // ====================================================
        // 3. Główne renderowanie
        // ====================================================
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, 1280, 720);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(skyboxProgram);
        glUniformMatrix4fv(sProjection, 1, GL_FALSE, glm::value_ptr(projection));

        // FIX: Strip translation out of the standard view matrix
        glm::mat4 mainSkyboxView = glm::mat4(glm::mat3(view));
        glUniformMatrix4fv(sView, 1, GL_FALSE, glm::value_ptr(mainSkyboxView));

        glUniform1f(sTime, t);
        glUniform3fv(sSunDir, 1, glm::value_ptr(sunDir));
        glUniform1i(sSunGlow, 1);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
        glUniform1i(glGetUniformLocation(skyboxProgram, "skybox"), 0);

        glDepthFunc(GL_LEQUAL); // FIX
        glBindVertexArray(skyboxVAO); glDrawArrays(GL_TRIANGLES, 0, 36);
        glDepthFunc(GL_LESS);  // FIX

        // Łódka
        glUseProgram(boatProgram);
        glUniformMatrix4fv(bProjection, 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(bView, 1, GL_FALSE, glm::value_ptr(view));
        glUniform3f(bViewPos, cameraPos.x, cameraPos.y, cameraPos.z);
        glm::mat4 baseModel = glm::translate(glm::mat4(1.0f), boatWorldPos);
        baseModel = baseModel * glm::mat4_cast(boatOrientation);
        glUniformMatrix4fv(bModel, 1, GL_FALSE, glm::value_ptr(baseModel));

        glUniform3f(bColor, 0.4f, 0.25f, 0.15f);   glBindVertexArray(hullVAO);       glDrawArrays(GL_TRIANGLES, 0, hullVertexCount / 3);
        glUniform3f(bColor, 0.6f, 0.45f, 0.3f);    glBindVertexArray(deckVAO);       glDrawArrays(GL_TRIANGLES, 0, deckVertexCount / 3);
        glUniform3f(bColor, 0.85f, 0.85f, 0.85f);  glBindVertexArray(cabinVAO);      glDrawArrays(GL_TRIANGLES, 0, cabinVertexCount / 3);
        glBindVertexArray(wheelhouseVAO); glDrawArrays(GL_TRIANGLES, 0, wheelhouseVertexCount / 3);
        glUniform3f(bColor, 0.15f, 0.15f, 0.15f);
        glUniformMatrix4fv(bModel, 1, GL_FALSE, glm::value_ptr(glm::translate(baseModel, glm::vec3(0, 0, -1.0f))));
        glBindVertexArray(funnelVAO); glDrawArrays(GL_TRIANGLES, 0, funnelVertexCount / 3);
        glUniformMatrix4fv(bModel, 1, GL_FALSE, glm::value_ptr(glm::translate(baseModel, glm::vec3(0, 0, -0.6f))));
        glDrawArrays(GL_TRIANGLES, 0, funnelVertexCount / 3);

        auto drawWheel = [&](glm::vec3 center, float angle,
            unsigned int hubVAO, size_t hubCnt,
            unsigned int paddlesVAO, size_t paddlesCnt)
            {
                glm::mat4 wm = baseModel
                    * glm::translate(glm::mat4(1), center)
                    * glm::rotate(glm::mat4(1), angle, glm::vec3(1, 0, 0))
                    * glm::translate(glm::mat4(1), -center);
                glUniformMatrix4fv(bModel, 1, GL_FALSE, glm::value_ptr(wm));
                glUniform3f(bColor, 0.18f, 0.18f, 0.18f); glBindVertexArray(hubVAO);     glDrawArrays(GL_TRIANGLES, 0, hubCnt / 3);
                glUniform3f(bColor, 0.85f, 0.30f, 0.15f); glBindVertexArray(paddlesVAO); glDrawArrays(GL_TRIANGLES, 0, paddlesCnt / 3);
            };
        drawWheel(glm::vec3(0.65f, 0.1f, 0), wheelAngle, wheelHubR_VAO, wheelHubRightVertexCount, wheelPaddlesR_VAO, wheelPaddlesRightVertexCount);
        drawWheel(glm::vec3(-0.65f, 0.1f, 0), wheelAngle, wheelHubL_VAO, wheelHubLeftVertexCount, wheelPaddlesL_VAO, wheelPaddlesLeftVertexCount);

        // Woda
        glUseProgram(waterProgram);
        glUniformMatrix4fv(wProjection, 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(wView, 1, GL_FALSE, glm::value_ptr(view));
        glUniform1f(wTime, t);
        glUniform1i(wIsWater, 1);
        glUniform3f(wViewPos, cameraPos.x, cameraPos.y, cameraPos.z);
        glUniform1f(wDistStr, distortionStrength);
        glUniform1f(wTexTile, textureTiling);
        glUniform1f(wSpecPow, specularPower);
        glUniform1f(wFresPow, fresnelPower);
        glUniform1f(wWaveTransition, waveTransition);

        if (histCount > 0) {
            glUniform3fv(wHistPosR, histCount, histPosR.data());
            glUniform3fv(wHistPosL, histCount, histPosL.data());
            glUniform1fv(wHistSpeed, histCount, histSpeed.data());
            glUniform1fv(wHistTime, histCount, histTime.data());
            glUniform1i(wHistCount, histCount);
        }

        glUniform1f(wWakeBaseAmplitude, wakeBaseAmplitude);
        glUniform1f(wWaveNumber, waveNumber);
        glUniform1f(wWaveOmega, waveOmega);
        glUniform1f(wWakeSpreadFactor, wakeSpreadFactor);

        glActiveTexture(GL_TEXTURE0);  glBindTexture(GL_TEXTURE_2D, reflectionFBO.texture);
        glUniform1i(glGetUniformLocation(waterProgram, "reflectionTexture"), 0);
        glActiveTexture(GL_TEXTURE1);  glBindTexture(GL_TEXTURE_2D, refractionFBO.texture);
        glUniform1i(glGetUniformLocation(waterProgram, "refractionTexture"), 1);
        glActiveTexture(GL_TEXTURE2);  glBindTexture(GL_TEXTURE_2D, dudvTexture);
        glUniform1i(glGetUniformLocation(waterProgram, "dudvMap"), 2);
        glActiveTexture(GL_TEXTURE3);  glBindTexture(GL_TEXTURE_2D, normalTexture);
        glUniform1i(glGetUniformLocation(waterProgram, "normalMap"), 3);
        glActiveTexture(GL_TEXTURE4);  glBindTexture(GL_TEXTURE_2D, foamTexture);
        glUniform1i(glGetUniformLocation(waterProgram, "foamTexture"), 4);

        glm::mat4 waterModel = glm::mat4(1.0f);
        glUniformMatrix4fv(wModel, 1, GL_FALSE, glm::value_ptr(waterModel));
        glDepthMask(GL_FALSE);
        glBindVertexArray(waterVAO);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(waterVertices.size() / 3));
        glDepthMask(GL_TRUE);

        // --- HUD: tytuł okna ---
        static float hudUpdateTimer = 0.0f;
        hudUpdateTimer += dt;
        if (hudUpdateTimer >= 0.1f) {
            hudUpdateTimer = 0.0f;
            char hudText[512];
            snprintf(hudText, sizeof(hudText),
                "Speed: %.2f | Pos: (%.1f, %.1f) | Yaw: %.1f° | Heave: %.3f | Roll: %.3f | Pitch: %.3f | Waves: %s",
                boatSpeed, boatPos.x, boatPos.z, glm::degrees(boatYaw), heavePos, roll, pitch,
                enableDefaultWaves ? "ON" : "OFF");
            glfwSetWindowTitle(window, hudText);
        }

        glfwSwapBuffers(window);
    }

    reflectionFBO.cleanup(); refractionFBO.cleanup();
    glDeleteTextures(1, &dudvTexture); glDeleteTextures(1, &normalTexture);
    glDeleteTextures(1, &foamTexture);
    glDeleteVertexArrays(1, &skyboxVAO); glDeleteVertexArrays(1, &waterVAO);
    glDeleteVertexArrays(1, &hullVAO); glDeleteVertexArrays(1, &deckVAO);
    glDeleteVertexArrays(1, &cabinVAO); glDeleteVertexArrays(1, &wheelhouseVAO);
    glDeleteVertexArrays(1, &funnelVAO);
    glDeleteVertexArrays(1, &wheelHubR_VAO); glDeleteVertexArrays(1, &wheelPaddlesR_VAO);
    glDeleteVertexArrays(1, &wheelHubL_VAO); glDeleteVertexArrays(1, &wheelPaddlesL_VAO);
    glDeleteProgram(waterProgram); glDeleteProgram(skyboxProgram); glDeleteProgram(boatProgram);
    glfwTerminate();
    return 0;
}