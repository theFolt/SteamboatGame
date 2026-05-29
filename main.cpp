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

// --- Struktura FBO ---
struct WaterFBO {
    unsigned int fbo, texture, depthBuffer;
    int width, height;
    void init(int w, int h) {
        width = w; height = h;
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

unsigned int generateDUDVTexture() {
    unsigned int tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    const int size = 512; std::vector<unsigned char> data(size * size * 3);
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
        float nx = (float)x / size * 2 - 1, ny = (float)y / size * 2 - 1;
        float r1 = sqrtf(nx * nx + ny * ny), r2 = sqrtf((nx - .5f) * (nx - .5f) + (ny + .3f) * (ny + .3f));
        int idx = (y * size + x) * 3;
        data[idx] = (unsigned char)((sinf(r1 * 15) * cosf(r1 * 8) * .5f + .5f) * 255);
        data[idx + 1] = (unsigned char)((cosf(r2 * 12) * sinf(r2 * 10) * .5f + .5f) * 255);
        data[idx + 2] = 255;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D); return tex;
}

unsigned int generateNormalTexture() {
    unsigned int tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    const int size = 512; std::vector<unsigned char> data(size * size * 3);
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
        float nx = (float)x / size * 2 - 1, ny = (float)y / size * 2 - 1;
        float r1 = sqrtf(nx * nx + ny * ny), r2 = sqrtf((nx + .2f) * (nx + .2f) + (ny - .4f) * (ny - .4f));
        float fnx = (sinf(r1 * 20) * .5f + .5f + sinf(r2 * 15) * .3f + .5f) * .5f;
        float fny = (cosf(r1 * 20) * .5f + .5f + cosf(r2 * 15) * .3f + .5f) * .5f;
        float nz = sqrtf(1.0f - glm::min(fnx * fnx + fny * fny, 1.0f));
        int idx = (y * size + x) * 3;
        data[idx] = (unsigned char)(fnx * 255); data[idx + 1] = (unsigned char)(fny * 255); data[idx + 2] = (unsigned char)(nz * 255);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D); return tex;
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
    GLint sTime = glGetUniformLocation(skyboxProgram, "u_time");

    WaterFBO reflectionFBO, refractionFBO;
    reflectionFBO.init(1280, 720);
    refractionFBO.init(1280, 720);

    unsigned int dudvTexture = generateDUDVTexture();
    unsigned int normalTexture = generateNormalTexture();

    float skyboxVertices[] = {
        -1, 1,-1, -1,-1,-1,  1,-1,-1,   1,-1,-1,  1, 1,-1, -1, 1,-1,
        -1,-1, 1, -1,-1,-1, -1, 1,-1,  -1, 1,-1, -1, 1, 1, -1,-1, 1,
         1,-1,-1,  1,-1, 1,  1, 1, 1,   1, 1, 1,  1, 1,-1,  1,-1,-1,
        -1,-1, 1, -1, 1, 1,  1, 1, 1,   1, 1, 1,  1,-1, 1, -1,-1, 1,
        -1, 1,-1,  1, 1,-1,  1, 1, 1,   1, 1, 1, -1, 1, 1, -1, 1,-1,
        -1,-1,-1, -1,-1, 1,  1,-1,-1,   1,-1,-1, -1,-1, 1,  1,-1, 1
    };
    unsigned int skyboxVAO = createVAO(skyboxVertices, sizeof(skyboxVertices) / sizeof(float));

    const float waterHalfSize = 40.0f;
    const int   segments = 600;
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
    float boatDraft = 0.7f;

    // --- Parametry Fal Ślądu Statku ---
    float wakeBaseAmplitude = 0.008f;
    float waveNumber = 12.0f;
    float waveOmega = 4.5f;
    float wakeSpreadFactor = 0.4f;

