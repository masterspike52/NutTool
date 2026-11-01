#include "gl_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nut::gui {
namespace {

const float kOne[4] = {1, 1, 1, 1}, kZero[4] = {0, 0, 0, 0};

const char* kMeshVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
uniform mat4 uViewProj;
out vec3 vNormal;
out vec2 vUv;
out vec3 vPos;
void main() {
    vNormal = aNormal;
    vUv = aUv;
    vPos = aPos;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

const char* kMeshFs = R"(#version 330 core
in vec3 vNormal;
in vec2 vUv;
in vec3 vPos;
uniform sampler2D uTex;
uniform int uTextured;
uniform vec3 uLightDir[3];    // towards each light: key, fill, rim
uniform vec3 uLightColour[3];
uniform vec3 uSky;            // ambient from above
uniform vec3 uGround;         // ambient from below
uniform int uUnlit;
vec3 lighting(vec3 n) {
    if (uUnlit != 0) return vec3(1.0);
    vec3 c = mix(uGround, uSky, n.y * 0.5 + 0.5);
    for (int i = 0; i < 3; ++i) c += uLightColour[i] * max(dot(n, uLightDir[i]), 0.0);
    return c;
}
uniform vec3 uBase;
uniform vec4 uGain;
uniform vec4 uOffset;
uniform vec3 uEye;
uniform vec3 uSpec;
uniform float uPower;
out vec4 oColor;
void main() {
    vec3 n = vNormal;
    // Buffers without normals fall back to face normals.
    if (dot(n, n) < 0.01) n = cross(dFdx(vPos), dFdy(vPos));
    n = normalize(n);
    if (!gl_FrontFacing) n = -n;
    vec3 l = uLightDir[0];
    vec3 light = lighting(n);
    vec3 h = normalize(l + normalize(uEye - vPos));
    // Specular, toned down: the game adds it to lighting this viewer does not reproduce.
    vec3 spec = uUnlit != 0 ? vec3(0.0) : 0.35 * uSpec * uLightColour[0] * pow(max(dot(n, h), 0.0), max(uPower, 4.0)) * (dot(n, l) > 0.0 ? 1.0 : 0.0);
    vec4 c = vec4(uBase, 1.0);
    if (uTextured != 0) {
        vec4 t = texture(uTex, vUv);
        if (t.a < 0.25) discard;
        c = vec4(clamp(t.rgb * uGain.rgb + uOffset.rgb, 0.0, 1.0), 1.0);
    }
    oColor = vec4(c.rgb * light + spec, 1.0);
}
)";

// Fur shells: the mesh pushed out along its normals, one draw per volume layer.
const char* kFurVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec2 aUv2;
layout(location = 4) in vec2 aUv3;
uniform mat4 uViewProj;
uniform float uOffset;
uniform int uThinSet; // texture coordinate set of the thinning map
uniform int uVolSet;  // ... and of the fur volume
uniform float uVolScale; // repeats of the fur volume across its texture coordinates
out vec3 vNormal;
out vec2 vUv;
out vec2 vUvThin;
out vec2 vUv2;
void main() {
    vNormal = aNormal;
    vUv = aUv;
    vUvThin = uThinSet == 2 ? aUv3 : uThinSet == 1 ? aUv2 : aUv;
    vUv2 = (uVolSet == 2 ? aUv3 : uVolSet == 1 ? aUv2 : aUv) * uVolScale;
    vec3 n = dot(aNormal, aNormal) > 0.01 ? normalize(aNormal) : vec3(0.0);
    gl_Position = uViewProj * vec4(aPos + n * uOffset, 1.0);
}
)";

