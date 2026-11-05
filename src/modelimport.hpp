// Writing models back: geometry from Wavefront OBJ, or a whole model copied from another one.
//
// The game builds each model's vertex and index buffer headers at load from the rendergraph's
// resource records (rendergraph+0xFC, read by 0x82BC32D0): vertex buffers {->header, ->.gpu offset,
// size} and index buffers {->header, ->.gpu offset, bytes, format}. A draw's index count is read
// when it draws. New geometry is appended to the model's .gpu part and those records updated.
#pragma once

#include "caff.hpp"
#include "model.hpp"
#include "streambundle.hpp"

#include <functional>
#include <memory>
#include <optional>

namespace nut::model {

struct ImportReport {
    int edited = 0;  // meshes edited in place (same vertices and triangles)
    int rebuilt = 0; // meshes given new geometry
    int hidden = 0;  // meshes emptied by an object without faces
    std::vector<std::string> notes;
};

// Applies an OBJ to the model whose .data part is `symbol`. Objects are matched to meshes by the
// "mesh<N>" in their name, as model export writes them ("group<G>_mesh<N>"); N indexes Model::meshes.
//  - Same vertex and triangle count: the mesh is edited in place (positions, normals and the first
//    texture coordinate set); fields that did not change keep their exact bytes.
//  - Anything else replaces the mesh's geometry. Each new vertex takes its bone weights, extra
//    texture coordinate sets and other fields from the nearest original vertex of the mesh.
//  - An object without faces hides the mesh.
// Meshes not named in the file are left alone. Throws when the file names no mesh or does not fit.
ImportReport importObj(caff::Caff& c, size_t symbol, const std::string& objText);

// The same from a binary FBX file's mesh objects, named the same way. Skin weights on bones named
// like the model's (exactly, or ignoring case and underscores) replace the weights copied from the
// nearest original vertex; the fur shells FBX export adds are ignored.
ImportReport importFbx(caff::Caff& c, size_t symbol, const Bytes& fbxFile);

// Rig override: moves the model's joints (its bind pose, which the game skins and animates from) to the FBX bones named
// like them. Orientations, hierarchy and bone count stay for the game's animations; unnamed bones follow their parent.
ImportReport importRig(caff::Caff& c, size_t symbol, const Bytes& fbxFile);

// OBJ or FBX, told apart by the file's content. With `rig` (FBX only), importRig first, then the meshes when the
// file has any named like the model's.
ImportReport importMeshFile(caff::Caff& c, size_t symbol, const Bytes& file, bool rig = false);

// Where copyModel gets a texture the destination lacks: the CAFF holding it (a whole texture, or
// the "mip" half of a split one) and, for a split texture, its streamed top level: a stream
// bundle entry ("<name>top"), which belongs in the destination's stream bundle.
struct TextureSource {
    std::shared_ptr<const caff::Caff> caff;
    size_t header = 0; // the texture's .data symbol in `caff`
    std::optional<streambundle::Entry> top;
};
// Called with a model texture name (split suffix removed); nothing when it cannot be found.
using FindTexture = std::function<std::optional<TextureSource>(const std::string& name)>;

struct CopyResult {
    std::vector<std::string> notes;                // differing skeletons, copied textures...
    std::vector<streambundle::Entry> streamEntries; // streamed top levels to add to the destination's stream bundle
};

// Makes the model whose .data part is `symbol` in `dst` a copy of the model `srcSymbol` in `src`
// (which may be the same CAFF): its .data, .gpu and .stream parts with their relocations, plus any
// pool strings and shared blobs it uses that `dst` lacks. The names stay, so the game loads the copy
// in the original's place.
//
// When a model loads, the game binds every texture it names (0x822D8078) and crashes on one that is
// not loaded. Textures that neither `dst` holds nor the original model used are copied into `dst`
// from `find`; their streamed top levels come back in CopyResult::streamEntries for the caller to
// add to the destination's stream bundle. Throws, changing nothing, when a texture cannot be found.
CopyResult copyModel(caff::Caff& dst, size_t symbol, const caff::Caff& src, size_t srcSymbol, const FindTexture& find = {});

// A FindTexture that looks in one CAFF (whole textures and mip halves) and, for streamed top
// levels, in a stream bundle file (normally the CAFF's paired one; may be empty).
FindTexture texturesIn(std::shared_ptr<const caff::Caff> caff, const fs::path& streamBundle);

// The stream bundle paired with a bundle: Bundle/4f/<name> -> Bundle/50/<name>.
fs::path pairedStreamBundle(const fs::path& bundle);
// Adds the entries whose ids `sb` lacks; returns how many were added.
int addStreamEntries(streambundle::StreamBundle& sb, const std::vector<streambundle::Entry>& entries);

} // namespace nut::model