    // =========================================================
    // NOWE ZMIENNE: BUFOR HISTORII ŚLADU WODY
    // =========================================================
    const int MAX_WAKE_POINTS = 60;
    std::vector<float> histPosR(MAX_WAKE_POINTS * 3, 0.0f);
    std::vector<float> histPosL(MAX_WAKE_POINTS * 3, 0.0f);
    std::vector<float> histSpeed(MAX_WAKE_POINTS, 0.0f);
    std::vector<float> histTime(MAX_WAKE_POINTS, 0.0f);
    int histCount = 0;
    double lastRecordTime = 0.0;
    // =========================================================

    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        float dt = (float)(currentTime - lastTime);
        lastTime = currentTime;
        if (dt <= 0.0f) dt = 0.001f;
        if (dt > dtLimit) dt = dtLimit;
        float t = (float)currentTime;

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
        // Animacja przejścia fal
        if (enableDefaultWaves && waveTransition < 1.0f) {
            waveTransition += (float)(dt / waveTransitionDuration);
            if (waveTransition > 1.0f) waveTransition = 1.0f;
        } else if (!enableDefaultWaves && waveTransition > 0.0f) {
            waveTransition -= (float)(dt / waveTransitionDuration);
            if (waveTransition < 0.0f) waveTransition = 0.0f;
        }

