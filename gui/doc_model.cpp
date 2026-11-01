// 3D model viewer: geometry, textures and skeleton of a model from the catalog.
#include "docs.hpp"
#include "fbx.hpp"

#include "gl_view.hpp"
#include "model.hpp"
#include "modelimport.hpp"
#include "streambundle.hpp"
#include "ops.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <map>
#include <set>

namespace nut::gui {
namespace {

std::string shortModelName(const std::string& n) {
    for (const char* p : {"aid_model_banjox_", "aid_model_banjoxdlc_", "aid_model_"})
        if (startsWith(n, p)) return n.substr(std::string(p).size());
    return n;
}

// Column-major 4x4 matrices for the skeleton.
using Mat4 = std::array<float, 16>;

Mat4 identity4() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

Mat4 mul4(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[size_t(k * 4 + row)] * b[size_t(c * 4 + k)];
            r[size_t(c * 4 + row)] = s;
        }
    return r;
}

// Translation * rotation (quaternion x, y, z, w) * scale.
Mat4 trs(const float* t, const float* q, const float* s) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float n = std::sqrt(x * x + y * y + z * z + w * w);
    if (n > 0) { x /= n; y /= n; z /= n; w /= n; }
    Mat4 m = identity4();
    m[0] = (1 - 2 * (y * y + z * z)) * s[0];
    m[1] = (2 * (x * y + z * w)) * s[0];
    m[2] = (2 * (x * z - y * w)) * s[0];
    m[4] = (2 * (x * y - z * w)) * s[1];
    m[5] = (1 - 2 * (x * x + z * z)) * s[1];
    m[6] = (2 * (y * z + x * w)) * s[1];
    m[8] = (2 * (x * z + y * w)) * s[2];
    m[9] = (2 * (y * z - x * w)) * s[2];
    m[10] = (1 - 2 * (x * x + y * y)) * s[2];
    m[12] = t[0];
    m[13] = t[1];
    m[14] = t[2];
    return m;
}

// Inverse of a rotation + translation matrix (bind poses carry no scale).
Mat4 rigidInverse(const Mat4& m) {
    Mat4 r = identity4();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r[size_t(j * 4 + i)] = m[size_t(i * 4 + j)];
    for (int i = 0; i < 3; ++i) r[size_t(12 + i)] = -(r[size_t(i)] * m[12] + r[size_t(4 + i)] * m[13] + r[size_t(8 + i)] * m[14]);
    return r;
}

std::string shortAnimName(const std::string& n, const std::string& prefix) {
    if (!prefix.empty() && startsWith(n, prefix)) return n.substr(prefix.size());
    for (const char* p : {"aid_anim_banjox_", "aid_anim_"})
        if (startsWith(n, p)) return n.substr(std::string(p).size());
    return n;
}

std::string layoutText(const model::Mesh& m) {
    std::string s = std::to_string(m.stride) + " B: pos";
    if (m.layout.skinned) s += ", skin";
    if (m.layout.normal >= 0) s += ", normal";
    if (m.layout.tangent >= 0) s += ", tangent";
    if (!m.layout.uvSets.empty()) s += ", " + std::to_string(m.layout.uvSets.size()) + " UV";
    return s;
}

class ModelDocument : public Document {
public:
    ModelDocument(const ModelInfo& info, std::shared_ptr<const Catalog> catalog, const fs::path& root)
        : info_(info), catalog_(std::move(catalog)), root_(root) {
        path = info.locs.front().file;
        loadModel(cache_.get(info.locs.front()), info.locs.front().symbol);
    }

    // (Re)builds everything shown from one copy of the model.
    void loadModel(const caff::Caff& c, uint32_t symbol) {
        stopAnimation();
        view_.reset(); // the next frame uploads the new meshes and textures
        textures_.clear();
        ownAnims_.clear();
        hasFur_ = false;
        colourGroups_.clear();
        colourEdited_.clear();
        model_ = model::parse(c, symbol);
        // Decode every referenced texture (level 0, scaled down to at most 1024 for the viewport).
        for (auto& name : model_.textures) {
            LoadedTex lt;
            lt.name = name;
            const TextureInfo* ti = catalog_ ? catalog_->texture(name) : nullptr;
            if (!ti) {
                lt.error = "not found in the game files";
            } else {
                try {
                    ops::DecodedTexture d = decodeCatalogTexture(*ti, cache_);
                    if (d.header.kind == texture::Kind::Volume) {
                        // Fur volumes: every slice of level 0, as layers of one array texture.
                        lt.volumeLayers = int(d.levels[0].slices.size());
                        lt.noAlpha = true;
                        for (int s = 0; s < lt.volumeLayers; ++s) {
                            image::Rgba sl = texture::toRgba(d.header, d.levels[0], uint32_t(s));
                            for (size_t p = 3; p < sl.pixels.size(); p += 4) lt.noAlpha &= sl.pixels[p] == 255;
                            lt.volume.insert(lt.volume.end(), sl.pixels.begin(), sl.pixels.end());
                        }
                    }
                    image::Rgba img = texture::toRgba(d.header, d.levels[0], 0);
                    uint32_t w = img.width, h = img.height;
                    while (w > 1024 || h > 1024) { w = std::max(1u, w / 2); h = std::max(1u, h / 2); }
                    if (w != img.width || h != img.height) img = texture::downscale(img, w, h);
                    lt.img = std::move(img);
                } catch (std::exception& e) {
                    lt.error = e.what();
                }
            }
            hasFur_ |= lt.volumeLayers > 0;
            textures_.push_back(std::move(lt));
        }
        // Animations named after this model ("aid_model_x" -> "aid_anim_x_...").
        if (catalog_ && startsWith(model_.name, "aid_model_")) {
            animPrefix_ = "aid_anim_" + model_.name.substr(10) + "_";
            for (auto& a : catalog_->anims)
                if (startsWith(a.name, animPrefix_)) ownAnims_.push_back(a.name);
        }
        computeBindPose();
        initMeshState();
        collectColours();
    }

public:
    std::string title() const override { return "model " + shortModelName(model_.name); }

    void draw(App& app) override {
        // A reload asked for while the panel was drawn (it replaces the view the rest of that frame uses).
        if (deferred_) std::exchange(deferred_, nullptr)();
        if (!view_) {
            view_ = std::make_unique<ModelView>();
            for (auto& t : textures_) {
                if (t.img.pixels.empty()) continue;
                t.gl = std::make_unique<GlTexture>();
                t.gl->upload(t.img.pixels.data(), int(t.img.width), int(t.img.height), true, false);
                if (t.volumeLayers > 0) {
                    t.glArray = std::make_unique<GlTextureArray>();
                    t.glArray->upload(t.volume.data(), int(t.img.width), int(t.img.height), t.volumeLayers);
                }
            }
            pushModel();
        }
        float h = ImGui::GetContentRegionAvail().y, w = ImGui::GetContentRegionAvail().x;
        float panel = std::clamp(panelFrac_ * w, em(260), w - em(300));
        ImGui::BeginChild("##props", ImVec2(panel, h), ImGuiChildFlags_Borders);
        drawPanel(app);
        ImGui::EndChild();
        splitter("##modelsplit", panel, em(260), w - em(300), h);
        panelFrac_ = panel / w;
        ImGui::BeginChild("##viewport", ImVec2(0, h));
        if (!startupChecked_) {
            startupChecked_ = true;
            std::string a = app.takeStartupAnimation();
            // --replace-with: the same path as picking from "Replace with another model" (then --anim,
            // since reloading the model stops animations).
            std::string r = app.takeStartupReplacement();
            if (!r.empty())
                deferred_ = [this, &app, r, a] {
                    if (preview(app, [&](caff::Caff& c, uint32_t sym) { return copyFrom(c, sym, info_.locs.front(), r).notes; })) pendingSwap_ = r;
                    if (!a.empty()) loadAnimation(a);
                };
            else if (!a.empty())
                loadAnimation(a);
            fs::path fbxOut = app.takeStartupFbx(); // --export-fbx: as "Export FBX..." with the default choices
            if (!fbxOut.empty()) exportFbxTo(app, fbxOut);
        }
        updateAnimation();
        view_->draw(ImGui::GetContentRegionAvail(), opt_);
        ImGui::EndChild();
    }

private:
    struct LoadedTex {
        std::string name;
        image::Rgba img;
        std::unique_ptr<GlTexture> gl;
        std::string error;
        // Volume textures (fur): all slices of level 0, uploaded as an array texture.
        std::vector<uint8_t> volume;
        int volumeLayers = 0;
        bool noAlpha = false; // the volume carries no coverage; shells use strand noise
        std::unique_ptr<GlTextureArray> glArray;
    };

