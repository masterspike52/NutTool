// Game script tab: the text of a scene script, actor goal list, script table or state table, with
// what it points at and what points at it. Saving writes every copy in the game's bundles.
#include "docs.hpp"

#include "assetdb.hpp"
#include "container.hpp"
#include "objparams.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>

namespace nut::gui {

void ScriptEditor::load(std::string initial, Build b) {
    build = std::move(b);
    text = saved = std::move(initial);
    check();
}

void ScriptEditor::loadAsset(const std::string& assetName, const Bytes& data, const gamescript::NameLookup& names) {
    if (auto kind = gamescript::kindOf(assetName)) {
        gamescript::Kind k = *kind;
        load(gamescript::toText(k, data, names), [k](const std::string& t) { return gamescript::fromText(k, t); });
    } else if (objparams::isObjParams(data)) {
        load(objparams::toText(data, names), [data](const std::string& t) { return objparams::fromText(data, t); });
    } else {
        fail(assetName + " is neither a game script nor an objparams record");
    }
}

void ScriptEditor::check() {
    try {
        bytes = build(text).size();
        problem.clear();
    } catch (std::exception& e) {
        problem = e.what();
    }
}

bool ScriptEditor::draw(const char* id, const ImVec2& size) {
    ImVec2 box(size.x, std::max(em(80), size.y - ImGui::GetTextLineHeightWithSpacing()));
    bool changed = inputTextMultiline(id, text, box, ImGuiInputTextFlags_AllowTabInput);
    if (changed) check();
    if (!problem.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", problem.c_str());
    else ImGui::TextDisabled("%s%zu bytes", modified() ? "edited, builds to " : "", bytes);
    return changed;
}

void assetList(App& app, const char* id, const std::vector<std::string>& names, float height) {
    ImGui::BeginChild(id, ImVec2(0, height), ImGuiChildFlags_Borders);
    if (names.empty()) ImGui::TextDisabled("none found");
    auto cat = app.catalog();
    ImGuiListClipper clip;
    clip.Begin(int(names.size()));
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            const std::string& n = names[size_t(i)];
            ImGui::PushID(i);
            bool known = cat && (cat->script(n) || cat->part(n) || cat->model(n) || cat->texture(ops::fullName(n)) || cat->audioBank(n));
            if (!known) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(n.c_str()) && known) app.openAsset(n);
            if (!known) ImGui::PopStyleColor();
            if (ImGui::BeginItemTooltip()) {
                auto aid = assetdb::idFromName(n);
                ImGui::Text("%s\n%s%s", n.c_str(), aid ? (assetdb::typeName(uint8_t(*aid >> 24)) + ", id " + hex32(*aid)).c_str() : "",
                            known ? "\nClick to open it" : "\nNo tab for this kind of asset: open its bundle to see it");
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

namespace {

class ScriptDocument : public Document {
public:
    ScriptDocument(ScriptInfo info, std::shared_ptr<const Catalog> cat, const fs::path& root) : info_(std::move(info)), cat_(std::move(cat)), root_(root) {
        if (info_.locs.empty()) fail("no copies of " + info_.name + " were found");
        path = info_.locs.front().file;
        CaffCache cache;
        caff::Caff& c = cache.get(info_.locs.front());
        uint32_t si = info_.locs.front().symbol;
        if (si >= c.symbols.size() || assetdb::baseAssetName(c.symbolName(si)) != info_.name)
            fail(toUtf8(path) + " changed since it was indexed; reopen the assets folder to index it again");
        Bytes data = c.symbolData(si);
        editor_.loadAsset(info_.name, data, lookup());
        findUses(data);
    }

    std::string title() const override { return info_.name; }
    bool dirty() const override { return editor_.modified(); }
    bool canSave() const override { return true; }

    void draw(App& app) override {
        ImGui::TextUnformatted(info_.name.c_str());
        ImGui::SameLine();
        size_t files = copies();
        ImGui::TextDisabled("%s, %zu %s", info_.objParams ? "vehicle part" : gamescript::kindLabel(info_.kind), files, files == 1 ? "bundle" : "bundles");
        if (ImGui::BeginItemTooltip()) {
            for (auto& l : info_.locs) ImGui::TextUnformatted((toUtf8Generic(fs::relative(l.file, root_)) + (l.entry >= 0 ? "  entry " + std::to_string(l.entry + 1) : "")).c_str());
            ImGui::EndTooltip();
        }
        ImGui::BeginDisabled(!editor_.problem.empty() || !editor_.modified());
        if (ImGui::Button("Save to every copy")) save(app, nullptr);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!editor_.modified());
        if (ImGui::Button("Revert")) {
            editor_.text = editor_.saved;
            editor_.check();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Export text...")) {
            std::string text = "# " + info_.name + "\n" + editor_.text;
            app.saveFileDialog("Export script text", {{"Text", "txt"}}, [&app, text](const fs::path& p) {
                writeText(p, text);
                app.status("Exported " + toUtf8(p));
            }, fromUtf8(info_.name + ".txt"));
        }
        ImGui::SameLine();
        if (ImGui::Button("Import text...")) {
            app.openFileDialog("Import script text", {{"Text", "txt"}, {"All files", "*"}}, [this, &app](const fs::path& p) {
                editor_.text = readText(p);
                editor_.check();
                app.status(editor_.problem.empty() ? "Loaded " + toUtf8(p) + " (save to apply it)" : "Loaded " + toUtf8(p) + ": " + editor_.problem);
            });
        }
        helpMarker(info_.objParams
                       ? "One field per line, name = value. Fields without a name go by offset; words that are 0 are not listed, "
                         "and adding a line such as 0x3C4 = 1.5 sets one. Values: 12 or -3 (int), 1.5 (float), 0x1A2B3C4D (hex), "
                         "@aid_name (an asset, by name), \"text\" (a string field), <0a0b> (raw bytes), # starts a comment.\n"
                         "The ratings are the parts shop's 0-5 stars, separate from the values the game simulates with (mass, "
                         "health, power). Saving writes every bundle that holds this part (with .bak backups when enabled)."
                       : "One record per line. Values: 12 or -3 (int), 1.5 (float), 0x1A2B3C4D (hex), @aid_name (an asset, by name), "
                         "\"text\" (a string field), <0a0b> (raw bytes), # starts a comment.\n"
                         "Known opcodes and goals must keep their argument count and types' sizes; the line under the editor says "
                         "what is wrong. Saving writes every bundle that holds this asset (with .bak backups when enabled).");

        float h = ImGui::GetContentRegionAvail().y, w = ImGui::GetContentRegionAvail().x;
        float lw = leftFrac_ * w;
        ImGui::BeginChild("##scripttext", ImVec2(lw, h));
        editor_.draw("##script", ImGui::GetContentRegionAvail());
        ImGui::EndChild();
        splitter("##scriptsplit", lw, em(300), w - em(200), h);
        leftFrac_ = lw / w;
        ImGui::BeginChild("##scriptrefs", ImVec2(0, h));
        float lh = (ImGui::GetContentRegionAvail().y - 2 * ImGui::GetTextLineHeightWithSpacing()) / 2;
        ImGui::Text("Uses (%zu)", uses_.size());
        helpMarker("Assets this one points at: every word of its data that is the id of a known asset.");
        assetList(app, "##uses", uses_, lh);
        ImGui::Text("Used by (%zu)", info_.usedBy.size());
        helpMarker("Assets whose data holds this one's id. Scripts that nothing points at are started by the game's code, or not used.");
        assetList(app, "##usedby", info_.usedBy, lh);
        ImGui::EndChild();
    }

    bool save(App& app, const fs::path* saveAs) override {
        try {
            if (saveAs) {
                writeText(*saveAs, "# " + info_.name + "\n" + editor_.text);
                return true;
            }
            Bytes data = editor_.build(editor_.text);
            // Every copy is patched before any file is written, so a failure leaves them all as they were.
            std::map<fs::path, std::vector<AssetLoc>> byFile;
            for (auto& l : info_.locs) byFile[l.file].push_back(l);
            std::vector<std::pair<fs::path, Loaded>> patched;
            for (auto& [file, locs] : byFile) {
                Loaded l = loadAsset(file);
                for (auto& loc : locs) {
                    std::optional<caff::Caff> entry;
                    caff::Caff* c = nullptr;
                    if (loc.entry < 0 && l.caff) {
                        c = &*l.caff;
                    } else if (loc.entry >= 0 && l.sb && size_t(loc.entry) < l.sb->entries.size()) {
                        entry = caff::Caff::parse(l.sb->entries[size_t(loc.entry)].data);
                        c = &*entry;
                    }
                    if (!c || loc.symbol >= c->symbols.size() || assetdb::baseAssetName(c->symbolName(loc.symbol)) != info_.name)
                        fail(toUtf8(file) + " changed since it was indexed; reopen the assets folder to index it again");
                    c->replaceSymbolData(loc.symbol, data);
                    if (entry) l.sb->entries[size_t(loc.entry)].data = entry->serialize();
                }
                l.raw = Bytes();
                patched.emplace_back(file, std::move(l));
            }
            for (auto& [file, l] : patched) {
                app.backupBeforeWrite(file);
                if (l.caff) saveCaff(file, *l.caff);
                else writeFile(file, l.sb->serialize());
            }
            editor_.loadAsset(info_.name, data, lookup());
            findUses(data);
            app.status("Saved " + info_.name + " to " + std::to_string(patched.size()) + (patched.size() == 1 ? " bundle" : " bundles"));
            return true;
        } catch (std::exception& e) {
            app.error(info_.name + ": " + e.what());
            return false;
        }
    }

private:
    gamescript::NameLookup lookup() const {
        auto cat = cat_;
        return [cat](uint32_t id) { return cat ? cat->nameOf(id) : std::string(); };
    }

    void findUses(const Bytes& data) {
        uses_.clear();
        auto names = lookup();
        for (uint32_t id : gamescript::referencedIds(data, names, assetdb::idFromName(info_.name).value_or(0))) uses_.push_back(names(id));
    }

    size_t copies() const {
        std::set<fs::path> files;
        for (auto& l : info_.locs) files.insert(l.file);
        return files.size();
    }

    ScriptInfo info_;
    std::shared_ptr<const Catalog> cat_;
    fs::path root_;
    ScriptEditor editor_;
    std::vector<std::string> uses_;
    float leftFrac_ = 0.68f;
};

} // namespace

std::unique_ptr<Document> makeScriptDocument(const ScriptInfo& s, std::shared_ptr<const Catalog> catalog, const fs::path& root) {
    return std::make_unique<ScriptDocument>(s, std::move(catalog), root);
}

} // namespace nut::gui