        // --- Kontrola Zanurzenia Statku ([ i ]) ---
        if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS) boatDraft += 0.01f * 2.0f;
        if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS) boatDraft -= 0.01f * 2.0f;
        boatDraft = glm::clamp(boatDraft, 0.0f, 2.0f);

        // --- Kontrola Fal Ślądu Statku (5,6 = amplituda; 7,8 = częstotliwość; 9,0 = spread; -,= = omega) ---
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

        // Inercja i opór wody
        boatSpeed *= (1.0f - 0.08f * dt);
        boatSpeed = glm::clamp(boatSpeed, -maxSpeed * 0.5f, maxSpeed);
        rudder = glm::clamp(rudder, -1.0f, 1.0f);

        boatYaw += rudder * boatSpeed * turnSpeed * dt * 0.5f;
        glm::vec3 forwardVec(sinf(boatYaw), 0.0f, cosf(boatYaw));
        glm::vec3 rightVec(cosf(boatYaw), 0.0f, -sinf(boatYaw));
        boatPos += forwardVec * boatSpeed * dt;

        // Wysokość i orientacja łódki na falach
        float h = getWaterHeight(boatPos.x, boatPos.z, t, enableDefaultWaves);
        float hL_w = getWaterHeight(boatPos.x - eps, boatPos.z, t, enableDefaultWaves);
        float hR_w = getWaterHeight(boatPos.x + eps, boatPos.z, t, enableDefaultWaves);
        float hF = getWaterHeight(boatPos.x, boatPos.z + eps, t, enableDefaultWaves);
        float hB = getWaterHeight(boatPos.x, boatPos.z - eps, t, enableDefaultWaves);
        glm::vec3 waveNormal = glm::normalize(glm::vec3(-(hR_w - hL_w) / (2 * eps), 1.0f, -(hF - hB) / (2 * eps)));
        float boatY = h + boatDraft;
        glm::quat waveQuat = glm::quat(glm::vec3(0, 1, 0), waveNormal);
        glm::quat yawQuat = glm::angleAxis(boatYaw, glm::vec3(0, 1, 0));
        glm::quat boatOrientation = yawQuat * waveQuat;
        wheelAngle += boatSpeed * 3.0f * dt;

        // --- POZYCJE KÓŁ W PRZESTRZENI ŚWIATA ---
        glm::vec3 boatWorldPos(boatPos.x, boatY, boatPos.z);
        glm::vec3 wheelPosR = boatWorldPos + rightVec * 0.65f;
        glm::vec3 wheelPosL = boatWorldPos - rightVec * 0.65f;
        wheelPosR.y = getWaterHeight(wheelPosR.x, wheelPosR.z, t, enableDefaultWaves);
        wheelPosL.y = getWaterHeight(wheelPosL.x, wheelPosL.z, t, enableDefaultWaves);

        // =========================================================
        // ZAPISYWANIE DO BUFORA HISTORII CO 0.05 SEKUNDY
        // =========================================================
        if (currentTime - lastRecordTime >= 0.05) {
            int count = std::min(histCount + 1, MAX_WAKE_POINTS);
            // Przesuwamy stare wpisy w dół tablicy
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
            // Zapisujemy najnowszy wpis na początku
            histPosR[0] = wheelPosR.x; histPosR[1] = wheelPosR.y; histPosR[2] = wheelPosR.z;
            histPosL[0] = wheelPosL.x; histPosL[1] = wheelPosL.y; histPosL[2] = wheelPosL.z;
            histSpeed[0] = boatSpeed;
            histTime[0] = t;

            histCount = count;
            lastRecordTime = currentTime;
        }

        // Kamera
        glm::vec3 cameraOffset = -forwardVec * 10.0f + glm::vec3(0, 5, 0);
        glm::vec3 cameraPos = boatPos + glm::vec3(0, 1.5f, 0) + cameraOffset;
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1280.0f / 720.0f, 0.1f, 100.0f);
        glm::mat4 view = glm::lookAt(cameraPos, boatPos + glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));

        // ====================================================
        // 1. Reflection FBO
        // ====================================================
        reflectionFBO.bind();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        float dist = 2.0f * (cameraPos.y - 0.0f);
        glm::vec3 reflCamPos = cameraPos - glm::vec3(0, dist, 0);
        glm::mat4 reflView = glm::lookAt(reflCamPos, boatPos + glm::vec3(0, 1, 0), glm::vec3(0, -1, 0));

        glUseProgram(skyboxProgram);
        glUniformMatrix4fv(sProjection, 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(sView, 1, GL_FALSE, glm::value_ptr(reflView));
        glUniform1f(sTime, t);
        glBindVertexArray(skyboxVAO); glDrawArrays(GL_TRIANGLES, 0, 36);

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
        glUniformMatrix4fv(sView, 1, GL_FALSE, glm::value_ptr(view));
        glUniform1f(sTime, t);
        glBindVertexArray(skyboxVAO); glDrawArrays(GL_TRIANGLES, 0, 36);
        refractionFBO.unbind();

        // ====================================================
        // 3. Główne renderowanie
        // ====================================================
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, 1280, 720);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(skyboxProgram);
        glUniformMatrix4fv(sProjection, 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(sView, 1, GL_FALSE, glm::value_ptr(view));
        glUniform1f(sTime, t);
        glBindVertexArray(skyboxVAO); glDrawArrays(GL_TRIANGLES, 0, 36);

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

        // --- PRZEKAZANIE BUFORA HISTORII DO SHADERA ---
        if (histCount > 0) {
            glUniform3fv(wHistPosR, histCount, histPosR.data());
            glUniform3fv(wHistPosL, histCount, histPosL.data());
            glUniform1fv(wHistSpeed, histCount, histSpeed.data());
            glUniform1fv(wHistTime, histCount, histTime.data());
            glUniform1i(wHistCount, histCount);
        }

        // --- USTAWIENIE PARAMETRÓW FAL ŚLĄDU ---
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

        glm::mat4 waterModel = glm::mat4(1.0f);
        glUniformMatrix4fv(wModel, 1, GL_FALSE, glm::value_ptr(waterModel));
        glDepthMask(GL_FALSE);
        glBindVertexArray(waterVAO);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(waterVertices.size() / 3));
        glDepthMask(GL_TRUE);

        // --- HUD: Wyświetlanie danych w tytule okna ---
        static float hudUpdateTimer = 0.0f;
        hudUpdateTimer += dt;
        if (hudUpdateTimer >= 0.1f) {
            hudUpdateTimer = 0.0f;

            float wheelSpeedR = boatSpeed * 3.0f;
            float wheelSpeedL = boatSpeed * 3.0f;
            float yawDegrees = glm::degrees(boatYaw);

            char hudText[256];
            snprintf(hudText, sizeof(hudText), 
                "Speed: %.2f | Pos: (%.1f, %.1f) | Yaw: %.1f° | Wheel R: %.2f | Wheel L: %.2f | Waves: %s",
                boatSpeed, boatPos.x, boatPos.z, yawDegrees, wheelSpeedR, wheelSpeedL,
                enableDefaultWaves ? "ON" : "OFF");
            glfwSetWindowTitle(window, hudText);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    reflectionFBO.cleanup(); refractionFBO.cleanup();
    glDeleteTextures(1, &dudvTexture); glDeleteTextures(1, &normalTexture);
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