    // Fur draws bind {0 root colour, 1 fur volume, 2 tip colour, 3 thinning, ...}.
    ModelView::Fur furOf(const model::Mesh& m) const {
        ModelView::Fur f;
        auto at = [&](int slot) -> const LoadedTex* {
            for (size_t k = 0; k < m.textures.size() && k < m.textureSlots.size(); ++k)
                if (m.textureSlots[k] == slot && size_t(m.textures[k]) < textures_.size()) return &textures_[size_t(m.textures[k])];
            return nullptr;
        };
        const LoadedTex* vol = at(1);
        if (!vol || !vol->glArray) return f;
        const LoadedTex *root = at(0), *tip = at(2), *thin = at(3);
        f.volume = vol->glArray.get();
        f.noise = vol->noAlpha;
        f.root = root && root->gl && !root->glArray ? root->gl.get() : nullptr;
        f.tip = tip && tip->gl && !tip->glArray ? tip->gl.get() : f.root;
        f.thinning = thin && thin->gl && !thin->glArray ? thin->gl.get() : nullptr;
        if (!f.root) f.root = f.tip;
        return f;
    }

    void initMeshState() {
        meshTex_.clear();
        for (auto& m : model_.meshes) meshTex_.push_back(m.layout.uvSets.empty() ? -1 : m.texture);
        meshVisible_.assign(model_.meshes.size(), true);
        lod_ = model_.defaultLod;
        switchState_.clear();
        for (auto& [id, states] : model_.switches) switchState_[id] = states.front();
        resetGroups();
    }

    // Groups the game draws at the chosen detail level and switch states.
    void resetGroups() {
        if (model_.nodes.empty()) {
            groupShown_.clear();
            for (auto& g : model_.groups) groupShown_.push_back(g.shown);
        } else {
            groupShown_ = model::visibleGroups(model_, lod_, switchState_);
        }
    }

    // What a switch state shows: the names of its nodes ("wrench", "camera"...).
    static constexpr int kSwitchOff = -1;

    std::string switchLabel(int id, int state) const {
        if (state == kSwitchOff) return "off";
        std::string out;
        for (auto& n : model_.nodes) {
            if (n.switchId != id || n.switchState != state) continue;
            std::string seg = n.name.substr(n.name.rfind('|') == std::string::npos ? 0 : n.name.rfind('|') + 1);
            size_t p = lower(seg).rfind("_switch");
            if (p != std::string::npos) seg = seg.substr(0, p);
            if (seg.rfind(':') != std::string::npos) seg = seg.substr(seg.rfind(':') + 1);
            if (out.find(seg) == std::string::npos) out += (out.empty() ? "" : ", ") + seg;
        }
        return std::to_string(state) + (out.empty() ? "" : ": " + out);
    }

    // A readable name for a group: its deepest named node.
    std::string groupLabel(size_t g) const {
        const auto& path = model_.groups[g].path;
        for (size_t k = path.size(); k-- > 0;) {
            const std::string& n = model_.nodes[size_t(path[k])].name;
            if (n.empty()) continue;
            std::string seg = n.substr(n.rfind('|') == std::string::npos ? 0 : n.rfind('|') + 1);
            if (!seg.empty()) return seg;
        }
        return "group " + std::to_string(g);
    }

    bool meshShown(size_t i) const { return meshVisible_[i] && groupShown_[size_t(model_.meshes[i].group)]; }

    void pushModel() {
        view_->setModel(model_);
        applyMeshState();
    }

    void applyMeshState() {
        for (size_t i = 0; i < model_.meshes.size(); ++i) {
            int t = meshTex_[i];
            view_->setMeshTexture(i, t >= 0 && size_t(t) < textures_.size() && textures_[size_t(t)].gl ? textures_[size_t(t)].gl.get() : nullptr);
            view_->setMeshVisible(i, meshShown(i));
            view_->setMeshColour(i, model_.meshes[i].colour);
            view_->setMeshTint(i, model_.meshes[i].textureGain, model_.meshes[i].textureOffset);
            view_->setMeshFur(i, furOf(model_.meshes[i]));
        }
    }