const char* kFurFs = R"(#version 330 core
in vec3 vNormal;
in vec2 vUv;
in vec2 vUvThin;
in vec2 vUv2;
uniform sampler2DArray uVolume;
uniform sampler2D uRoot;
uniform sampler2D uTip;
uniform sampler2D uThin;
uniform int uHasThin;
uniform int uNoise;
uniform float uLayer;   // volume layer of this shell
uniform float uHeight;  // 0 at the skin, 1 at the tips
uniform vec2 uVolSize;  // texels per layer
uniform vec3 uLightDir[3];    // towards each light: key, fill, rim
uniform vec3 uLightColour[3];
uniform vec3 uSky;            // ambient from above
uniform vec3 uGround;         // ambient from below
uniform int uUnlit;
vec3 lighting(vec3 n) {
    if (uUnlit != 0) return vec3(1.0);
    vec3 c = mix(uGround, uSky, n.y * 0.5 + 0.5);
    for (int i = 0; i < 3; ++i) c += uLightColour[i] * max(dot(n, uLightDir[i]), 0.0);
    return c;
}
uniform vec4 uGain;
uniform vec4 uOffsetColour;
out vec4 oColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void main() {
    float thin = uHasThin != 0 ? texture(uThin, vUvThin).r : 1.0;
    if (uHeight > thin) discard;
    float cover;
    if (uNoise != 0) {
        // Strand heights from per-texel noise, thinning out towards the tips.
        float strand = hash(floor(vUv2 * uVolSize));
        cover = strand > uHeight * 1.1 ? 1.0 : 0.0;
    } else {
        cover = texture(uVolume, vec3(vUv2, uLayer)).a;
    }
    if (cover < 0.35) discard;
    vec3 root = texture(uRoot, vUv).rgb, tip = texture(uTip, vUv).rgb;
    vec3 c = clamp(mix(root, tip, uHeight) * uGain.rgb + uOffsetColour.rgb, 0.0, 1.0);
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) n = -n;
    vec3 light = lighting(n) * mix(0.75, 1.0, uHeight);
    oColor = vec4(c * light, 1.0);
}
)";

const char* kLineVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uViewProj;
uniform float uPointSize;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPos, 1.0);
    gl_PointSize = uPointSize;
}
)";

const char* kLineFs = R"(#version 330 core
in vec4 vColor;
out vec4 oColor;
void main() { oColor = vColor; }
)";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        std::fprintf(stderr, "shader error: %s\n", log);
    }
    return s;
}

GLuint program(const char* vs, const char* fs) {
    GLuint p = glCreateProgram();
    GLuint a = compile(GL_VERTEX_SHADER, vs), b = compile(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, a);
    glAttachShader(p, b);
    glLinkProgram(p);
    glDeleteShader(a);
    glDeleteShader(b);
    return p;
}

// Column-major 4x4 helpers.
void mul(const float* a, const float* b, float* out) {
    float r[16];
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + rr] * b[c * 4 + k];
            r[c * 4 + rr] = s;
        }
    std::copy(r, r + 16, out);
}

void perspective(float fovy, float aspect, float zn, float zf, float* m) {
    float f = 1.0f / std::tan(fovy / 2);
    std::fill(m, m + 16, 0.0f);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1;
    m[14] = 2 * zf * zn / (zn - zf);
}

