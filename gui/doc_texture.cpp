// A single texture from the catalog, with every copy of it across bundles.
#include "docs.hpp"

#include "ops.hpp"

#include <algorithm>
#include <set>

namespace nut::gui {
namespace {

std::string shortName(const std::string& n) {
    for (const char* p : {"aid_texture_banjox_", "aid_texture_banjoxdlc_", "aid_texture_"})
        if (startsWith(n, p)) return n.substr(std::string(p).size());
    return n;
}

class TextureDocument : public Document {
public:
    TextureDocument(const TextureInfo& t, const fs::path& root) : t_(t), root_(root) {
        path = !t.full.empty() ? t.full.front().file : t.tops.front().file;
        reload();
    }

    std::string title() const override { return "tex " + shortName(t_.name); }

    void draw(App& app) override {
        ImGui::TextUnformatted(t_.name.c_str());
        ImGui::TextDisabled("%s  %ux%u  %u mip level(s)%s   %zu cop%s in the game files", t_.format.c_str(), t_.width, t_.height, t_.mips,
                            t_.ui ? "   UI icon" : "", copies(), copies() == 1 ? "y" : "ies");
        if (!error_.empty()) {
            ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", error_.c_str());
            return;
        }
        if (ImGui::Button("Export PNG...")) {
            image::Rgba img = texture::toRgba(tex_.header, tex_.levels[size_t(preview_.level)], uint32_t(preview_.slice));
            app.saveFileDialog("Export PNG", {{"PNG image", "png"}}, [&app, img](const fs::path& p) {
                writeFile(p, image::encodePng(img));
                app.status("Exported " + toUtf8(p));
            }, fromUtf8(ops::safeName(tex_.name, 150) + ".png"));
        }
        ImGui::SameLine();
        if (ImGui::Button("Export DDS...")) {
            Bytes dds = image::encodeDds(texture::toDds(tex_.header, tex_.levels, uint32_t(preview_.slice)));
            app.saveFileDialog("Export DDS", {{"DDS texture", "dds"}}, [&app, dds](const fs::path& p) {
                writeFile(p, dds);
                app.status("Exported " + toUtf8(p));
            }, fromUtf8(ops::safeName(tex_.name, 150) + ".dds"));
        }
        ImGui::SameLine();
        bool canImport = tex_.header.kind == texture::Kind::Tex2D;
        ImGui::BeginDisabled(!canImport);
        if (ImGui::Button("Import image...")) {
            app.openFileDialog("Import texture image", {{"Images", "png;dds"}}, [this, &app](const fs::path& img) {
                app.runTask("Importing " + shortName(t_.name), true, [this, img, &app](Task&) -> std::function<void()> {
                    int written = importAll(app, img);
                    return [this, &app, written] {
                        reload();
                        preview_.dirty = true;
                        app.status("Imported into " + std::to_string(written) + " file(s) (originals kept as .bak)");
                    };
                });
            });
        }
        ImGui::EndDisabled();
        helpMarker("Writes the image into every copy of this texture (each bundle, and the streamed top level of split textures). "
                   "PNG must match the size shown; DDS must use the same format. Changed files are saved immediately with a .bak backup.");
        if (ImGui::CollapsingHeader("Where it lives")) {
            auto row = [&](const AssetLoc& l, const char* what) {
                std::string s = toUtf8(fs::relative(l.file, root_));
                if (l.entry >= 0) s += "  entry " + std::to_string(l.entry + 1);
                ImGui::PushID(&l);
                if (ImGui::SmallButton("Open")) app.openPath(l.file);
                ImGui::SameLine();
                ImGui::Text("%s  %s", what, s.c_str());
                ImGui::PopID();
            };
            for (auto& l : t_.full) row(l, t_.tops.empty() ? "texture" : "mips   ");
            for (auto& l : t_.tops) row(l, "level 0");
        }
        preview_.draw(app, tex_.header, tex_.levels, std::max(em(160), ImGui::GetContentRegionAvail().y - em(40)));
    }

private:
    size_t copies() const {
        std::set<fs::path> files;
        for (auto& l : t_.full) files.insert(l.file);
        for (auto& l : t_.tops) if (t_.full.empty()) files.insert(l.file);
        return files.size();
    }

    void reload() {
        error_.clear();
        try {
            CaffCache cache;
            tex_ = decodeCatalogTexture(t_, cache);
        } catch (std::exception& e) {
            error_ = e.what();
        }
    }

    // Writes the image into every copy. Runs on a worker thread (the UI is blocked by a modal task).
    int importAll(App& app, const fs::path& img) {
        int written = 0;
        for (auto& loc : t_.full) {
            Loaded l = loadAsset(loc.file);
            if (loc.entry < 0) {
                if (!l.caff) continue;
                auto ref = ops::textureForSymbol(*l.caff, loc.symbol);
                if (!ref) continue;
                const AssetLoc* top = nullptr;
                for (auto& tp : t_.tops) if (tp.file.filename() == loc.file.filename()) top = &tp;
                std::optional<Loaded> sl;
                if (top) sl = loadAsset(top->file);
                bool streamChanged = false;
                ops::importTexture(*l.caff, *ref, img, sl && sl->sb ? &*sl->sb : nullptr, &streamChanged);
                app.backupBeforeWrite(loc.file);
                saveCaff(loc.file, *l.caff);
                ++written;
                if (streamChanged) {
                    app.backupBeforeWrite(top->file);
                    writeFile(top->file, sl->sb->serialize());
                    ++written;
                }
            } else if (l.sb && size_t(loc.entry) < l.sb->entries.size()) {
                caff::Caff c = caff::Caff::parse(l.sb->entries[size_t(loc.entry)].data);
                auto ref = ops::textureForSymbol(c, loc.symbol);
                if (!ref) continue;
                ops::importTexture(c, *ref, img, nullptr, nullptr);
                l.sb->entries[size_t(loc.entry)].data = c.serialize();
                app.backupBeforeWrite(loc.file);
                writeFile(loc.file, l.sb->serialize());
                ++written;
            }
        }
        return written;
    }

    TextureInfo t_;
    fs::path root_;
    ops::DecodedTexture tex_;
    std::string error_;
    TexturePreview preview_;
};

} // namespace

std::unique_ptr<Document> makeTextureDocument(const TextureInfo& t, const fs::path& root) { return std::make_unique<TextureDocument>(t, root); }

} // namespace nut::gui
