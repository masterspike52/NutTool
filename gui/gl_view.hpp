// OpenGL 3.3 model viewport rendered into a framebuffer and shown with ImGui::Image.
#pragma once

#include "model.hpp"

#include <glad/glad.h>
#include <imgui.h>

#include <memory>
#include <string>
#include <vector>

namespace nut::gui {

class GlTexture {
public:
    GlTexture() = default;
    GlTexture(const GlTexture&) = delete;
    GlTexture& operator=(const GlTexture&) = delete;
    ~GlTexture();
    void upload(const uint8_t* rgba, int w, int h, bool mipmaps, bool nearest);
    GLuint id() const { return id_; }
    int width() const { return w_; }
    int height() const { return h_; }
    ImTextureID imgui() const { return ImTextureID(intptr_t(id_)); }

private:
    GLuint id_ = 0;
    int w_ = 0, h_ = 0;
};

// Equally sized RGBA layers in one GL_TEXTURE_2D_ARRAY (fur shell volumes).
class GlTextureArray {
public:
    GlTextureArray() = default;
    GlTextureArray(const GlTextureArray&) = delete;
    GlTextureArray& operator=(const GlTextureArray&) = delete;
    ~GlTextureArray();
    // `rgba` holds `layers` images of w*h*4 bytes back to back.
    void upload(const uint8_t* rgba, int w, int h, int layers);
    GLuint id() const { return id_; }
    int layers() const { return layers_; }

private:
    GLuint id_ = 0;
    int layers_ = 0;
};

class ModelView {
public:
    struct Options {
        bool textured = true;
        bool wireframe = false;
        bool bones = true;
        bool boneNames = false;
        bool grid = true;
        bool backfaces = true;
        bool tint = true; // apply material texture gain/offset
        bool fur = true;
        float furLength = 0.015f; // shell thickness as a fraction of the model's radius
        float furTiling = 6;      // repeats of the fur volume over the surface (the game's value is in its shaders)
        // Lighting.
        int lighting = 0;              // 0 studio (key, fill and rim lights), 1 headlight, 2 unlit
        bool lightFollowsCamera = true;
        float lightAzimuth = 40;       // degrees around the vertical axis, when the lights stay in place
        float lightElevation = 35;     // degrees above the horizon
        float brightness = 1.0f;
        float ambient = 0.3f;
        float lightColour[3] = {1.0f, 0.97f, 0.92f};
    };

    // Fur shells drawn over a mesh: coverage per shell from `volume` (its alpha, or strand
    // noise when the volume has none), colour from root to tip, length scaled by `thinning`.
    struct Fur {
        const GlTextureArray* volume = nullptr;
        const GlTexture* root = nullptr;
        const GlTexture* tip = nullptr;
        const GlTexture* thinning = nullptr;
        bool noise = false;
    };

    ModelView();
    ~ModelView();

    // Replaces the geometry. `textures` are per-mesh (nullptr = untextured).
    void setModel(const model::Model& m);
    void setMeshTexture(size_t mesh, const GlTexture* tex);
    void setMeshVisible(size_t mesh, bool v);
    void setMeshColour(size_t mesh, const float* rgb); // used when the mesh draws untextured
    void setMeshTint(size_t mesh, const float* gain, const float* offset); // texture * gain + offset
    void setMeshFur(size_t mesh, const Fur& fur);
    // Animated pose: 16 floats (column-major) per bone mapping bind-pose space to the pose, and the
    // bones' animated positions (3 floats each) for the skeleton overlay. nullptr = bind pose.
    void setPose(const std::vector<float>* skinMatrices, const std::vector<float>* boneWorld);
    void frame();      // reset the camera to fit the model
    // Draws the viewport filling `size` and handles mouse input on it.
    void draw(const ImVec2& size, const Options& opt);

private:
    struct GpuMesh {
        GLuint vao = 0, vbo = 0, ibo = 0;
        GLsizei count = 0;
        const GlTexture* tex = nullptr;
        float colour[3] = {0.72f, 0.70f, 0.66f};
        float gain[4] = {1, 1, 1, 1}, offset[4] = {0, 0, 0, 0};
        float spec[3] = {0, 0, 0}, power = 16;
        int uvSets = 0;
        bool visible = true;
        std::vector<float> bind;      // bind-pose vertex data of skinned meshes
        std::vector<uint8_t> boneIds; // 4 per vertex
        std::vector<float> weights;   // 4 per vertex
        int skinnedVersion = 0;
        Fur fur;
    };
    struct Lights {
        float dir[3][3] = {};
        float colour[3][3] = {};
        float sky[3] = {}, ground[3] = {};
        bool unlit = false;
    };
    Lights makeLights(const Options& opt) const;
    void setLights(GLuint prog, const Lights& lights) const;
    void render(int w, int h, const Options& opt);
    void skinMeshes();
    const float* boneAt(size_t i) const {
        return boneWorld_.size() >= (i + 1) * 3 ? &boneWorld_[i * 3] : bones_[i].world;
    }
    void ensureTarget(int w, int h);
    void release();
    void viewProj(float* out, int w, int h) const;

    std::vector<GpuMesh> meshes_;
    std::vector<model::Bone> bones_;
    std::vector<float> skin_, boneWorld_;
    int poseVersion_ = 0;
    bool skinDirty_ = false;
    float center_[3] = {}, radius_ = 1;
    float yaw_ = 0.6f, pitch_ = 0.3f, distance_ = 3;
    float target_[3] = {};
    GLuint fbo_ = 0, color_ = 0, depth_ = 0;
    int fw_ = 0, fh_ = 0;
    GLuint prog_ = 0, furProg_ = 0, lineProg_ = 0, lineVao_ = 0, lineVbo_ = 0;
};

} // namespace nut::gui
