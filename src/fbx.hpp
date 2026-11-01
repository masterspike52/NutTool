// Autodesk FBX export (binary FBX 7.4) of a model: meshes skinned to its skeleton with a bind pose,
// materials with their colour maps (PNG, embedded), and animations as takes (animation stacks).
//
// Bones become LimbNode models under a Null named after the model, with their bind-pose local
// transforms; each mesh is a Mesh model under the same Null with a Skin deformer whose clusters
// carry the bind matrices. Animations are sampled at `fps` the way the viewer plays them: a matched
// bone takes the animation's rotation and scale and adds its translation to the bind offset.
// Rotations are written as XYZ Euler angles in degrees, kept continuous from frame to frame.
#pragma once

#include "model.hpp"

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
    float fps = 30;
    float unitScale = 100; // centimetres per model unit (FBX UnitScaleFactor); the game works in metres
};

struct Report {
    size_t meshes = 0, vertices = 0, triangles = 0, bones = 0, materials = 0, textures = 0, embedded = 0, animations = 0;
    std::vector<std::string> notes;
};

Bytes write(const model::Model& m, const Options& opt, Report* report = nullptr);

} // namespace nut::fbx
