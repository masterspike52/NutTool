// Game-wide index of textures, models and UI icons, built by scanning every bundle and
// stream bundle once (cached in the settings folder, keyed by file sizes and times).
#pragma once

#include "caff.hpp"
#include "common.hpp"
#include "ops.hpp"
#include "streambundle.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>

namespace nut::gui {

struct AssetLoc {
    fs::path file;
    int entry = -1;       // stream bundle entry, -1 for a bundle
    uint32_t symbol = 0;  // texture header / model .data symbol inside the CAFF
};

struct TextureInfo {
    std::string name;                // merged name (split "mip"/"top" suffix removed)
    std::vector<AssetLoc> full;      // complete textures, or mip halves of split ones
    std::vector<AssetLoc> tops;      // streamed level 0 of split textures
    uint32_t width = 0, height = 0, mips = 0;
    std::string format;
    bool ui = false;
};

struct ModelInfo {
    std::string name;
    std::vector<AssetLoc> locs;
};

struct Catalog {
    std::vector<TextureInfo> textures; // sorted by name
    std::vector<ModelInfo> models;
    std::map<std::string, size_t> textureIndex;
    std::map<std::string, size_t> modelIndex;
    std::vector<ModelInfo> anims; // animations (name and locations, like models)
    std::map<std::string, size_t> animIndex;

    const TextureInfo* texture(const std::string& name) const;
    const ModelInfo* model(const std::string& name) const;
    const ModelInfo* anim(const std::string& name) const;

    static std::shared_ptr<Catalog> build(const fs::path& root, const fs::path& cacheFile, const ops::Progress& progress);
};

// Loads CAFFs from bundles / stream bundle entries, keeping recently used files decompressed.
class CaffCache {
public:
    // Returns the CAFF holding `loc` (the bundle itself, or the stream bundle entry).
    caff::Caff& get(const AssetLoc& loc);

private:
    struct Item {
        std::unique_ptr<caff::Caff> bundle;                         // for bundles
        std::map<int, std::unique_ptr<caff::Caff>> entries;          // for stream bundles
        std::unique_ptr<streambundle::StreamBundle> sb;
        std::vector<streambundle::IndexEntry> index; // stream bundles read entry by entry
        bool indexed = false;
    };
    std::map<fs::path, Item> items_;
    std::vector<fs::path> order_;
};

// Decodes a catalog texture (level 0 merged from the stream bundle when split).
ops::DecodedTexture decodeCatalogTexture(const TextureInfo& t, CaffCache& cache);

} // namespace nut::gui
