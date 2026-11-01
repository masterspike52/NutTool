// Document types of nuttool-gui. Constructors run on worker threads (parsing only);
// all rendering resources are created lazily in draw().
#pragma once

#include "app.hpp"
#include "catalog.hpp"
#include "container.hpp"
#include "texture.hpp"

namespace nut::gui {

std::unique_ptr<Document> makeCaffDocument(const fs::path& p, const fs::path& root, Loaded l);
std::unique_ptr<Document> makeStreamBundleDocument(const fs::path& p, const fs::path& root, Loaded l);
std::unique_ptr<Document> makeTextDocument(const fs::path& p, const fs::path& root, Loaded l);
std::unique_ptr<Document> makeFontDocument(const fs::path& p, Bytes data);
std::unique_ptr<Document> makeVideoDocument(const fs::path& p, Bytes data);
std::unique_ptr<Document> makeIniDocument(const fs::path& p, Bytes data);
std::unique_ptr<Document> makeImageDocument(const fs::path& p, Bytes data);
std::unique_ptr<Document> makeIndexDocument(const fs::path& p, const fs::path& root);
std::unique_ptr<Document> makeHexDocument(const fs::path& p, Bytes data, const std::string& kind);
std::unique_ptr<Document> makeTextureDocument(const TextureInfo& t, const fs::path& root);
std::unique_ptr<Document> makeModelDocument(const ModelInfo& m, std::shared_ptr<const Catalog> catalog, const fs::path& root);

// "banjox_frontend"-style label from db_index for an asset file (thread-safe, cached per root).
std::string friendlyName(const fs::path& root, const fs::path& p);

// Shared texture preview widget (used by bundle and stream bundle documents).
struct TexturePreview {
    int level = 0, slice = 0, channel = 0; // channel: 0 RGBA, 1 RGB, 2 alpha
    float zoom = 1.0f;
    bool fit = true;
    bool dirty = true;
    std::shared_ptr<GpuImage> image;
    void draw(App& app, const texture::Header& h, const std::vector<texture::Level>& levels, float height);
};

} // namespace nut::gui
