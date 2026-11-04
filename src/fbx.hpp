// Autodesk FBX export (binary FBX 7.4) of a model: meshes skinned to its skeleton with a bind pose,
// materials with their colour maps (PNG, embedded), and animations as takes (animation stacks).
//
// Bones become LimbNode models under a Null named after the model, with their bind-pose local
// transforms; each mesh is a Mesh model under the same Null with a Skin deformer whose clusters
// carry the bind matrices. Animations are sampled at `fps` the way the viewer plays them: a matched
// bone takes the animation's rotation and scale and adds its translation to the bind offset.
// Rotations are written as XYZ Euler angles in degrees, kept continuous from frame to frame.
#pragma once

#include "image.hpp"
#include "model.hpp"

#include <array>
#include <functional>
#include <optional>

namespace nut::fbx {

struct NamedAnimation {
    std::string name; // take name
    const model::Animation* anim = nullptr;
};

struct Options {
    std::vector<bool> include;               // meshes to write (default: the shown groups)
    std::vector<NamedAnimation> animations;
    // PNG data of a texture (by Model::textures name) to embed; without it the file only refers
    // to "<name>.png" next to it.
    std::function<std::optional<Bytes>(const std::string&)> texture;
    // Bake each material's colour gain and offset into its texture (texture * c:textureGain + c:textureOffset),
    // as the game's shaders draw it; one tinted copy per texture and tint.
    bool tint = true;
    float fps = 30;
    float unitScale = 100; // centimetres per model unit (FBX UnitScaleFactor); the game works in metres
    // Fur shells, as the model viewer draws them: each written mesh that binds a fur volume gets one more
    // mesh per volume layer, pushed out along its normals and skinned like it. Each layer's texture is
    // baked from the root and tip colour maps, with alpha = the layer's coverage.
    bool fur = true;
    float furLength = 0.015f; // shell thickness at the tips, as a fraction of the model's radius
    float furTiling = 6;      // repeats of the fur volume over the surface
    uint32_t furSize = 512;   // baked texture size
    // Level 0 of a texture as RGBA, every slice of a volume; needed for fur.
    std::function<std::optional<std::vector<image::Rgba>>(const std::string&)> pixels;
};

struct Report {
    size_t meshes = 0, vertices = 0, triangles = 0, bones = 0, materials = 0, textures = 0, embedded = 0, animations = 0;
    size_t furShells = 0;
    std::vector<std::string> notes;
};

Bytes write(const model::Model& m, const Options& opt, Report* report = nullptr);

// ---- reading ----------------------------------------------------------------------------------

bool isFbx(const Bytes& file);

// A mesh of a binary FBX file (7.x), in the game's space: metres, Y up, facing +Z. The file's node
// transforms, unit scale and axis settings are applied.
struct MeshData {
    std::string name;                         // the mesh object's name
    std::vector<std::array<float, 3>> points; // control points
    std::vector<std::vector<int>> polygons;   // each polygon's corners (indices into the corner arrays)
    std::vector<int> cornerPoint;             // control point of each corner
    std::vector<std::array<float, 3>> cornerNormal; // empty when the file has no normals
    std::vector<std::array<float, 2>> cornerUv;     // first UV set as stored (v up); empty when none
    // Per control point: bone (by name) and weight; empty when the mesh has no skin.
    std::vector<std::vector<std::pair<std::string, float>>> skin;
};
std::vector<MeshData> readMeshes(const Bytes& file);

} // namespace nut::fbx