    void drawPanel(App& app) {
        ImGui::TextWrapped("%s", model_.name.c_str());
        size_t verts = 0, tris = 0, shownMeshes = 0;
        for (size_t i = 0; i < model_.meshes.size(); ++i) {
            if (!meshShown(i)) continue;
            ++shownMeshes;
            verts += model_.meshes[i].vertices.size();
            tris += model_.meshes[i].indices.size() / 3;
        }
        ImGui::TextDisabled("%zu meshes shown, %zu vertices, %zu triangles, %zu bones", shownMeshes, verts, tris, model_.bones.size());
        if (model_.meshes.empty() && !model_.warnings.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", model_.warnings.front().c_str());
        ImGui::SeparatorText("View");
        ImGui::Checkbox("Textures", &opt_.textured);
        ImGui::SameLine();
        ImGui::Checkbox("Wireframe", &opt_.wireframe);
        ImGui::SameLine();
        ImGui::Checkbox("Grid", &opt_.grid);
        ImGui::Checkbox("Bones", &opt_.bones);
        ImGui::SameLine();
        ImGui::BeginDisabled(!opt_.bones);
        ImGui::Checkbox("Bone names", &opt_.boneNames);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox("Back faces", &opt_.backfaces);
        ImGui::SameLine();
        ImGui::Checkbox("Material tint", &opt_.tint);
        helpMarker("Multiplies textures by their material's colour gain (c:textureGain), as the game's shaders do.");
        if (hasFur_) {
            ImGui::Checkbox("Fur", &opt_.fur);
            ImGui::SameLine();
            ImGui::BeginDisabled(!opt_.fur);
            float pct = opt_.furLength * 100;
            ImGui::SetNextItemWidth(em(120));
            if (ImGui::SliderFloat("Fur length", &pct, 0.2f, 6.0f, "%.1f%%")) opt_.furLength = pct / 100;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(em(100));
            ImGui::SliderFloat("Tiling", &opt_.furTiling, 1, 16, "%.0f");
            ImGui::EndDisabled();
            helpMarker("Fur is drawn as shells: one layer per slice of the fur volume texture, pushed out along the surface. "
                       "The length is a percentage of the model's size and the tiling is how often the volume repeats; "
                       "the game sets both in its fur shader, which is not decoded.");
        }
        drawLighting();
        if (ImGui::Button("Reset camera")) view_->frame();
        helpMarker("Left-drag: orbit around the model.\nRight-drag: turn the camera (look around).\nMiddle-drag or Shift+left-drag: pan.\n"
                   "Wheel: zoom.\nW/A/S/D or arrow keys: fly, Q/E: down/up, hold Shift to go faster (while the mouse is over the view).\n"
                   "Double-click: reset.");
        ImGui::SameLine();
        if (ImGui::Button("Export OBJ...")) exportObj(app);
        helpMarker("Writes the shown meshes as .obj + .mtl and the textures as PNG into a folder.");
        ImGui::SameLine();
        if (ImGui::Button("Export FBX...")) ImGui::OpenPopup("Export FBX");
        helpMarker("Writes the shown meshes skinned to the skeleton, materials with their textures (embedded) and animations as one .fbx file.");
        drawFbxExport(app);
        if (!model_.warnings.empty() && ImGui::CollapsingHeader("Warnings"))
            for (auto& w : model_.warnings) ImGui::BulletText("%s", w.c_str());

        drawReplace(app);
        drawAnimation();
        if (model_.groups.size() > 1) drawParts();
        drawColours();

        ImGui::SeparatorText("Meshes");
        ImGui::TextDisabled("Textured meshes show the colour map among the textures their draw binds; the others use their material's base colour.");
        for (size_t i = 0; i < model_.meshes.size(); ++i) {
            auto& m = model_.meshes[i];
            if (!groupShown_[size_t(m.group)]) continue;
            ImGui::PushID(int(i));
            bool vis = meshVisible_[i];
            if (ImGui::Checkbox("##vis", &vis)) { meshVisible_[i] = vis; applyMeshState(); }
            ImGui::SameLine();
            if (meshTex_[i] < 0 && m.hasColour) {
                ImGui::ColorButton("##col", ImVec4(m.colour[0], m.colour[1], m.colour[2], 1), ImGuiColorEditFlags_NoTooltip,
                                   ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()));
                ImGui::SameLine();
            }
            ImGui::Text("%s  %zu tris", m.material.empty() ? ("mesh " + std::to_string(i)).c_str() : m.material.c_str(), m.indices.size() / 3);
            if (ImGui::BeginItemTooltip()) {
                ImGui::Text("mesh %zu in %s\n%zu vertices, layout %s", i, groupLabel(size_t(m.group)).c_str(), m.vertices.size(), layoutText(m).c_str());
                if (m.textures.empty()) ImGui::TextUnformatted("No textures bound.");
                for (int t : m.textures) ImGui::BulletText("%s", shortTex(model_.textures[size_t(t)]).c_str());
                for (auto& c : m.constants)
                    ImGui::Text("%s = %.3g %.3g %.3g %.3g", c.name.c_str(), c.value[0], c.value[1], c.value[2], c.value[3]);
                ImGui::EndTooltip();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string cur = meshTex_[i] < 0 ? "(none)" : shortTex(model_.textures[size_t(meshTex_[i])]);
            ImGui::BeginDisabled(m.layout.uvSets.empty());
            if (ImGui::BeginCombo("##tex", cur.c_str())) {
                if (ImGui::Selectable("(none)", meshTex_[i] < 0)) { meshTex_[i] = -1; applyMeshState(); }
                // Textures bound to this draw first, then the rest of the model's.
                auto item = [&](size_t t) {
                    ImGui::PushID(int(t));
                    if (ImGui::Selectable(shortTex(model_.textures[t]).c_str(), meshTex_[i] == int(t))) { meshTex_[i] = int(t); applyMeshState(); }
                    ImGui::PopID();
                };
                for (int t : m.textures) item(size_t(t));
                if (!m.textures.empty()) ImGui::Separator();
                for (size_t t = 0; t < model_.textures.size(); ++t)
                    if (std::find(m.textures.begin(), m.textures.end(), int(t)) == m.textures.end()) item(t);
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }

        ImGui::SeparatorText("Textures");
        for (size_t i = 0; i < textures_.size(); ++i) {
            auto& t = textures_[i];
            ImGui::PushID(int(i) + 10000);
            if (t.gl) {
                ImGui::Image(t.gl->imgui(), ImVec2(em(40), em(40)));
                ImGui::SameLine();
            }
            ImGui::BeginGroup();
            ImGui::TextUnformatted(shortTex(t.name).c_str());
            if (!t.error.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", t.error.c_str());
            else if (ImGui::SmallButton("Open")) app.openTexture(t.name);
            ImGui::EndGroup();
            ImGui::PopID();
        }

        ImGui::SeparatorText("Skeleton");
        if (model_.bones.empty()) ImGui::TextDisabled("No bones.");
        else drawBoneTree(-1, 0);

        if (!model_.materials.empty()) {
            ImGui::SeparatorText("Materials");
            for (auto& m : model_.materials) ImGui::BulletText("%s", m.c_str());
        }
    }

    // ---- animation ----------------------------------------------------------------

    void computeBindPose() {
        size_t n = model_.bones.size();
        bindWorld_.assign(n, identity4());
        bindInverse_.assign(n, identity4());
        const float one[3] = {1, 1, 1};
        for (size_t i = 0; i < n; ++i) {
            const auto& b = model_.bones[i];
            Mat4 local = trs(b.local, b.rotation, one);
            bindWorld_[i] = b.parent >= 0 && size_t(b.parent) < i ? mul4(bindWorld_[size_t(b.parent)], local) : local;
            if (b.parent >= 0 && size_t(b.parent) >= i) bindWorld_[i] = trs(b.world, b.rotation, one); // parent listed later
            bindInverse_[i] = rigidInverse(bindWorld_[i]);
        }
    }

    void loadAnimation(const std::string& name) {
        const ModelInfo* info = catalog_ ? catalog_->anim(name) : nullptr;
        if (!info) return;
        AssetLoc loc = info->locs.front();
        pendingName_ = name;
        animError_.clear();
        pending_ = std::async(std::launch::async, [loc] {
            CaffCache cache;
            caff::Caff& c = cache.get(loc);
            return std::make_shared<model::Animation>(model::parseAnimation(c, loc.symbol));
        });
    }

    // Track of each model bone, matched by name.
    void bindTracks() {
        trackOf_.assign(model_.bones.size(), -1);
        matched_ = 0;
        std::map<std::string, int> byName;
        for (size_t t = 0; t < anim_->boneNames.size(); ++t) byName.emplace(anim_->boneNames[t], int(t));
        for (size_t b = 0; b < model_.bones.size(); ++b) {
            auto it = byName.find(model_.bones[b].name);
            if (it != byName.end()) {
                trackOf_[b] = it->second;
                ++matched_;
            }
        }
    }

    void updateAnimation() {
        if (pending_.valid() && pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                anim_ = pending_.get();
                animName_ = pendingName_;
                bindTracks();
                animTime_ = 0;
                playing_ = true;
            } catch (std::exception& e) {
                animError_ = e.what();
                anim_.reset();
            }
            pendingName_.clear();
        }
        if (!anim_ || !view_) return;
        if (playing_) {
            animTime_ += ImGui::GetIO().DeltaTime * speed_;
            float len = std::max(anim_->duration, 1e-3f);
            if (animTime_ > len) {
                if (loop_) animTime_ = std::fmod(animTime_, len);
                else {
                    animTime_ = len;
                    playing_ = false;
                }
            }
        }
        applyPose(animTime_);
    }

    // Model bones posed at time t: matched bones take the animation's rotation and scale and add
    // its translation to their bind offset; the others keep the bind pose.
    void applyPose(float t) {
        const auto& a = *anim_;
        size_t n = model_.bones.size();
        if (a.frames <= 0 || a.keys.empty() || n == 0) return;
        // Stored frames are not always evenly spaced (the last one sits on the final authored frame).
        int f0 = 0, f1 = 0;
        float w = 0;
        if (a.frameTimes.size() == size_t(a.frames) && a.frames > 1) {
            auto it = std::upper_bound(a.frameTimes.begin(), a.frameTimes.end(), t);
            f1 = std::clamp(int(it - a.frameTimes.begin()), 1, a.frames - 1);
            f0 = f1 - 1;
            float span = a.frameTimes[size_t(f1)] - a.frameTimes[size_t(f0)];
            w = span > 0 ? std::clamp((t - a.frameTimes[size_t(f0)]) / span, 0.0f, 1.0f) : 0.0f;
        }
        std::vector<Mat4> world(n);
        std::vector<bool> done(n, false);
        std::function<void(size_t)> solve = [&](size_t i) {
            if (done[i]) return;
            const auto& b = model_.bones[i];
            float tr[3], q[4], s[3] = {1, 1, 1};
            std::copy(b.local, b.local + 3, tr);
            std::copy(b.rotation, b.rotation + 4, q);
            int k = trackOf_[i];
            if (k >= 0 && uint32_t(k) < a.bones) {
                const auto& k0 = a.keys[size_t(f0) * a.bones + size_t(k)];
                const auto& k1 = a.keys[size_t(f1) * a.bones + size_t(k)];
                float dot = 0;
                for (int c = 0; c < 4; ++c) dot += k0.rotation[c] * k1.rotation[c];
                float sign = dot < 0 ? -1.0f : 1.0f;
                for (int c = 0; c < 4; ++c) q[c] = k0.rotation[c] * (1 - w) + sign * k1.rotation[c] * w;
                for (int c = 0; c < 3; ++c) {
                    tr[c] += k0.translation[c] * (1 - w) + k1.translation[c] * w;
                    s[c] = k0.scale[c] * (1 - w) + k1.scale[c] * w;
                }
            }
            Mat4 local = trs(tr, q, s);
            done[i] = true;
            if (b.parent >= 0 && size_t(b.parent) < n && size_t(b.parent) != i) {
                solve(size_t(b.parent));
                world[i] = mul4(world[size_t(b.parent)], local);
            } else {
                world[i] = local;
            }
        };
        for (size_t i = 0; i < n; ++i) solve(i);
        skin_.resize(n * 16);
        boneWorld_.resize(n * 3);
        for (size_t i = 0; i < n; ++i) {
            Mat4 m = mul4(world[i], bindInverse_[i]);
            std::copy(m.begin(), m.end(), &skin_[i * 16]);
            boneWorld_[i * 3] = world[i][12];
            boneWorld_[i * 3 + 1] = world[i][13];
            boneWorld_[i * 3 + 2] = world[i][14];
        }
        view_->setPose(&skin_, &boneWorld_);
    }

    void stopAnimation() {
        anim_.reset();
        animName_.clear();
        playing_ = false;
        if (view_) view_->setPose(nullptr, nullptr);
    }

    void drawAnimation() {
        ImGui::SeparatorText("Animation");
        if (model_.bones.empty()) {
            ImGui::TextDisabled("This model has no skeleton.");
            return;
        }
        if (!pendingName_.empty()) ImGui::TextDisabled("Loading %s...", shortAnimName(pendingName_, animPrefix_).c_str());
        if (!animError_.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", animError_.c_str());
        if (anim_) {
            ImGui::TextUnformatted(shortAnimName(animName_, animPrefix_).c_str());
            ImGui::TextDisabled("%d frames (%d stored), %.2f s, %u bones (%d of this model's %zu matched)", anim_->authoredFrames, anim_->frames, anim_->duration,
                                anim_->bones, matched_, model_.bones.size());
            if (ImGui::Button(playing_ ? "Pause" : "Play", ImVec2(em(60), 0))) {
                if (!playing_ && animTime_ >= anim_->duration && !loop_) animTime_ = 0;
                playing_ = !playing_;
            }
            ImGui::SameLine();
            if (ImGui::Button("Bind pose")) stopAnimation();
            if (anim_) {
                ImGui::SameLine();
                ImGui::Checkbox("Loop", &loop_);
                ImGui::SetNextItemWidth(-FLT_MIN);
                char label[48];
                std::snprintf(label, sizeof label, "%.2f / %.2f s", animTime_, anim_->duration);
                if (ImGui::SliderFloat("##time", &animTime_, 0, std::max(anim_->duration, 0.001f), label)) playing_ = false;
                ImGui::SetNextItemWidth(em(120));
                ImGui::SliderFloat("Speed", &speed_, 0.1f, 2.0f, "%.2fx");
                for (auto& w : anim_->warnings) ImGui::TextDisabled("note: %s", w.c_str());
            }
        }
        // Choice of animation: this model's own, or any by search.
        ImGui::SetNextItemWidth(-FLT_MIN);
        inputText("##animfilter", animFilter_);
        if (animFilter_.empty() && !ImGui::IsItemActive()) {
            ImVec2 pos = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + ImGui::GetStyle().FramePadding.x, pos.y + ImGui::GetStyle().FramePadding.y),
                                                ImGui::GetColorU32(ImGuiCol_TextDisabled), ownAnims_.empty() ? "Search all animations..." : "Filter...");
        }
        helpMarker("Lists the animations named after this model. Type to filter; with no match it searches every animation in the game "
                   "(bones are matched by name, so other characters' animations work only partly).");
        std::vector<const std::string*> shown;
        std::string needle = lower(animFilter_);
        for (auto& n : ownAnims_)
            if (needle.empty() || lower(n).find(needle) != std::string::npos) shown.push_back(&n);
        bool global = shown.empty() && !needle.empty() && catalog_;
        if (global)
            for (auto& a : catalog_->anims)
                if (lower(a.name).find(needle) != std::string::npos) shown.push_back(&a.name);
        if (shown.empty()) {
            ImGui::TextDisabled(ownAnims_.empty() ? "No animations are named after this model; search to pick any." : "No match.");
            return;
        }
        ImGui::TextDisabled("%zu animation(s)%s", shown.size(), global ? " in the whole game" : "");
        float rowH = ImGui::GetTextLineHeightWithSpacing();
        ImGui::BeginChild("##anims", ImVec2(0, std::min(rowH * float(shown.size()) + em(8), em(260))), ImGuiChildFlags_Borders);
        ImGuiListClipper clip;
        clip.Begin(int(shown.size()), rowH);
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const std::string& n = *shown[size_t(i)];
                ImGui::PushID(i);
                if (ImGui::Selectable(shortAnimName(n, global ? "" : animPrefix_).c_str(), n == animName_)) loadAnimation(n);
                if (ImGui::BeginItemTooltip()) {
                    ImGui::TextUnformatted(n.c_str());
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
        ImGui::EndChild();
    }

    // Detail level and switches (the game's own visibility rules), plus per-group overrides.
    void drawParts() {
        ImGui::SeparatorText("Parts");
        bool changed = false;
        if (model_.lodCount > 1) {
            ImGui::SetNextItemWidth(em(120));
            changed |= ImGui::SliderInt("Detail level", &lod_, 0, model_.lodCount - 1);
            helpMarker("0 is the most detailed. The game switches to lower levels with distance.");
        }
        for (auto& [id, states] : model_.switches) {
            ImGui::PushID(id);
            ImGui::SetNextItemWidth(em(200));
            std::string cur = switchLabel(id, switchState_[id]);
            if (ImGui::BeginCombo(("Switch " + std::to_string(id)).c_str(), cur.c_str())) {
                for (int s : states)
                    if (ImGui::Selectable(switchLabel(id, s).c_str(), switchState_[id] == s)) {
                        switchState_[id] = s;
                        changed = true;
                    }
                // A state no node uses: hides every alternative (e.g. goggles taken off).
                if (ImGui::Selectable("off", switchState_[id] == kSwitchOff)) {
                    switchState_[id] = kSwitchOff;
                    changed = true;
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
        if (!model_.switches.empty())
            helpMarker("Switches pick between alternative parts the game shows depending on the situation (a held tool, an open or closed lid...).");
        if (changed) {
            resetGroups();
            applyMeshState();
        }
        if (ImGui::SmallButton("Show everything")) {
            groupShown_.assign(model_.groups.size(), true);
            applyMeshState();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Back to the game's choice")) {
            resetGroups();
            applyMeshState();
        }
        if (!ImGui::TreeNode("##groups", "Draw groups (%zu)", model_.groups.size())) return;
        float rowH = ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("##parts", ImVec2(0, std::min(rowH * float(model_.groups.size()) + em(8), em(220))), ImGuiChildFlags_Borders);
        ImGuiListClipper clip;
        clip.Begin(int(model_.groups.size()), rowH);
        while (clip.Step()) {
            for (int g = clip.DisplayStart; g < clip.DisplayEnd; ++g) {
                const auto& grp = model_.groups[size_t(g)];
                bool on = groupShown_[size_t(g)];
                std::string label = groupLabel(size_t(g)) + "  " + std::to_string(grp.triangles) + " tris###grp" + std::to_string(g);
                ImGui::BeginDisabled(grp.meshes.empty());
                if (ImGui::Checkbox(label.c_str(), &on)) {
                    groupShown_[size_t(g)] = on;
                    applyMeshState();
                }
                ImGui::EndDisabled();
                if (ImGui::BeginItemTooltip()) {
                    ImGui::Text("group %d, %zu mesh(es)", g, grp.meshes.size());
                    for (int n : grp.path) ImGui::BulletText("%s", model_.nodes[size_t(n)].name.c_str());
                    ImGui::EndTooltip();
                }
            }
        }
        ImGui::EndChild();
        ImGui::TreePop();
    }

    void drawLighting() {
        if (!ImGui::TreeNode("Lighting")) return;
        ImGui::SetNextItemWidth(em(140));
        ImGui::Combo("Mode", &opt_.lighting, "Studio (key, fill, rim)\0Headlight\0Unlit\0");
        helpMarker("Studio: three lights around the model. Headlight: one light at the camera. Unlit: flat texture colours.");
        ImGui::BeginDisabled(opt_.lighting == 2);
        ImGui::SetNextItemWidth(em(140));
        ImGui::SliderFloat("Brightness", &opt_.brightness, 0, 2.5f, "%.2f");
        ImGui::SetNextItemWidth(em(140));
        ImGui::SliderFloat("Ambient", &opt_.ambient, 0, 1, "%.2f");
        ImGui::SetNextItemWidth(em(140));
        ImGui::ColorEdit3("Light colour", opt_.lightColour, ImGuiColorEditFlags_NoInputs);
        ImGui::EndDisabled();
        if (opt_.lighting == 0) {
            ImGui::Checkbox("Lights follow the camera", &opt_.lightFollowsCamera);
            ImGui::BeginDisabled(opt_.lightFollowsCamera);
            ImGui::SetNextItemWidth(em(140));
            ImGui::SliderFloat("Direction", &opt_.lightAzimuth, -180, 180, "%.0f deg");
            ImGui::SetNextItemWidth(em(140));
            ImGui::SliderFloat("Height", &opt_.lightElevation, -89, 89, "%.0f deg");
            ImGui::EndDisabled();
        }
        if (ImGui::SmallButton("Reset lighting")) {
            ModelView::Options def;
            opt_.lighting = def.lighting;
            opt_.lightFollowsCamera = def.lightFollowsCamera;
            opt_.lightAzimuth = def.lightAzimuth;
            opt_.lightElevation = def.lightElevation;
            opt_.brightness = def.brightness;
            opt_.ambient = def.ambient;
            std::copy(def.lightColour, def.lightColour + 3, opt_.lightColour);
        }
        ImGui::TreePop();
    }

    void drawBoneTree(int parent, int depth) {
        if (depth > 64) return;
        for (size_t i = 0; i < model_.bones.size(); ++i) {
            const auto& b = model_.bones[i];
            if (b.parent != parent) continue;
            bool hasChildren = false;
            for (auto& c : model_.bones) hasChildren |= c.parent == int(i);
            ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_DefaultOpen | (hasChildren ? 0 : ImGuiTreeNodeFlags_Leaf);
            bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(intptr_t(i + 1)), f, "%s", b.name.c_str());
            if (ImGui::BeginItemTooltip()) {
                ImGui::Text("bone %zu\nposition %.3f %.3f %.3f", i, b.world[0], b.world[1], b.world[2]);
                ImGui::EndTooltip();
            }
            if (open) {
                drawBoneTree(int(i), depth + 1);
                ImGui::TreePop();
            }
        }
    }

    static std::string shortTex(const std::string& n) {
        for (const char* p : {"aid_texture_banjox_", "aid_texture_"})
            if (startsWith(n, p)) return n.substr(std::string(p).size());
        return n;
    }

    void exportObj(App& app) {
        app.openFolderDialog("Export model to", [this, &app](const fs::path& dir) {
            std::string base = ops::safeName(shortModelName(model_.name));
            std::vector<bool> include(model_.meshes.size());
            for (size_t i = 0; i < include.size(); ++i) include[i] = meshShown(i);
            writeText(dir / (base + ".obj"), model::toObj(model_, base + ".mtl", meshTex_, include));
            writeText(dir / (base + ".mtl"), model::toMtl(model_));
            int n = 0;
            for (auto& t : textures_)
                if (!t.img.pixels.empty()) { writeFile(dir / (t.name + ".png"), image::encodePng(t.img)); ++n; }
            app.status("Exported " + base + ".obj with " + std::to_string(n) + " textures to " + toUtf8(dir));
        });
    }

    // ---- FBX export ---------------------------------------------------------------

    void drawFbxExport(App& app) {
        if (!ImGui::BeginPopup("Export FBX")) return;
        ImGui::TextUnformatted("Animations");
        std::string own = "Named after this model (" + std::to_string(ownAnims_.size()) + ")";
        ImGui::BeginDisabled(ownAnims_.empty());
        ImGui::RadioButton(own.c_str(), &fbxAnims_, 1);
        ImGui::EndDisabled();
        std::string cur = anim_ ? "Only the one playing (" + shortAnimName(animName_, animPrefix_) + ")" : std::string("Only the one playing (none)");
        ImGui::BeginDisabled(!anim_);
        ImGui::RadioButton(cur.c_str(), &fbxAnims_, 2);
        ImGui::EndDisabled();
        ImGui::RadioButton("None", &fbxAnims_, 0);
        if (fbxAnims_ == 1 && ownAnims_.empty()) fbxAnims_ = 0;
        if (fbxAnims_ == 2 && !anim_) fbxAnims_ = ownAnims_.empty() ? 0 : 1;
        ImGui::Separator();
        if (ImGui::Button("Save as...")) {
            ImGui::CloseCurrentPopup();
            exportFbx(app);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void exportFbx(App& app) {
        app.saveFileDialog("Export FBX", {{"FBX model", "fbx"}}, [this, &app](const fs::path& picked) {
            fs::path out = picked;
            if (lower(out.extension().string()) != ".fbx") out += ".fbx";
            exportFbxTo(app, out);
        });
    }

    void exportFbxTo(App& app, const fs::path& out) {
        // Everything the worker needs, copied: the tab may change or close meanwhile.
        auto m = std::make_shared<model::Model>(model_);
        for (size_t i = 0; i < m->meshes.size() && i < meshTex_.size(); ++i) m->meshes[i].texture = meshTex_[i];
        std::vector<bool> include(m->meshes.size());
        for (size_t i = 0; i < include.size(); ++i) include[i] = meshShown(i);
        std::vector<std::string> names;
        std::shared_ptr<model::Animation> playing;
        if (fbxAnims_ == 1) names = ownAnims_;
        if (fbxAnims_ == 2) playing = anim_;
        std::string playingName = shortAnimName(animName_, animPrefix_), prefix = animPrefix_;
        auto catalog = catalog_;
        app.runTask("Exporting " + toUtf8(out.filename()), false,
                    [&app, m, include, names, playing, playingName, prefix, catalog, out](Task& t) -> std::function<void()> {
            CaffCache cache;
            std::vector<std::shared_ptr<model::Animation>> anims;
            fbx::Options opt;
            opt.include = include;
            std::vector<std::string> notes;
            for (size_t i = 0; i < names.size() && catalog; ++i) {
                if (t.cancel) return {};
                t.report(0.8 * double(i) / double(names.size()), "Reading " + names[i]);
                const ModelInfo* info = catalog->anim(names[i]);
                if (!info) continue;
                try {
                    const AssetLoc& loc = info->locs.front();
                    anims.push_back(std::make_shared<model::Animation>(model::parseAnimation(cache.get(loc), loc.symbol)));
                    opt.animations.push_back({shortAnimName(names[i], prefix), anims.back().get()});
                } catch (std::exception& e) {
                    notes.push_back(names[i] + ": " + e.what());
                }
            }
            if (playing) opt.animations.push_back({playingName, playing.get()});
            // Textures at full size, straight from the game files.
            opt.texture = [&](const std::string& name) -> std::optional<Bytes> {
                const TextureInfo* ti = catalog ? catalog->texture(name) : nullptr;
                if (!ti) return std::nullopt;
                try {
                    ops::DecodedTexture d = decodeCatalogTexture(*ti, cache);
                    return image::encodePng(texture::toRgba(d.header, d.levels[0], 0));
                } catch (std::exception& e) {
                    notes.push_back(name + ": " + e.what());
                    return std::nullopt;
                }
            };
            t.report(0.85, "Writing " + toUtf8(out.filename()));
            fbx::Report r;
            Bytes data = fbx::write(*m, opt, &r);
            writeFile(out, data);
            for (auto& n : r.notes) notes.push_back(n);
            return [&app, r, notes, out] {
                for (auto& n : notes) app.log("FBX export: " + n);
                app.status("Exported " + toUtf8(out.filename()) + ": " + std::to_string(r.meshes) + " meshes, " + std::to_string(r.bones) + " bones, " +
                           std::to_string(r.textures) + " textures, " + std::to_string(r.animations) + " animations");
            };
        });
    }

    // ---- material colours -------------------------------------------------------
    // Colour constants (c:basecolour, c:velvetColour...) that share a material, a name and a value
    // are edited together: every detail level repeats the material with its own copy of the value.
    struct ColourGroup {
        std::string material, name;
        std::vector<std::pair<size_t, size_t>> uses; // mesh, constant
        std::array<float, 4> original;
    };

    void collectColours() {
        std::map<std::string, size_t> byKey;
        for (size_t i = 0; i < model_.meshes.size(); ++i) {
            const auto& cs = model_.meshes[i].constants;
            for (size_t k = 0; k < cs.size(); ++k) {
                if (cs[k].symbol < 0 || lower(cs[k].name).find("colour") == std::string::npos) continue;
                std::string key = model_.meshes[i].material + '\x1f' + cs[k].name + '\x1f' +
                                  std::string(reinterpret_cast<const char*>(cs[k].value.data()), 16);
                auto [it, added] = byKey.emplace(key, colourGroups_.size());
                if (added) colourGroups_.push_back({model_.meshes[i].material, cs[k].name, {}, cs[k].value});
                colourGroups_[it->second].uses.push_back({i, k});
            }
        }
    }

    const std::array<float, 4>& colourOf(const ColourGroup& g) const {
        return model_.meshes[g.uses[0].first].constants[g.uses[0].second].value;
    }

    void setColour(size_t gi, const std::array<float, 4>& v) {
        ColourGroup& g = colourGroups_[gi];
        for (auto [mi, ki] : g.uses) {
            auto& mesh = model_.meshes[mi];
            mesh.constants[ki].value = v;
            if (g.name == "c:basecolour") std::copy(v.begin(), v.end(), mesh.colour);
            if (g.name == "c:specularColour") std::copy(v.begin(), v.begin() + 3, mesh.specular);
        }
        if (v == g.original) colourEdited_.erase(gi);
        else colourEdited_.insert(gi);
        if (view_) applyMeshState();
    }

    void drawColours() {
        if (colourGroups_.empty()) return;
        ImGui::SeparatorText("Material colours");
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("A change applies to every mesh and detail level using that material colour. Save (Ctrl+S) "
                           "writes it into the bundle; the viewport shows base colour changes.");
        ImGui::PopStyleColor();
        for (size_t gi = 0; gi < colourGroups_.size(); ++gi) {
            const ColourGroup& g = colourGroups_[gi];
            bool shown = false;
            for (auto [mi, ki] : g.uses) shown |= meshShown(mi);
            if (!shown && !allColours_) continue;
            ImGui::PushID(int(gi));
            std::array<float, 4> v = colourOf(g);
            if (ImGui::ColorEdit3("##c", v.data(), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
                setColour(gi, v);
            ImGui::SameLine();
            std::string what = (g.material.empty() ? "mesh " + std::to_string(g.uses[0].first) : g.material) + "  " +
                               (startsWith(g.name, "c:") ? g.name.substr(2) : g.name);
            ImGui::TextUnformatted(what.c_str());
            if (ImGui::BeginItemTooltip()) {
                ImGui::Text("%.3f %.3f %.3f (was %.3f %.3f %.3f)\n%zu mesh(es)", v[0], v[1], v[2], g.original[0], g.original[1], g.original[2],
                            g.uses.size());
                ImGui::EndTooltip();
            }
            if (colourEdited_.count(gi)) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Revert")) setColour(gi, g.original);
            }
            ImGui::PopID();
        }
        ImGui::Checkbox("Include hidden parts and detail levels", &allColours_);
    }

    // ---- replacing geometry ---------------------------------------------------------
    // Imports and swaps are applied to a copy of the first bundle for the preview, and replayed on
    // every copy of the model when saving.
    void drawReplace(App& app) {
        ImGui::SeparatorText("Replace");
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Import OBJ: objects named like the export's (group0_mesh3) edit that mesh in place when the vertex and "
                           "triangle counts match, otherwise replace its geometry (skinning comes from the nearest original vertex); "
                           "an object with no faces hides the mesh. Replace copies another game model into this one's place.");
        ImGui::PopStyleColor();
        bool coloursPending = !colourEdited_.empty();
        ImGui::BeginDisabled(coloursPending);
        if (ImGui::Button("Import OBJ...")) {
            app.openFileDialog("Import OBJ", {{"Wavefront OBJ", "obj"}}, [this, &app](const fs::path& p) {
                std::string text;
                try {
                    text = readText(p);
                } catch (std::exception& e) {
                    app.error(e.what());
                    return;
                }
                std::string name = toUtf8(p.filename());
                deferred_ = [this, &app, name, text] {
                    bool ok = preview(app, [&](caff::Caff& c, uint32_t sym) {
                        model::ImportReport r = model::importObj(c, sym, text);
                        std::vector<std::string> notes{name + ": " + std::to_string(r.edited) + " mesh(es) edited, " + std::to_string(r.rebuilt) +
                                                       " rebuilt, " + std::to_string(r.hidden) + " hidden"};
                        notes.insert(notes.end(), r.notes.begin(), r.notes.end());
                        return notes;
                    });
                    if (ok) pendingObjs_.push_back(text);
                };
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("Replace with another model...")) {
            swapFilter_.clear();
            ImGui::OpenPopup("##swap");
        }
        ImGui::EndDisabled();
        if (coloursPending) ImGui::TextDisabled("Save or revert the colour changes first.");
        if (ImGui::BeginPopup("##swap")) {
            ImGui::TextUnformatted("Replace this model with:");
            ImGui::SetNextItemWidth(em(320));
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            inputText("##filter", swapFilter_);
            std::string f = lower(swapFilter_);
            if (ImGui::BeginChild("##models", ImVec2(em(420), em(300)))) {
                int listed = 0;
                if (catalog_)
                    for (auto& m : catalog_->models) {
                        if (m.name == info_.name || (!f.empty() && lower(m.name).find(f) == std::string::npos)) continue;
                        if (++listed > 500) {
                            ImGui::TextDisabled("... type to narrow the list");
                            break;
                        }
                        if (ImGui::Selectable(shortModelName(m.name).c_str())) {
                            std::string name = m.name;
                            deferred_ = [this, &app, name] {
                                if (preview(app, [&](caff::Caff& c, uint32_t sym) { return copyFrom(c, sym, info_.locs.front(), name).notes; })) {
                                    pendingSwap_ = name;
                                    pendingObjs_.clear(); // the new model replaces any imported geometry
                                }
                            };
                            ImGui::CloseCurrentPopup();
                        }
                    }
                if (!catalog_) ImGui::TextDisabled("The model list is still being built.");
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        if (!pendingSwap_.empty()) ImGui::BulletText("Replaced by %s", shortModelName(pendingSwap_).c_str());
        if (!pendingObjs_.empty()) ImGui::BulletText("%zu OBJ import(s)", pendingObjs_.size());
        if (!replaceNotes_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            for (auto& n : replaceNotes_) ImGui::TextWrapped("%s", n.c_str());
            ImGui::PopStyleColor();
        }
        if (dirty()) {
            if (ImGui::Button("Discard changes")) deferred_ = [this] { discard(); };
            ImGui::SameLine();
            ImGui::TextDisabled("Not saved yet: Ctrl+S writes them into the bundle(s).");
        }
    }

    // Applies `op` to a copy of the previewed CAFF; on success it becomes the preview and is shown.
    bool preview(App& app, const std::function<std::vector<std::string>(caff::Caff&, uint32_t)>& op) {
        const AssetLoc& loc = info_.locs.front();
        try {
            caff::Caff trial = preview_ ? *preview_ : cache_.get(loc);
            std::vector<std::string> notes = op(trial, loc.symbol);
            preview_ = std::make_unique<caff::Caff>(std::move(trial));
            replaceNotes_.insert(replaceNotes_.end(), notes.begin(), notes.end());
            loadModel(*preview_, loc.symbol);
            app.status(notes.empty() ? "Model replaced" : notes.front());
            return true;
        } catch (std::exception& e) {
            app.error(std::string("could not apply it: ") + e.what());
            return false;
        }
    }

    // Copies the model `name` over the one at `symbol` in `c` (a copy of the model at `at`).
    model::CopyResult copyFrom(caff::Caff& c, uint32_t symbol, const AssetLoc& at, const std::string& name) {
        const ModelInfo* src = catalog_ ? catalog_->model(name) : nullptr;
        if (!src || src->locs.empty()) fail(name + " is not in the model list");
        // A copy in the same file shares its pools: copy within the CAFF.
        for (auto& l : src->locs)
            if (l.file == at.file && l.entry == at.entry) return model::copyModel(c, symbol, c, l.symbol, catalogTextures());
        caff::Caff& sc = cache_.get(src->locs.front());
        return model::copyModel(c, symbol, sc, src->locs.front().symbol, catalogTextures());
    }

    // Textures for copyModel, from wherever the catalog found them (whole or mip half in a bundle,
    // the streamed top level in a stream bundle). Files are read once per copy, not through cache_,
    // whose entries could be dropped while copyModel still uses them.
    model::FindTexture catalogTextures() const {
        auto catalog = catalog_;
        auto caffs = std::make_shared<std::map<std::pair<fs::path, int>, std::shared_ptr<const caff::Caff>>>();
        auto indexes = std::make_shared<std::map<fs::path, std::vector<streambundle::IndexEntry>>>();
        auto indexOf = [indexes](const fs::path& f) -> const std::vector<streambundle::IndexEntry>& {
            auto it = indexes->find(f);
            if (it == indexes->end()) {
                std::vector<streambundle::IndexEntry> idx;
                streambundle::readIndex(f, idx);
                it = indexes->emplace(f, std::move(idx)).first;
            }
            return it->second;
        };
        return [catalog, caffs, indexOf](const std::string& name) -> std::optional<model::TextureSource> {
            const TextureInfo* ti = catalog ? catalog->texture(name) : nullptr;
            if (!ti || ti->full.empty()) return std::nullopt;
            const AssetLoc& loc = ti->full.front();
            auto& c = (*caffs)[{loc.file, loc.entry}];
            if (!c) {
                if (loc.entry < 0) {
                    Loaded l = loadAsset(loc.file);
                    if (!l.caff) return std::nullopt;
                    c = std::make_shared<caff::Caff>(std::move(*l.caff));
                } else {
                    auto& idx = indexOf(loc.file);
                    if (size_t(loc.entry) >= idx.size()) return std::nullopt;
                    c = std::make_shared<caff::Caff>(caff::Caff::parse(streambundle::readEntry(loc.file, idx[size_t(loc.entry)])));
                }
            }
            model::TextureSource src{c, loc.symbol, std::nullopt};
            if (!ti->tops.empty()) {
                const AssetLoc& t = ti->tops.front();
                auto& idx = indexOf(t.file);
                if (size_t(t.entry) >= idx.size()) return std::nullopt;
                src.top = streambundle::Entry{idx[size_t(t.entry)].id, streambundle::readEntry(t.file, idx[size_t(t.entry)])};
            }
            return src;
        };
    }

    // Returns the stream bundle entries (streamed texture levels) the destination's stream bundle needs.
    std::vector<streambundle::Entry> applyReplacements(caff::Caff& c, uint32_t symbol, const AssetLoc& at) {
        std::vector<streambundle::Entry> entries;
        if (!pendingSwap_.empty()) entries = copyFrom(c, symbol, at, pendingSwap_).streamEntries;
        for (auto& obj : pendingObjs_) model::importObj(c, symbol, obj);
        return entries;
    }

    // Back to what is on disk.
    void discard() {
        preview_.reset();
        pendingObjs_.clear();
        pendingSwap_.clear();
        replaceNotes_.clear();
        cache_ = CaffCache();
        loadModel(cache_.get(info_.locs.front()), info_.locs.front().symbol);
    }

    // Writes the edited colours into one copy of the model (`symbol` is its .data part in `c`).
    void patchColours(caff::Caff& c, uint32_t symbol) const {
        model::Model m = model::parse(c, symbol);
        if (m.meshes.size() != model_.meshes.size()) fail("the model there differs from the one shown");
        std::map<int, Bytes> data;
        for (size_t gi : colourEdited_)
            for (auto [mi, ki] : colourGroups_[gi].uses) {
                const auto& k = m.meshes[mi].constants.at(ki);
                if (k.name != colourGroups_[gi].name || k.symbol < 0) fail("the model there differs from the one shown");
                Bytes& d = data.try_emplace(k.symbol, c.symbolData(size_t(k.symbol))).first->second;
                if (k.offset + 16 > d.size()) fail("material constant outside its symbol");
                const auto& v = model_.meshes[mi].constants[ki].value;
                for (int q = 0; q < 4; ++q) {
                    uint32_t bits;
                    std::memcpy(&bits, &v[size_t(q)], 4);
                    putBe32(&d[k.offset + 4 * uint32_t(q)], bits);
                }
            }
        for (auto& [s, d] : data) c.replaceSymbolData(size_t(s), d);
    }

public:
    bool dirty() const override { return !colourEdited_.empty() || !pendingObjs_.empty() || !pendingSwap_.empty(); }
    bool canSave() const override { return dirty(); }

    // Every bundle holding the model gets the change (some models ship in several); "save as"
    // writes only the first.
    bool save(App& app, const fs::path* saveAs) override {
        try {
            std::vector<AssetLoc> locs = info_.locs;
            if (saveAs) locs.resize(1);
            int streamFiles = 0;
            for (const AssetLoc& loc : locs) {
                fs::path target = saveAs ? *saveAs : loc.file;
                Loaded l = loadAsset(loc.file);
                if (loc.entry < 0) {
                    if (!l.caff) fail(toUtf8(loc.file) + " is not a bundle");
                    auto streamed = applyReplacements(*l.caff, loc.symbol, loc);
                    patchColours(*l.caff, loc.symbol);
                    // Copied textures' streamed levels go into the bundle's stream bundle, written first.
                    if (!streamed.empty() && !saveAs) {
                        fs::path sp = model::pairedStreamBundle(loc.file);
                        Loaded sl = loadAsset(sp);
                        if (!sl.sb) fail(toUtf8(sp) + " (the bundle's stream bundle) is missing; the copied textures need it");
                        if (model::addStreamEntries(*sl.sb, streamed)) {
                            app.backupBeforeWrite(sp);
                            writeFile(sp, sl.sb->serialize());
                            ++streamFiles;
                        }
                    }
                    if (!saveAs) app.backupBeforeWrite(loc.file);
                    saveCaff(target, *l.caff);
                } else {
                    if (!l.sb || size_t(loc.entry) >= l.sb->entries.size()) fail(toUtf8(loc.file) + ": stream bundle entry missing");
                    auto& e = l.sb->entries[size_t(loc.entry)];
                    caff::Caff c = caff::Caff::parse(e.data);
                    auto streamed = applyReplacements(c, loc.symbol, loc);
                    patchColours(c, loc.symbol);
                    e.data = c.serialize();
                    model::addStreamEntries(*l.sb, streamed);
                    if (!saveAs) app.backupBeforeWrite(loc.file);
                    writeFile(target, l.sb->serialize());
                }
            }
            for (size_t gi : colourEdited_) colourGroups_[gi].original = colourOf(colourGroups_[gi]);
            colourEdited_.clear();
            bool replaced = !pendingObjs_.empty() || !pendingSwap_.empty();
            if (!saveAs) {
                // What is on disk now matches the preview.
                preview_.reset();
                pendingObjs_.clear();
                pendingSwap_.clear();
                replaceNotes_.clear();
                cache_ = CaffCache();
            }
            app.status(std::string(replaced ? "Saved the model" : "Saved material colours") + " to " + std::to_string(locs.size() + size_t(streamFiles)) +
                       " file(s)" + (streamFiles ? " (with the textures it needs)" : ""));
            return true;
        } catch (std::exception& e) {
            app.error(std::string("save failed: ") + e.what());
            return false;
        }
    }

private:
    std::vector<ColourGroup> colourGroups_;
    std::set<size_t> colourEdited_;
    bool allColours_ = false;
    // Replacements not saved yet: a model copied in, then OBJ imports, in order.
    std::unique_ptr<caff::Caff> preview_;
    std::string pendingSwap_;
    std::vector<std::string> pendingObjs_;
    std::vector<std::string> replaceNotes_;
    std::string swapFilter_;
    std::function<void()> deferred_; // runs at the start of the next frame

    ModelInfo info_;
    std::shared_ptr<const Catalog> catalog_;
    fs::path root_;
    CaffCache cache_;
    model::Model model_;
    std::vector<LoadedTex> textures_;
    std::vector<int> meshTex_;
    std::vector<bool> meshVisible_;
    std::vector<bool> groupShown_;
    bool hasFur_ = false;
    int lod_ = 0;
    std::map<int, int> switchState_;
    std::unique_ptr<ModelView> view_;
    ModelView::Options opt_;
    // Animation.
    std::string animPrefix_;
    std::vector<std::string> ownAnims_;
    int fbxAnims_ = 1; // FBX export: 0 none, 1 named after the model, 2 the one playing
    std::string animFilter_, animName_, pendingName_, animError_;
    std::future<std::shared_ptr<model::Animation>> pending_;
    std::shared_ptr<model::Animation> anim_;
    std::vector<int> trackOf_;
    int matched_ = 0;
    float animTime_ = 0, speed_ = 1;
    bool playing_ = false, loop_ = true;
    std::vector<Mat4> bindWorld_, bindInverse_;
    std::vector<float> skin_, boneWorld_;
    bool startupChecked_ = false;
    float panelFrac_ = 0.28f;
};

} // namespace

std::unique_ptr<Document> makeModelDocument(const ModelInfo& m, std::shared_ptr<const Catalog> catalog, const fs::path& root) {
    return std::make_unique<ModelDocument>(m, std::move(catalog), root);
}

} // namespace nut::gui