void lookAt(const float* eye, const float* at, float* m) {
    float f[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (float& x : f) x /= fl;
    float up[3] = {0, 1, 0};
    float s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
    float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    for (float& x : s) x /= sl;
    float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    float r[16] = {s[0], u[0], -f[0], 0, s[1], u[1], -f[1], 0, s[2], u[2], -f[2], 0, 0, 0, 0, 1};
    r[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    r[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    r[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
    std::copy(r, r + 16, m);
}

} // namespace

GlTexture::~GlTexture() {
    if (id_) glDeleteTextures(1, &id_);
}

void GlTexture::upload(const uint8_t* rgba, int w, int h, bool mipmaps, bool nearest) {
    if (!id_) glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : (nearest ? GL_NEAREST : GL_LINEAR));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    w_ = w;
    h_ = h;
}

GlTextureArray::~GlTextureArray() {
    if (id_) glDeleteTextures(1, &id_);
}

void GlTextureArray::upload(const uint8_t* rgba, int w, int h, int layers) {
    if (!id_) glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, w, h, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    layers_ = layers;
}

ModelView::ModelView() {
    prog_ = program(kMeshVs, kMeshFs);
    furProg_ = program(kFurVs, kFurFs);
    lineProg_ = program(kLineVs, kLineFs);
    glGenVertexArrays(1, &lineVao_);
    glGenBuffers(1, &lineVbo_);
}

ModelView::~ModelView() {
    release();
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (color_) glDeleteTextures(1, &color_);
    if (depth_) glDeleteRenderbuffers(1, &depth_);
    glDeleteVertexArrays(1, &lineVao_);
    glDeleteBuffers(1, &lineVbo_);
    glDeleteProgram(prog_);
    glDeleteProgram(furProg_);
    glDeleteProgram(lineProg_);
}

void ModelView::release() {
    for (auto& m : meshes_) {
        glDeleteVertexArrays(1, &m.vao);
        glDeleteBuffers(1, &m.vbo);
        glDeleteBuffers(1, &m.ibo);
    }
    meshes_.clear();
}

void ModelView::setModel(const model::Model& m) {
    release();
    bones_ = m.bones;
    boneWorld_.clear();
    skin_.clear();
    skinDirty_ = false;
    for (auto& mesh : m.meshes) {
        GpuMesh g;
        std::vector<float> v;
        v.reserve(mesh.vertices.size() * 12);
        for (auto& x : mesh.vertices)
            v.insert(v.end(), {x.pos[0], x.pos[1], x.pos[2], x.normal[0], x.normal[1], x.normal[2], x.uv[0], x.uv[1], x.uv2[0], x.uv2[1], x.uv3[0], x.uv3[1]});
        // Skinned meshes keep their bind-pose vertices to re-skin from.
        if (mesh.layout.skinned) {
            g.bind = v;
            for (auto& x : mesh.vertices)
                for (int k = 0; k < 4; ++k) {
                    g.boneIds.push_back(x.bones[k]);
                    g.weights.push_back(x.weights[k]);
                }
        }
        glGenVertexArrays(1, &g.vao);
        glGenBuffers(1, &g.vbo);
        glGenBuffers(1, &g.ibo);
        glBindVertexArray(g.vao);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(float)), v.data(), g.bind.empty() ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(mesh.indices.size() * 4), mesh.indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 48, reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 48, reinterpret_cast<void*>(12));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 48, reinterpret_cast<void*>(24));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 48, reinterpret_cast<void*>(32));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, 48, reinterpret_cast<void*>(40));
        glBindVertexArray(0);
        g.count = GLsizei(mesh.indices.size());
        g.uvSets = int(mesh.layout.uvSets.size());
        std::copy(mesh.specular, mesh.specular + 3, g.spec);
        g.power = mesh.specularPower;
        meshes_.push_back(g);
    }
    for (int k = 0; k < 3; ++k) center_[k] = (m.boundsMin[k] + m.boundsMax[k]) / 2;
    float dx = m.boundsMax[0] - m.boundsMin[0], dy = m.boundsMax[1] - m.boundsMin[1], dz = m.boundsMax[2] - m.boundsMin[2];
    radius_ = std::max(0.01f, std::sqrt(dx * dx + dy * dy + dz * dz) / 2);
    frame();
}

void ModelView::setMeshTexture(size_t i, const GlTexture* t) { if (i < meshes_.size()) meshes_[i].tex = t; }
void ModelView::setMeshVisible(size_t i, bool v) {
    if (i >= meshes_.size()) return;
    if (v && !meshes_[i].visible) meshes_[i].skinnedVersion = -1; // re-skin when it shows again
    meshes_[i].visible = v;
}

void ModelView::setPose(const std::vector<float>* skinMatrices, const std::vector<float>* boneWorld) {
    if (skinMatrices) skin_ = *skinMatrices;
    else skin_.clear();
    if (boneWorld) boneWorld_ = *boneWorld;
    else boneWorld_.clear();
    ++poseVersion_;
}

// Linear blend skinning on the CPU: positions and normals of visible skinned meshes are rebuilt
// from their bind-pose copies with the bone matrices (16 floats each, column-major).
void ModelView::skinMeshes() {
    for (auto& g : meshes_) {
        if (!g.visible || g.bind.empty() || g.skinnedVersion == poseVersion_) continue;
        g.skinnedVersion = poseVersion_;
        if (skin_.empty()) {
            glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(g.bind.size() * sizeof(float)), g.bind.data());
            continue;
        }
        std::vector<float> out = g.bind;
        size_t nBones = skin_.size() / 16, nv = g.bind.size() / 12;
        for (size_t i = 0; i < nv; ++i) {
            const float* src = &g.bind[i * 12];
            float* dst = &out[i * 12];
            float p[3] = {}, n[3] = {}, total = 0;
            for (int k = 0; k < 4; ++k) {
                float w = g.weights[i * 4 + size_t(k)];
                size_t b = g.boneIds[i * 4 + size_t(k)];
                if (w <= 0 || b >= nBones) continue;
                const float* M = &skin_[b * 16];
                total += w;
                for (int r = 0; r < 3; ++r) {
                    p[r] += w * (M[r] * src[0] + M[4 + r] * src[1] + M[8 + r] * src[2] + M[12 + r]);
                    n[r] += w * (M[r] * src[3] + M[4 + r] * src[4] + M[8 + r] * src[5]);
                }
            }
            if (total <= 0) continue;
            float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            for (int r = 0; r < 3; ++r) {
                dst[r] = p[r] / total;
                dst[3 + r] = nl > 0 ? n[r] / nl : src[3 + r];
            }
        }
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(out.size() * sizeof(float)), out.data());
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}
void ModelView::setMeshColour(size_t i, const float* rgb) { if (i < meshes_.size()) std::copy(rgb, rgb + 3, meshes_[i].colour); }
void ModelView::setMeshFur(size_t i, const Fur& fur) { if (i < meshes_.size()) meshes_[i].fur = fur; }
void ModelView::setMeshTint(size_t i, const float* gain, const float* offset) {
    if (i >= meshes_.size()) return;
    std::copy(gain, gain + 4, meshes_[i].gain);
    std::copy(offset, offset + 4, meshes_[i].offset);
}

// Light rig: a key light, a weaker fill from the other side and a rim light from behind, either
// fixed relative to the camera or placed in the world by azimuth/elevation.
ModelView::Lights ModelView::makeLights(const Options& opt) const {
    Lights L;
    L.unlit = opt.lighting == 2;
    auto norm = [](float* v) {
        float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l > 0) for (int k = 0; k < 3; ++k) v[k] /= l;
    };
    auto set = [&](int i, float x, float y, float z, float intensity) {
        L.dir[i][0] = x; L.dir[i][1] = y; L.dir[i][2] = z;
        norm(L.dir[i]);
        for (int k = 0; k < 3; ++k) L.colour[i][k] = opt.lightColour[k] * intensity * opt.brightness;
    };
    // Camera basis: towards the viewer, right and up.
    float back[3] = {std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_)};
    float right[3] = {std::cos(yaw_), 0, -std::sin(yaw_)};
    float up[3] = {back[1] * right[2] - back[2] * right[1], back[2] * right[0] - back[0] * right[2], back[0] * right[1] - back[1] * right[0]};
    if (opt.lighting == 1) {
        // Headlight: a single light at the camera.
        set(0, back[0], back[1], back[2], 0.85f);
        set(1, 0, 1, 0, 0);
        set(2, 0, 1, 0, 0);
    } else if (opt.lightFollowsCamera) {
        auto mixDir = [&](int i, float b, float r, float u, float intensity) {
            set(i, back[0] * b + right[0] * r + up[0] * u, back[1] * b + right[1] * r + up[1] * u, back[2] * b + right[2] * r + up[2] * u, intensity);
        };
        mixDir(0, 1.0f, -0.6f, 0.8f, 0.85f);  // key: front, upper left
        mixDir(1, 0.8f, 0.9f, -0.2f, 0.35f);  // fill: front right, a little low
        mixDir(2, -1.0f, 0.2f, 0.7f, 0.45f);  // rim: behind, above
    } else {
        float az = opt.lightAzimuth * 3.14159265f / 180, el = opt.lightElevation * 3.14159265f / 180;
        set(0, std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az), 0.85f);
        set(1, std::cos(0.1f) * std::sin(az + 2.2f), std::sin(0.1f), std::cos(0.1f) * std::cos(az + 2.2f), 0.35f);
        set(2, std::cos(0.6f) * std::sin(az + 3.14159265f), std::sin(0.6f), std::cos(0.6f) * std::cos(az + 3.14159265f), 0.45f);
    }
    float a = L.unlit ? 0 : opt.ambient;
    for (int k = 0; k < 3; ++k) {
        L.sky[k] = a * (k == 2 ? 1.05f : 1.0f);
        L.ground[k] = a * 0.45f;
    }
    return L;
}

void ModelView::setLights(GLuint prog, const Lights& L) const {
    glUniform3fv(glGetUniformLocation(prog, "uLightDir"), 3, &L.dir[0][0]);
    glUniform3fv(glGetUniformLocation(prog, "uLightColour"), 3, &L.colour[0][0]);
    glUniform3fv(glGetUniformLocation(prog, "uSky"), 1, L.sky);
    glUniform3fv(glGetUniformLocation(prog, "uGround"), 1, L.ground);
    glUniform1i(glGetUniformLocation(prog, "uUnlit"), L.unlit ? 1 : 0);
}

void ModelView::frame() {
    std::copy(center_, center_ + 3, target_);
    distance_ = radius_ * 2.6f;
    yaw_ = 0.6f;
    pitch_ = 0.3f;
}

void ModelView::ensureTarget(int w, int h) {
    if (fbo_ && w == fw_ && h == fh_) return;
    if (!fbo_) glGenFramebuffers(1, &fbo_);
    if (!color_) glGenTextures(1, &color_);
    if (!depth_) glGenRenderbuffers(1, &depth_);
    glBindTexture(GL_TEXTURE_2D, color_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    fw_ = w;
    fh_ = h;
}

void ModelView::viewProj(float* out, int w, int h) const {
    float eye[3] = {target_[0] + distance_ * std::cos(pitch_) * std::sin(yaw_), target_[1] + distance_ * std::sin(pitch_),
                    target_[2] + distance_ * std::cos(pitch_) * std::cos(yaw_)};
    float view[16], proj[16];
    lookAt(eye, target_, view);
    perspective(0.8f, float(w) / float(std::max(h, 1)), std::max(radius_ * 0.005f, 0.001f), radius_ * 200 + distance_, proj);
    mul(proj, view, out);
}

void ModelView::render(int w, int h, const Options& opt) {
    ensureTarget(w, h);
    skinMeshes();
    GLint prevFbo = 0, prevViewport[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w, h);
    glClearColor(0.16f, 0.17f, 0.20f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    if (opt.backfaces) glDisable(GL_CULL_FACE);
    else { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CW); }
    float vp[16];
    viewProj(vp, w, h);

    glUseProgram(prog_);
    glUniformMatrix4fv(glGetUniformLocation(prog_, "uViewProj"), 1, GL_FALSE, vp);
    float eyePos[3] = {target_[0] + distance_ * std::cos(pitch_) * std::sin(yaw_), target_[1] + distance_ * std::sin(pitch_),
                       target_[2] + distance_ * std::cos(pitch_) * std::cos(yaw_)};
    glUniform3fv(glGetUniformLocation(prog_, "uEye"), 1, eyePos);
    Lights lights = makeLights(opt);
    setLights(prog_, lights);
    glUniform1i(glGetUniformLocation(prog_, "uTex"), 0);
    glPolygonMode(GL_FRONT_AND_BACK, opt.wireframe ? GL_LINE : GL_FILL);
    for (auto& m : meshes_) {
        if (!m.visible || m.count == 0) continue;
        bool tex = opt.textured && m.tex && m.tex->id();
        glUniform1i(glGetUniformLocation(prog_, "uTextured"), tex ? 1 : 0);
        glUniform3fv(glGetUniformLocation(prog_, "uBase"), 1, m.colour);
        glUniform3fv(glGetUniformLocation(prog_, "uSpec"), 1, m.spec);
        glUniform1f(glGetUniformLocation(prog_, "uPower"), m.power);
        glUniform4fv(glGetUniformLocation(prog_, "uGain"), 1, opt.tint ? m.gain : kOne);
        glUniform4fv(glGetUniformLocation(prog_, "uOffset"), 1, opt.tint ? m.offset : kZero);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex ? m.tex->id() : 0);
        glBindVertexArray(m.vao);
        glDrawElements(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, nullptr);
    }
    // Fur shells, innermost first. Shells are alpha-tested so they need no sorting.
    if (opt.fur && opt.textured && !opt.wireframe) {
        glUseProgram(furProg_);
        glUniformMatrix4fv(glGetUniformLocation(furProg_, "uViewProj"), 1, GL_FALSE, vp);
        setLights(furProg_, lights);
        glUniform1i(glGetUniformLocation(furProg_, "uVolume"), 0);
        glUniform1i(glGetUniformLocation(furProg_, "uRoot"), 1);
        glUniform1i(glGetUniformLocation(furProg_, "uTip"), 2);
        glUniform1i(glGetUniformLocation(furProg_, "uThin"), 3);
        glDisable(GL_CULL_FACE);
        for (auto& m : meshes_) {
            const Fur& f = m.fur;
            if (!m.visible || m.count == 0 || !f.volume || !f.root || !f.tip) continue;
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, f.volume->id());
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, f.root->id());
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, f.tip->id());
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, f.thinning ? f.thinning->id() : 0);
            glUniform1i(glGetUniformLocation(furProg_, "uHasThin"), f.thinning ? 1 : 0);
            glUniform1i(glGetUniformLocation(furProg_, "uNoise"), f.noise ? 1 : 0);
            glUniform2f(glGetUniformLocation(furProg_, "uVolSize"), 96.0f, 96.0f);
            glUniform1f(glGetUniformLocation(furProg_, "uVolScale"), opt.furTiling);
            // Two sets: colour and thinning share set 0, the volume tiles with set 1. Three sets: set 1 is a
            // mirrored layout for symmetric thinning maps (Kazooie's eyes) and the volume uses set 2.
            glUniform1i(glGetUniformLocation(furProg_, "uThinSet"), m.uvSets >= 3 ? 1 : 0);
            glUniform1i(glGetUniformLocation(furProg_, "uVolSet"), m.uvSets >= 3 ? 2 : m.uvSets >= 2 ? 1 : 0);
            glUniform4fv(glGetUniformLocation(furProg_, "uGain"), 1, opt.tint ? m.gain : kOne);
            glUniform4fv(glGetUniformLocation(furProg_, "uOffsetColour"), 1, opt.tint ? m.offset : kZero);
            glBindVertexArray(m.vao);
            int layers = std::max(1, f.volume->layers());
            for (int k = 0; k < layers; ++k) {
                float hgt = float(k + 1) / float(layers);
                glUniform1f(glGetUniformLocation(furProg_, "uLayer"), float(k));
                glUniform1f(glGetUniformLocation(furProg_, "uHeight"), hgt);
                glUniform1f(glGetUniformLocation(furProg_, "uOffset"), opt.furLength * radius_ * hgt);
                glDrawElements(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, nullptr);
            }
        }
        glActiveTexture(GL_TEXTURE0);
        if (!opt.backfaces) glEnable(GL_CULL_FACE);
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glBindVertexArray(0);

    // Lines: grid and skeleton (skeleton drawn on top of the mesh).
    std::vector<float> lines;
    auto addLine = [&](const float* a, const float* b, float r, float g, float bl, float al) {
        lines.insert(lines.end(), {a[0], a[1], a[2], r, g, bl, al, b[0], b[1], b[2], r, g, bl, al});
    };
    glUseProgram(lineProg_);
    glUniformMatrix4fv(glGetUniformLocation(lineProg_, "uViewProj"), 1, GL_FALSE, vp);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 28, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 28, reinterpret_cast<void*>(12));
    auto flush = [&](GLenum mode, float pointSize) {
        if (lines.empty()) return;
        glUniform1f(glGetUniformLocation(lineProg_, "uPointSize"), pointSize);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(lines.size() * sizeof(float)), lines.data(), GL_STREAM_DRAW);
        glDrawArrays(mode, 0, GLsizei(lines.size() / 7));
        lines.clear();
    };
    if (opt.grid) {
        float step = std::pow(10.0f, std::floor(std::log10(radius_)));
        int n = 10;
        float y = 0;
        for (int i = -n; i <= n; ++i) {
            float a[3] = {i * step, y, -n * step}, b[3] = {i * step, y, n * step};
            float c[3] = {-n * step, y, i * step}, d[3] = {n * step, y, i * step};
            float shade = i == 0 ? 0.55f : 0.32f;
            addLine(a, b, shade, shade, shade, 1);
            addLine(c, d, shade, shade, shade, 1);
        }
        flush(GL_LINES, 1);
    }
    if (opt.bones && !bones_.empty()) {
        glDisable(GL_DEPTH_TEST);
        for (size_t i = 0; i < bones_.size(); ++i)
            if (bones_[i].parent >= 0) addLine(boneAt(size_t(bones_[i].parent)), boneAt(i), 1.0f, 0.75f, 0.2f, 1);
        flush(GL_LINES, 1);
        glEnable(GL_PROGRAM_POINT_SIZE);
        for (size_t i = 0; i < bones_.size(); ++i) {
            const float* p = boneAt(i);
            lines.insert(lines.end(), {p[0], p[1], p[2], 1.0f, 0.35f, 0.25f, 1});
        }
        flush(GL_POINTS, 7);
        glEnable(GL_DEPTH_TEST);
    }
    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, GLuint(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

void ModelView::draw(const ImVec2& size, const Options& opt) {
    int w = std::max(16, int(size.x)), h = std::max(16, int(size.y));
    render(w, h, opt);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image(ImTextureID(intptr_t(color_)), ImVec2(float(w), float(h)), ImVec2(0, 1), ImVec2(1, 0));
    // An invisible button over the image makes the viewport an active item while a mouse button
    // is held, so drags keep working when the cursor leaves it.
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##viewport", ImVec2(float(w), float(h)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    ImGuiIO& io = ImGui::GetIO();
    bool active = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
    // The camera sits at target + distance * dir(yaw, pitch), looking at the target.
    auto dir = [this](float* d) {
        d[0] = std::cos(pitch_) * std::sin(yaw_);
        d[1] = std::sin(pitch_);
        d[2] = std::cos(pitch_) * std::cos(yaw_);
    };
    if (hovered) {
        if (io.MouseWheel != 0) distance_ *= std::pow(0.88f, io.MouseWheel);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) frame();
    }
    if (active) {
        ImVec2 dm = io.MouseDelta;
        bool pan = ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (ImGui::IsMouseDown(ImGuiMouseButton_Left) && io.KeyShift);
        if (pan) {
            // Pan: slide camera and target together in the view plane.
            float s = distance_ * 0.0015f;
            float rx[3] = {std::cos(yaw_), 0, -std::sin(yaw_)};
            float up[3] = {-std::sin(pitch_) * std::sin(yaw_), std::cos(pitch_), -std::sin(pitch_) * std::cos(yaw_)};
            for (int k = 0; k < 3; ++k) target_[k] += (-dm.x * rx[k] + dm.y * up[k]) * s;
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // Orbit around the target.
            yaw_ -= dm.x * 0.01f;
            pitch_ = std::clamp(pitch_ + dm.y * 0.01f, -1.55f, 1.55f);
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            // Turn the camera in place (look around): keep the eye, move the target.
            float d[3], eye[3];
            dir(d);
            for (int k = 0; k < 3; ++k) eye[k] = target_[k] + distance_ * d[k];
            yaw_ -= dm.x * 0.005f;
            pitch_ = std::clamp(pitch_ + dm.y * 0.005f, -1.55f, 1.55f);
            dir(d);
            for (int k = 0; k < 3; ++k) target_[k] = eye[k] - distance_ * d[k];
        }
    }
    // Fly with WASD (Q/E down/up, Shift faster) while the viewport is hovered or held.
    if ((hovered || active) && !io.WantTextInput) {
        float d[3];
        dir(d);
        float fwd[3] = {-d[0], -d[1], -d[2]}, right[3] = {std::cos(yaw_), 0, -std::sin(yaw_)};
        float move[3] = {};
        auto add = [&](const float* v, float s) { for (int k = 0; k < 3; ++k) move[k] += v[k] * s; };
        const float up[3] = {0, 1, 0};
        if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) add(fwd, 1);
        if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) add(fwd, -1);
        if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) add(right, 1);
        if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) add(right, -1);
        if (ImGui::IsKeyDown(ImGuiKey_E)) add(up, 1);
        if (ImGui::IsKeyDown(ImGuiKey_Q)) add(up, -1);
        float speed = radius_ * 0.8f * io.DeltaTime * (io.KeyShift ? 4.0f : 1.0f);
        for (int k = 0; k < 3; ++k) target_[k] += move[k] * speed;
    }
    distance_ = std::clamp(distance_, radius_ * 0.02f, radius_ * 100);
    if (opt.bones && opt.boneNames && !bones_.empty()) {
        float vp[16];
        viewProj(vp, w, h);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (size_t bi = 0; bi < bones_.size(); ++bi) {
            const auto& b = bones_[bi];
            const float* bp = boneAt(bi);
            float x = bp[0], y = bp[1], z = bp[2];
            float cx = vp[0] * x + vp[4] * y + vp[8] * z + vp[12];
            float cy = vp[1] * x + vp[5] * y + vp[9] * z + vp[13];
            float cw = vp[3] * x + vp[7] * y + vp[11] * z + vp[15];
            if (cw <= 0) continue;
            float sx = origin.x + (cx / cw * 0.5f + 0.5f) * float(w), sy = origin.y + (1 - (cy / cw * 0.5f + 0.5f)) * float(h);
            dl->AddText(ImVec2(sx + 5, sy - 7), IM_COL32(255, 220, 140, 255), b.name.c_str());
        }
    }
}

} // namespace nut::gui
