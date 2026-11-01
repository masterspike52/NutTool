#include "app.hpp"
#include "docs.hpp"

#include "assetdb.hpp"
#include "container.hpp"
#include "loctext.hpp"
#include "nbpatch.hpp"
#include "ops.hpp"

#include <glad/glad.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nut::gui {

// ---- helpers -----------------------------------------------------------------

std::string toUtf8(const fs::path& p) {
    auto u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

fs::path fromUtf8(const std::string& s) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size())); }

std::string toUtf8Generic(const fs::path& p) {
    auto u = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::string formatSize(uint64_t b) {
    char t[32];
    if (b < 1024) std::snprintf(t, sizeof t, "%llu B", (unsigned long long)b);
    else if (b < 1024 * 1024) std::snprintf(t, sizeof t, "%.1f KB", b / 1024.0);
    else std::snprintf(t, sizeof t, "%.1f MB", b / (1024.0 * 1024.0));
    return t;
}

GpuImage::~GpuImage() {
    if (tex) glDeleteTextures(1, &tex);
}

namespace {

int inputTextResize(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* s = static_cast<std::string*>(data->UserData);
        s->resize(size_t(data->BufTextLen));
        data->Buf = s->data();
    }
    return 0;
}

} // namespace

bool inputText(const char* label, std::string& s, ImGuiInputTextFlags flags) {
    return ImGui::InputText(label, s.data(), s.capacity() + 1, flags | ImGuiInputTextFlags_CallbackResize, inputTextResize, &s);
}

bool inputTextMultiline(const char* label, std::string& s, const ImVec2& size, ImGuiInputTextFlags flags) {
    return ImGui::InputTextMultiline(label, s.data(), s.capacity() + 1, size, flags | ImGuiInputTextFlags_CallbackResize, inputTextResize, &s);
}

void helpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void splitter(const char* id, float& width, float minWidth, float maxWidth, float height) {
    ImGui::SameLine(0, 0);
    ImGui::InvisibleButton(id, ImVec2(ImGui::GetFontSize() * 0.4f, height));
    if (ImGui::IsItemActive()) width += ImGui::GetIO().MouseDelta.x;
    width = std::clamp(width, minWidth, std::max(minWidth, maxWidth));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2((a.x + b.x) * 0.5f - 1, a.y), ImVec2((a.x + b.x) * 0.5f + 1, b.y), ImGui::GetColorU32(ImGuiCol_SeparatorHovered));
    }
    ImGui::SameLine(0, 0);
}

void hexView(const char* id, const Bytes& data, float height) {
    if (!ImGui::BeginChild(id, ImVec2(0, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::EndChild();
        return;
    }
    int rows = int((data.size() + 15) / 16);
    ImGuiListClipper clip;
    clip.Begin(rows);
    char line[128];
    while (clip.Step()) {
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
            size_t base = size_t(r) * 16;
            int n = std::snprintf(line, sizeof line, "%08zx  ", base);
            for (size_t k = 0; k < 16; ++k) {
                if (base + k < data.size()) n += std::snprintf(line + n, sizeof line - size_t(n), "%02x ", data[base + k]);
                else n += std::snprintf(line + n, sizeof line - size_t(n), "   ");
                if (k == 7) line[n++] = ' ';
            }
            line[n++] = ' ';
            for (size_t k = 0; k < 16 && base + k < data.size(); ++k) {
                uint8_t c = data[base + k];
                line[n++] = (c >= 32 && c < 127) ? char(c) : '.';
            }
            line[n] = 0;
            ImGui::TextUnformatted(line);
        }
    }
    ImGui::EndChild();
}

// ---- construction / settings ------------------------------------------------------

App::App(SDL_Window* window) : window_(window) {
    instance_ = this;
    if (char* pref = SDL_GetPrefPath("NutTool", "NutTool")) {
        settingsPath_ = fromUtf8(pref) / "settings.txt";
        SDL_free(pref);
    }
    loadSettings();
    uiScale_ = 0;
    updateScale();
    if (!root_.empty() && fs::exists(root_)) setAssetsRoot(root_);
    else {
        // Look for an "assets" folder next to the executable or in the working directory.
        const char* base = SDL_GetBasePath();
        for (fs::path cand : {fs::current_path() / "assets", base ? fromUtf8(base) / "assets" : fs::path(), base ? fromUtf8(base) / ".." / "assets" : fs::path()}) {
            if (!cand.empty() && fs::exists(cand / "Debug" / "db_index.txt")) {
                setAssetsRoot(fs::weakly_canonical(cand));
                break;
            }
        }
    }
}

App::~App() {
    for (auto& t : tasks_) {
        t->cancel = true;
        if (t->thread.joinable()) t->thread.join();
    }
    saveSettings();
    instance_ = nullptr;
}

void App::loadSettings() {
    if (settingsPath_.empty() || !fs::exists(settingsPath_)) return;
    try {
        for (auto& line : split(readText(settingsPath_), '\n')) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
            if (k == "assets") root_ = fromUtf8(v);
            else if (k == "backups") backups_ = v == "1";
            else if (k == "dark") darkTheme_ = v == "1";
            else if (k == "browserWidth") browserWidth_ = std::max(150.0f, std::stof(v));
            else if (k == "extractOut") extractOut_ = v;
            else if (k == "lastDir") lastDir_ = fromUtf8(v);
            else if (k == "modAuthor") mod_.author = v;
            else if (k == "modOriginal") modOriginal_ = v;
            else if (k == "zoom") userZoom_ = std::clamp(std::stof(v), 0.5f, 3.0f);
        }
    } catch (...) {
    }
}

void App::saveSettings() {
    if (settingsPath_.empty()) return;
    std::string s = "assets=" + toUtf8(root_) + "\nbackups=" + (backups_ ? "1" : "0") + "\ndark=" + (darkTheme_ ? "1" : "0") +
                    "\nbrowserWidth=" + std::to_string(int(browserWidth_)) + "\nextractOut=" + extractOut_ + "\nlastDir=" + toUtf8(lastDir_) + "\nzoom=" + std::to_string(userZoom_) +
                    "\nmodAuthor=" + mod_.author + "\nmodOriginal=" + modOriginal_ + "\n";
    try { writeText(settingsPath_, s); } catch (...) {}
}

void App::setAssetsRoot(const fs::path& p) {
    root_ = p;
    catalog_.reset();
    scanAssets();
    collectGlyphs();
    buildCatalog();
    saveSettings();
    status("Assets folder: " + toUtf8(root_));
}

// ---- asset scanning ---------------------------------------------------------------

void App::scanAssets() {
    groups_.clear();
    labels_.clear();
    if (root_.empty() || !fs::exists(root_)) return;
    assetdb::Names names;
    try {
        for (auto& e : assetdb::readDbIndex(root_ / "Debug" / "db_index.txt")) names.add(e.name);
    } catch (...) {
    }
    auto label = [&](const fs::path& f) {
        auto id = assetdb::idFromPath(f);
        if (!id) return toUtf8(f.filename());
        std::string n = names.lookup(*id);
        std::string t = assetdb::typeName(uint8_t(*id >> 24));
        if (startsWith(n, "aid_" + t + "_")) n = n.substr(5 + t.size());
        return n.empty() ? hex32(*id) : n;
    };
    auto list = [&](const fs::path& dir, bool recursive) {
        std::vector<BrowserItem> items;
        if (!fs::exists(dir)) return items;
        auto add = [&](const fs::path& f) {
            BrowserItem it;
            it.path = f;
            it.label = label(f);
            it.tooltip = toUtf8(fs::relative(f, root_));
            if (auto id = assetdb::idFromPath(f)) it.tooltip += "\nid " + hex32(*id);
            labels_[toUtf8(f)] = it.label;
            it.lower = lower(it.label + "\n" + it.tooltip);
            items.push_back(std::move(it));
        };
        if (recursive) {
            for (auto& e : fs::recursive_directory_iterator(dir))
                if (e.is_regular_file() && e.path().extension() != ".bak") add(e.path());
        } else {
            for (auto& e : fs::directory_iterator(dir))
                if (e.is_regular_file() && e.path().extension() != ".bak") add(e.path());
        }
        std::sort(items.begin(), items.end(), [](auto& a, auto& b) { return a.label < b.label; });
        return items;
    };
    groups_.push_back({"Bundles", list(root_ / "Bundle" / "4f", false), {}});
    groups_.push_back({"Stream bundles", list(root_ / "Bundle" / "50", false), {}});
    BrowserGroup text{"Text", {}, {}};
    text.children.push_back({"english", list(root_ / "Debug" / "11", true), {}});
    if (fs::exists(root_ / "loctext")) {
        std::vector<fs::path> langs;
        for (auto& d : fs::directory_iterator(root_ / "loctext")) if (d.is_directory()) langs.push_back(d.path());
        std::sort(langs.begin(), langs.end());
        for (auto& d : langs) text.children.push_back({toUtf8(d.filename()), list(d, true), {}});
    }
    groups_.push_back(std::move(text));
    groups_.push_back({"Videos", list(root_ / "Debug" / "36", true), {}});
    groups_.push_back({"Fonts", list(root_ / "xuifontcachefont", false), {}});
    groups_.push_back({"Font metadata", list(root_ / "xuifontcachemeta", false), {}});
    BrowserGroup other{"Other", {}, {}};
    for (auto& e : fs::directory_iterator(root_)) {
        if (!e.is_regular_file()) continue;
        std::string ext = lower(e.path().extension().string());
        if (ext == ".xex" || ext == ".xexp" || ext == ".bak") continue;
        BrowserItem it;
        it.label = it.tooltip = toUtf8(e.path().filename());
        it.path = e.path();
        it.lower = lower(it.label);
        other.items.push_back(std::move(it));
    }
    if (fs::exists(root_ / "Debug" / "db_index.txt")) {
        BrowserItem it;
        it.label = "db_index.txt (asset index)";
        it.tooltip = "Debug/db_index.txt";
        it.path = root_ / "Debug" / "db_index.txt";
        it.lower = lower(it.label);
        other.items.push_back(std::move(it));
    }
    groups_.push_back(std::move(other));
    if (catalog_) addCatalogGroups();
}

// Indexes every texture and model in the background; the browser gains Textures, UI icons
// and Models groups when it finishes.
void App::buildCatalog() {
    if (root_.empty() || !fs::exists(root_)) return;
    fs::path root = root_;
    fs::path cache = settingsPath_.empty() ? fs::path() : settingsPath_.parent_path() / "catalog.json";
    indexing_ = true;
    runTask("Indexing textures and models", false, [this, root, cache](Task& t) -> std::function<void()> {
        ops::Progress pr;
        pr.log = [this](const std::string& s) { log(s); };
        pr.step = [&t](double f, const std::string& s) { t.report(f, s); };
        pr.cancel = &t.cancel;
        std::shared_ptr<const Catalog> cat = Catalog::build(root, cache, pr);
        return [this, cat, root] {
            indexing_ = false;
            if (!cat || root != root_) return;
            catalog_ = cat;
            addCatalogGroups();
            for (auto& k : std::exchange(pendingOpen_, {})) openWhenIndexed(k);
            size_t ui = 0;
            for (auto& x : cat->textures) ui += x.ui;
            status("Indexed " + std::to_string(cat->textures.size()) + " textures (" + std::to_string(ui) + " UI icons) and " +
                   std::to_string(cat->models.size()) + " models");
        };
    });
}

void App::addCatalogGroups() {
    groups_.erase(std::remove_if(groups_.begin(), groups_.end(),
                                 [](const BrowserGroup& g) { return g.name == "Textures" || g.name == "UI icons" || g.name == "Models"; }),
                  groups_.end());
    if (!catalog_) return;
    auto strip = [](const std::string& n, std::initializer_list<const char*> prefixes) {
        for (const char* p : prefixes)
            if (startsWith(n, p)) return n.substr(std::string(p).size());
        return n;
    };
    BrowserGroup textures{"Textures", {}, {}}, ui{"UI icons", {}, {}}, models{"Models", {}, {}};
    for (auto& t : catalog_->textures) {
        BrowserItem it;
        it.kind = BrowserItem::Texture;
        it.key = t.name;
        it.label = strip(t.name, {"aid_texture_banjox_", "aid_texture_banjoxdlc_", "aid_texture_"});
        size_t copies = std::max(t.full.size(), t.tops.size());
        it.tooltip = t.name + "\n" + std::to_string(t.width) + "x" + std::to_string(t.height) + " " + t.format + ", " +
                     std::to_string(t.mips) + " mip(s), " + std::to_string(copies) + (copies == 1 ? " copy" : " copies");
        it.lower = lower(t.name);
        if (t.ui) ui.items.push_back(it);
        textures.items.push_back(std::move(it));
    }
    for (auto& m : catalog_->models) {
        BrowserItem it;
        it.kind = BrowserItem::Model;
        it.key = m.name;
        it.label = strip(m.name, {"aid_model_banjox_", "aid_model_banjoxdlc_", "aid_model_"});
        it.tooltip = m.name + "\n" + std::to_string(m.locs.size()) + (m.locs.size() == 1 ? " copy" : " copies");
        it.lower = lower(m.name);
        models.items.push_back(std::move(it));
    }
    // Right after Bundles and Stream bundles.
    auto at = groups_.begin() + long(std::min<size_t>(2, groups_.size()));
    groups_.insert(at, {std::move(textures), std::move(ui), std::move(models)});
}

void App::collectGlyphs() {
    // The game text uses thousands of CJK characters; only those are added to the UI font.
    fs::path root = root_;
    runTask("Loading text glyphs", false, [this, root](Task& t) -> std::function<void()> {
        auto glyphs = std::make_shared<std::set<uint32_t>>();
        std::vector<fs::path> files;
        auto addDir = [&](const fs::path& d) {
            if (!fs::exists(d)) return;
            for (auto& e : fs::recursive_directory_iterator(d)) if (e.is_regular_file()) files.push_back(e.path());
        };
        addDir(root / "Debug" / "11");
        addDir(root / "loctext");
        for (size_t i = 0; i < files.size() && !t.cancel; ++i) {
            t.report(double(i) / files.size(), toUtf8(files[i].filename()));
            try {
                auto id = assetdb::idFromPath(files[i]);
                if (!id || (*id >> 24) != assetdb::kTypeLoctext) continue;
                Bytes d = readFile(files[i]);
                if (!caff::isCaff(d)) continue;
                for (auto& e : loctext::read(caff::Caff::parse(d)).entries)
                    for (char16_t ch : utf8ToUtf16(e.text)) glyphs->insert(ch);
            } catch (...) {
            }
        }
        return [this, glyphs] {
            glyphs_ = std::move(*glyphs);
            fontsDirty_ = true;
        };
    });
}

// ---- fonts -------------------------------------------------------------------------

void App::setZoom(float z) {
    userZoom_ = std::clamp(z, 0.5f, 3.0f);
    updateScale();
    saveSettings();
}

void App::updateScale() {
    // The window scale includes the monitor's content scale (125%, 150%, 200%...).
    float s = SDL_GetWindowDisplayScale(window_);
    float content = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window_));
    s = std::max({s, content, 1.0f}) * userZoom_;
    if (std::fabs(s - uiScale_) < 0.01f) return;
    uiScale_ = s;
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    if (darkTheme_) ImGui::StyleColorsDark();
    else ImGui::StyleColorsLight();
    style.FrameRounding = 3;
    style.TabRounding = 3;
    style.ScaleAllSizes(uiScale_);
    fontsDirty_ = true;
}

void App::beforeNewFrame() {
    if (fontsDirty_) {
        fontsDirty_ = false;
        rebuildFonts();
    }
}

void App::rebuildFonts() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    static ImVector<ImWchar> baseRanges, cjkRanges, hangulRanges;
    baseRanges.clear();
    cjkRanges.clear();
    hangulRanges.clear();
    ImFontGlyphRangesBuilder base, cjk, hangul;
    base.AddRanges(io.Fonts->GetGlyphRangesDefault());
    base.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    base.AddRanges(io.Fonts->GetGlyphRangesGreek());
    static const ImWchar extra[] = {0x0100, 0x024F, 0x2000, 0x206F, 0x20A0, 0x20CF, 0x2100, 0x21FF, 0x2500, 0x25FF, 0x2600, 0x27BF, 0};
    base.AddRanges(extra);
    for (uint32_t cp : glyphs_) {
        if (cp >= 0xAC00 && cp <= 0xD7AF) hangul.AddChar(ImWchar(cp));
        else if ((cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F)) hangul.AddChar(ImWchar(cp));
        else if (cp >= 0x2E80) cjk.AddChar(ImWchar(cp));
        else base.AddChar(ImWchar(cp));
    }
    base.BuildRanges(&baseRanges);
    cjk.BuildRanges(&cjkRanges);
    hangul.BuildRanges(&hangulRanges);

    const char* windir = SDL_getenv("WINDIR");
    fs::path fonts = fromUtf8(windir ? windir : "C:\\Windows") / "Fonts";
    float size = std::round(16.0f * uiScale_);
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    ImFont* main = nullptr;
    if (fs::exists(fonts / "segoeui.ttf")) main = io.Fonts->AddFontFromFileTTF(toUtf8(fonts / "segoeui.ttf").c_str(), size, &cfg, baseRanges.Data);
    if (!main) {
        ImFontConfig def;
        def.SizePixels = size;
        io.Fonts->AddFontDefault(&def);
    }
    ImFontConfig merge;
    merge.MergeMode = true;
    merge.OversampleH = 1;
    if (cjkRanges.Size > 1 && fs::exists(fonts / "msyh.ttc")) io.Fonts->AddFontFromFileTTF(toUtf8(fonts / "msyh.ttc").c_str(), size, &merge, cjkRanges.Data);
    if (hangulRanges.Size > 1 && fs::exists(fonts / "malgun.ttf")) io.Fonts->AddFontFromFileTTF(toUtf8(fonts / "malgun.ttf").c_str(), size, &merge, hangulRanges.Data);
    io.Fonts->Build();
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    ImGui_ImplOpenGL3_CreateFontsTexture();
}

// ---- services -----------------------------------------------------------------------

std::shared_ptr<GpuImage> App::upload(const image::Rgba& img, bool nearest) {
    auto g = std::make_shared<GpuImage>();
    if (img.width == 0 || img.height == 0) return g;
    glGenTextures(1, &g->tex);
    glBindTexture(GL_TEXTURE_2D, g->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLsizei(img.width), GLsizei(img.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, img.pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    g->width = int(img.width);
    g->height = int(img.height);
    return g;
}

void App::status(const std::string& msg) {
    status_ = msg;
    statusError_ = false;
    log(msg);
}

void App::error(const std::string& msg) {
    status_ = msg;
    statusError_ = true;
    log("error: " + msg);
}

void App::log(const std::string& msg) {
    std::lock_guard<std::mutex> lk(logMutex_);
    log_.push_back(msg);
    while (log_.size() > 2000) log_.pop_front();
}

void App::backupBeforeWrite(const fs::path& p) {
    if (!backups_ || !fs::exists(p)) return;
    fs::path bak = p;
    bak += ".bak";
    if (!fs::exists(bak)) fs::copy_file(p, bak);
}

void App::openDocument(const std::string& key, const std::string& label, std::function<std::unique_ptr<Document>()> load) {
    for (auto& d : docs_) {
        if (d->key == key) {
            selectTab_ = d->uid;
            return;
        }
    }
    runTask("Opening " + label, true, [this, key, load = std::move(load)](Task&) -> std::function<void()> {
        auto holder = std::make_shared<std::unique_ptr<Document>>(load());
        return [this, holder, key] {
            auto& d = *holder;
            d->key = key;
            d->uid = "doc" + std::to_string(++uidCounter_);
            selectTab_ = d->uid;
            status("Opened " + d->title());
            docs_.push_back(std::move(d));
        };
    });
}

void App::openPath(const fs::path& p) {
    fs::path root = root_;
    std::string name = labels_.count(toUtf8(p)) ? labels_[toUtf8(p)] : toUtf8(p.filename());
    openDocument(toUtf8(p), name, [p, root] { return loadDocument(p, root); });
}

void App::openTexture(const std::string& name) {
    const TextureInfo* t = catalog_ ? catalog_->texture(name) : nullptr;
    if (!t) {
        error(indexing_ ? "Still indexing textures, try again in a moment" : "Texture not found: " + name);
        return;
    }
    TextureInfo copy = *t;
    fs::path root = root_;
    openDocument("tex:" + name, name, [copy, root] { return makeTextureDocument(copy, root); });
}

void App::openWhenIndexed(const std::string& key) {
    if (!catalog_) {
        pendingOpen_.push_back(key);
        return;
    }
    if (startsWith(key, "tex:")) openTexture(key.substr(4));
    else if (startsWith(key, "model:")) openModel(key.substr(6));
}

void App::openModel(const std::string& name) {
    const ModelInfo* m = catalog_ ? catalog_->model(name) : nullptr;
    if (!m) {
        error(indexing_ ? "Still indexing models, try again in a moment" : "Model not found: " + name);
        return;
    }
    ModelInfo copy = *m;
    auto cat = catalog_;
    fs::path root = root_;
    openDocument("model:" + name, name, [copy, cat, root] { return makeModelDocument(copy, cat, root); });
}

void App::openAny(const fs::path& p) {
    if (fs::is_directory(p)) {
        if (fs::exists(p / "Debug" / "db_index.txt") || fs::exists(p / "Bundle")) setAssetsRoot(p);
        else if (fs::exists(p / "assets" / "Debug" / "db_index.txt")) setAssetsRoot(p / "assets");
        else error(toUtf8(p) + " does not look like the game's assets folder");
        return;
    }
    if (fs::exists(p)) openPath(p);
    else error("No such file: " + toUtf8(p));
}

// ---- dialogs --------------------------------------------------------------------------

int App::newDialog(FileCallback cb, std::vector<Filter> filters, const fs::path& start) {
    auto req = std::make_unique<DialogRequest>();
    req->cb = std::move(cb);
    req->strings.reserve(filters.size() * 2);
    for (auto& f : filters) {
        req->strings.push_back(f.name);
        req->strings.push_back(f.pattern);
    }
    for (size_t i = 0; i < filters.size(); ++i) req->filters.push_back({req->strings[i * 2].c_str(), req->strings[i * 2 + 1].c_str()});
    // Start where the user last was: a bare file name (a suggested save name) goes into that folder.
    fs::path dir = !lastDir_.empty() && fs::is_directory(lastDir_) ? lastDir_ : root_;
    fs::path where = start;
    if (start.empty()) where = dir;
    else if (!start.has_parent_path() && !dir.empty()) where = dir / start;
    req->start = where.empty() ? std::string() : toUtf8(where);
    std::lock_guard<std::mutex> lk(dialogMutex_);
    int id = ++dialogCounter_;
    dialogs_[id] = std::move(req);
    return id;
}

void SDLCALL App::dialogCallback(void* userdata, const char* const* files, int) {
    // SDL may call this from another thread; results are handed to the UI thread.
    App* app = instance_;
    if (!app) return;
    std::string path = (files && files[0]) ? files[0] : "";
    std::lock_guard<std::mutex> lk(app->dialogMutex_);
    app->dialogResults_.emplace_back(int(intptr_t(userdata)), path);
}

void App::openFileDialog(const std::string&, std::vector<Filter> filters, FileCallback cb, const fs::path& start) {
    int id = newDialog(std::move(cb), std::move(filters), start);
    auto& r = *dialogs_[id];
    SDL_ShowOpenFileDialog(dialogCallback, reinterpret_cast<void*>(intptr_t(id)), window_, r.filters.empty() ? nullptr : r.filters.data(),
                           int(r.filters.size()), r.start.empty() ? nullptr : r.start.c_str(), false);
}

void App::saveFileDialog(const std::string&, std::vector<Filter> filters, FileCallback cb, const fs::path& start) {
    int id = newDialog(std::move(cb), std::move(filters), start);
    auto& r = *dialogs_[id];
    SDL_ShowSaveFileDialog(dialogCallback, reinterpret_cast<void*>(intptr_t(id)), window_, r.filters.empty() ? nullptr : r.filters.data(),
                           int(r.filters.size()), r.start.empty() ? nullptr : r.start.c_str());
}

void App::openFolderDialog(const std::string&, FileCallback cb, const fs::path& start) {
    int id = newDialog(std::move(cb), {}, start);
    auto& r = *dialogs_[id];
    SDL_ShowOpenFolderDialog(dialogCallback, reinterpret_cast<void*>(intptr_t(id)), window_, r.start.empty() ? nullptr : r.start.c_str(), false);
}

void App::pumpDialogs() {
    std::deque<std::pair<int, std::string>> results;
    std::map<int, std::unique_ptr<DialogRequest>> done;
    {
        std::lock_guard<std::mutex> lk(dialogMutex_);
        results.swap(dialogResults_);
        for (auto& [id, path] : results) {
            auto it = dialogs_.find(id);
            if (it != dialogs_.end()) {
                done[id] = std::move(it->second);
                dialogs_.erase(it);
            }
        }
    }
    for (auto& [id, path] : results) {
        auto it = done.find(id);
        if (it == done.end() || path.empty()) continue;
        fs::path chosen = fromUtf8(path);
        std::error_code ec;
        fs::path dir = fs::is_directory(chosen, ec) ? chosen : chosen.parent_path();
        if (!dir.empty() && dir != lastDir_) {
            lastDir_ = dir;
            saveSettings();
        }
        try {
            it->second->cb(fromUtf8(path));
        } catch (std::exception& e) {
            error(e.what());
        }
    }
}

// ---- tasks ------------------------------------------------------------------------------

void App::runTask(const std::string& label, bool modal, std::function<std::function<void()>(Task&)> work) {
    auto t = std::make_unique<Task>();
    t->label = label;
    t->modal = modal;
    Task* raw = t.get();
    tasks_.push_back(std::move(t));
    raw->thread = std::thread([raw, work = std::move(work)] {
        try {
            raw->onDone = work(*raw);
        } catch (std::exception& e) {
            raw->error = e.what();
        } catch (...) {
            raw->error = "unknown error";
        }
        raw->finished = true;
    });
}

void App::pumpTasks() {
    for (size_t i = 0; i < tasks_.size();) {
        Task& t = *tasks_[i];
        if (!t.finished) { ++i; continue; }
        t.thread.join();
        std::unique_ptr<Task> keep = std::move(tasks_[i]);
        tasks_.erase(tasks_.begin() + long(i));
        if (!keep->error.empty()) error(keep->label + ": " + keep->error);
        else if (keep->onDone) {
            try { keep->onDone(); } catch (std::exception& e) { error(e.what()); }
        }
    }
}

// ---- frame --------------------------------------------------------------------------------

void App::frame() {
    pumpDialogs();
    pumpTasks();

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput || io.KeyCtrl) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            for (auto& d : docs_) if (d->uid == currentTabUid_) { saveDocument(*d, io.KeyShift); break; }
        }
        if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false))) setZoom(userZoom_ + 0.125f);
        if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false))) setZoom(userZoom_ - 0.125f);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0, false)) setZoom(1.0f);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false))
            openFolderDialog("Choose the assets folder", [this](const fs::path& p) { setAssetsRoot(p); }, root_);
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    ImGui::PopStyleVar(2);
    drawMenu();

    float statusH = ImGui::GetFrameHeightWithSpacing();
    float logH = showLog_ ? 160 * uiScale_ : 0;
    float avail = ImGui::GetContentRegionAvail().y - statusH - logH;
    // browserWidth_ is stored at 100% scale so it survives DPI changes.
    float bw = browserWidth_ * uiScale_;
    ImGui::BeginChild("##browser", ImVec2(bw, avail), ImGuiChildFlags_Borders);
    drawBrowser(bw);
    ImGui::EndChild();
    splitter("##split", bw, em(150), vp->WorkSize.x - em(300), avail);
    browserWidth_ = bw / uiScale_;
    ImGui::BeginChild("##docs", ImVec2(0, avail));
    drawDocuments();
    ImGui::EndChild();
    if (showLog_) drawLog();
    drawStatusBar();
    ImGui::End();

    drawTasks();
    drawExtractDialog();
    drawModExport();
    drawIdTool();
    drawAbout();
    drawCloseConfirm();
}

void App::drawMenu() {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open assets folder...", "Ctrl+O"))
            openFolderDialog("Choose the assets folder", [this](const fs::path& p) { setAssetsRoot(p); }, root_);
        if (ImGui::MenuItem("Open file...")) openFileDialog("Open asset file", {}, [this](const fs::path& p) { openPath(p); }, root_);
        if (ImGui::MenuItem("Rescan assets folder", nullptr, false, !root_.empty())) scanAssets();
        ImGui::Separator();
        Document* cur = nullptr;
        for (auto& d : docs_) if (d->uid == currentTabUid_) cur = d.get();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, cur && cur->canSave())) saveDocument(*cur, false);
        if (ImGui::MenuItem("Save as...", "Ctrl+Shift+S", false, cur && cur->canSave())) saveDocument(*cur, true);
        if (ImGui::MenuItem("Save all", nullptr, false, !docs_.empty()))
            for (auto& d : docs_) if (d->dirty()) saveDocument(*d, false);
        ImGui::Separator();
        ImGui::MenuItem("Make .bak backups when saving", nullptr, &backups_);
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) requestQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem("Extract everything...", nullptr, false, !root_.empty())) showExtract_ = true;
        if (ImGui::MenuItem("Export mod package (.nbpatch)...", nullptr, false, !root_.empty())) showModExport_ = true;
        if (ImGui::MenuItem("Verify all files", nullptr, false, !root_.empty())) {
            fs::path root = root_;
            runTask("Verifying all files", false, [this, root](Task& t) -> std::function<void()> {
                ops::Progress pr;
                pr.log = [this](const std::string& s) { log(s); };
                pr.step = [&t](double f, const std::string& s) { t.report(f, s); };
                pr.cancel = &t.cancel;
                auto r = ops::verifyAll(root, pr);
                return [this, r] {
                    if (r.failed) error("Verify: " + std::to_string(r.passed) + " passed, " + std::to_string(r.failed) + " failed (see log)");
                    else status("Verify: all " + std::to_string(r.passed) + " checks passed");
                };
            });
        }
        if (ImGui::MenuItem("Asset ID calculator...")) showIdTool_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Log", nullptr, &showLog_);
        static float zoomPct = 100;
        if (ImGui::IsWindowAppearing()) zoomPct = userZoom_ * 100;
        ImGui::SetNextItemWidth(em(160));
        ImGui::SliderFloat("UI size", &zoomPct, 50, 300, "%.0f%%");
        if (ImGui::IsItemDeactivatedAfterEdit()) setZoom(zoomPct / 100);
        ImGui::TextDisabled("Ctrl+= / Ctrl+- / Ctrl+0");
        if (ImGui::MenuItem("Dark theme", nullptr, &darkTheme_)) {
            if (darkTheme_) ImGui::StyleColorsDark();
            else ImGui::StyleColorsLight();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About")) showAbout_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void App::drawBrowser(float) {
    if (root_.empty()) {
        ImGui::TextWrapped("No assets folder selected.");
        if (ImGui::Button("Choose assets folder..."))
            openFolderDialog("Choose the assets folder", [this](const fs::path& p) { setAssetsRoot(p); });
        return;
    }
    ImGui::TextDisabled("%s", toUtf8(root_).c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    inputText("##filter", browserFilter_);
    if (browserFilter_.empty() && !ImGui::IsItemActive()) {
        ImVec2 pos = ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + ImGui::GetStyle().FramePadding.x, pos.y + ImGui::GetStyle().FramePadding.y),
                                            ImGui::GetColorU32(ImGuiCol_TextDisabled), "Search assets...");
    }
    if (indexing_) ImGui::TextDisabled("Indexing textures and models...");
    ImGui::Separator();
    ImGui::BeginChild("##tree");
    for (auto& g : groups_) drawBrowserGroup(g, true);
    ImGui::EndChild();
}

void App::drawBrowserGroup(const BrowserGroup& g, bool topLevel) {
    std::string needle = lower(browserFilter_);
    auto matches = [&](const BrowserItem& it) { return needle.empty() || it.lower.find(needle) != std::string::npos; };
    std::function<size_t(const BrowserGroup&)> count = [&](const BrowserGroup& x) {
        size_t n = 0;
        for (auto& it : x.items) n += matches(it);
        for (auto& c : x.children) n += count(c);
        return n;
    };
    size_t n = count(g);
    if (n == 0 && !needle.empty()) return;
    if (!needle.empty()) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    char head[256];
    std::snprintf(head, sizeof head, "%s (%zu)###%s%p", g.name.c_str(), n, g.name.c_str(), static_cast<const void*>(&g));
    if (!ImGui::TreeNodeEx(head, topLevel ? ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth : ImGuiTreeNodeFlags_SpanAvailWidth)) return;
    for (auto& c : g.children) drawBrowserGroup(c, false);
    std::vector<const BrowserItem*> shown;
    for (auto& it : g.items) if (matches(it)) shown.push_back(&it);
    ImGuiListClipper clip;
    clip.Begin(int(shown.size()));
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            const BrowserItem& it = *shown[size_t(i)];
            std::string key = it.kind == BrowserItem::Texture ? "tex:" + it.key : it.kind == BrowserItem::Model ? "model:" + it.key : toUtf8(it.path);
            bool open = false;
            for (auto& d : docs_) if (d->key == key) open = true;
            ImGui::PushID(&it);
            if (ImGui::Selectable(it.label.c_str(), open)) {
                if (it.kind == BrowserItem::Texture) openTexture(it.key);
                else if (it.kind == BrowserItem::Model) openModel(it.key);
                else openPath(it.path);
            }
            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted(it.tooltip.c_str());
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
    }
    ImGui::TreePop();
}

void App::drawDocuments() {
    if (docs_.empty()) {
        drawWelcome();
        return;
    }
    if (!ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_AutoSelectNewTabs)) return;
    for (size_t i = 0; i < docs_.size(); ++i) {
        Document& d = *docs_[i];
        bool open = true;
        ImGuiTabItemFlags flags = d.dirty() ? ImGuiTabItemFlags_UnsavedDocument : 0;
        if (selectTab_ == d.uid && ImGui::GetFrameCount() > 1) flags |= ImGuiTabItemFlags_SetSelected;
        std::string label = d.title() + "###" + d.uid;
        if (ImGui::BeginTabItem(label.c_str(), &open, flags)) {
            if (selectTab_ == d.uid) selectTab_.clear();
            currentTabUid_ = d.uid;
            ImGui::BeginChild("##doc");
            try {
                d.draw(*this);
            } catch (std::exception& e) {
                error(d.title() + ": " + e.what());
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (!open) {
            if (d.dirty()) closeConfirm_ = int(i);
            else { closeDocument(i); break; }
        }
    }
    ImGui::EndTabBar();
}

void App::drawWelcome() {
    ImGui::Dummy(ImVec2(0, 20 * uiScale_));
    ImGui::Indent(20 * uiScale_);
    ImGui::TextUnformatted("NutTool - Banjo-Kazooie: Nuts & Bolts asset editor");
    ImGui::Spacing();
    if (root_.empty()) {
        ImGui::TextWrapped("Start by choosing the game's assets folder (the one with Bundle, Debug, loctext...).");
        if (ImGui::Button("Choose assets folder..."))
            openFolderDialog("Choose the assets folder", [this](const fs::path& p) { setAssetsRoot(p); });
    } else {
        ImGui::BulletText("Pick an asset on the left to open it. Use the search box to filter by name.");
        ImGui::BulletText("Bundles: browse every asset inside, preview and replace textures, export or replace raw data.");
        ImGui::BulletText("Text: edit strings for any language side by side with English.");
        ImGui::BulletText("Fonts: view glyphs, edit metrics, export/import glyph pages.");
        ImGui::BulletText("Tools > Extract everything... unpacks the whole game into a folder.");
        ImGui::Spacing();
        ImGui::TextDisabled("Saving writes plain (uncompressed) files and keeps a .bak of the original.");
    }
    ImGui::Unindent();
}

void App::drawStatusBar() {
    ImGui::Separator();
    for (auto& t : tasks_) {
        if (t->modal) continue;
        double p;
        std::string step;
        {
            std::lock_guard<std::mutex> lk(t->m);
            p = t->progress;
            step = t->step;
        }
        ImGui::PushID(t.get());
        ImGui::ProgressBar(p < 0 ? float(-1.0 * ImGui::GetTime()) : float(p), ImVec2(180 * uiScale_, 0), t->label.c_str());
        if (ImGui::BeginItemTooltip()) { ImGui::TextUnformatted(step.c_str()); ImGui::EndTooltip(); }
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) t->cancel = true;
        ImGui::SameLine();
        ImGui::PopID();
    }
    if (statusError_) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", status_.c_str());
    else ImGui::TextUnformatted(status_.c_str());
}

void App::drawLog() {
    ImGui::BeginChild("##log", ImVec2(0, 160 * uiScale_ - ImGui::GetStyle().ItemSpacing.y), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    std::lock_guard<std::mutex> lk(logMutex_);
    ImGuiListClipper clip;
    clip.Begin(int(log_.size()));
    while (clip.Step())
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) ImGui::TextUnformatted(log_[size_t(i)].c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

void App::drawTasks() {
    Task* modal = nullptr;
    for (auto& t : tasks_) if (t->modal) { modal = t.get(); break; }
    if (modal && !ImGui::IsPopupOpen("Working")) ImGui::OpenPopup("Working");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Working", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        if (!modal) ImGui::CloseCurrentPopup();
        else {
            double p;
            std::string step;
            {
                std::lock_guard<std::mutex> lk(modal->m);
                p = modal->progress;
                step = modal->step;
            }
            ImGui::TextUnformatted(modal->label.c_str());
            ImGui::ProgressBar(p < 0 ? float(-1.0 * ImGui::GetTime()) : float(p), ImVec2(320 * uiScale_, 0), step.empty() ? "" : step.c_str());
        }
        ImGui::EndPopup();
    }
}

void App::drawExtractDialog() {
    if (showExtract_) ImGui::OpenPopup("Extract everything");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Extract everything", &showExtract_, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted("Unpack every asset into editable files (textures as DDS, text as JSON, ...).");
    ImGui::SetNextItemWidth(420 * uiScale_);
    inputText("Output folder", extractOut_);
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) openFolderDialog("Output folder", [this](const fs::path& p) { extractOut_ = toUtf8(p); });
    static const char* cats[] = {"bundles", "streams", "text", "video", "fonts", "ini", "misc"};
    static const char* catLabels[] = {"Bundles", "Stream bundles (incl. audio)", "Text", "Videos", "Fonts", "Font metadata", "Other files"};
    for (int i = 0; i < 7; ++i) {
        ImGui::Checkbox(catLabels[i], &extractCats_[i]);
        if (i % 3 != 2 && i != 6) ImGui::SameLine();
    }
    ImGui::Checkbox("Also write PNG copies of textures", &extractPng_);
    ImGui::TextDisabled("A full extraction writes about 8 GB and takes a few minutes.");
    ImGui::BeginDisabled(extractOut_.empty());
    if (ImGui::Button("Start")) {
        ops::ExtractOptions opt;
        opt.png = extractPng_;
        for (int i = 0; i < 7; ++i) if (extractCats_[i]) opt.only.insert(cats[i]);
        fs::path root = root_, out = fromUtf8(extractOut_);
        saveSettings();
        runTask("Extracting", false, [this, root, out, opt](Task& t) -> std::function<void()> {
            ops::Progress pr;
            pr.log = [this](const std::string& s) { log(s); };
            pr.step = [&t](double f, const std::string& s) { t.report(f, s); };
            pr.cancel = &t.cancel;
            int failures = ops::extractAll(root, out, opt, pr);
            bool cancelled = t.cancel;
            return [this, failures, cancelled, out] {
                if (cancelled) status("Extraction cancelled");
                else if (failures) error("Extraction finished with " + std::to_string(failures) + " errors (see log)");
                else status("Extraction finished: " + toUtf8(out));
            };
        });
        showExtract_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close")) { showExtract_ = false; ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}

void App::drawModExport() {
    if (!showModExport_) return;
    ImGui::SetNextWindowSize(ImVec2(em(660), em(600)), ImGuiCond_Appearing);
    if (!ImGui::Begin("Export mod package", &showModExport_)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Packages the game files you changed as a .nbpatch mod (NB Studio's format). reNut applies it at startup "
                       "to an unmodified game folder, without writing to that folder; put it in reNut's mods folder.");

    ImGui::SeparatorText("Mod");
    const float w = em(380);
    ImGui::SetNextItemWidth(w);
    inputText("Name", mod_.name);
    ImGui::SetNextItemWidth(w);
    inputText("Version", mod_.version);
    ImGui::SetNextItemWidth(w);
    inputText("Author", mod_.author);
    ImGui::SetNextItemWidth(w);
    inputText("Category", mod_.category);
    ImGui::SetNextItemWidth(w);
    inputText("Id", mod_.id);
    ImGui::SameLine();
    helpMarker("Leave empty to use the name in lower case with dashes. Other mods name this one by id when they need it "
               "or cannot be combined with it.");
    if (mod_.id.empty() && !mod_.name.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", nbpatch::slug(mod_.name).c_str());
    }
    inputTextMultiline("Description", mod_.description, ImVec2(w, em(52)));

    ImGui::SeparatorText("Changed files");
    bool rescan = false;
    rescan |= ImGui::RadioButton("Files that have a .bak backup (saved by NutTool with backups on)", &modSource_, 0);
    rescan |= ImGui::RadioButton("Files that differ from a clean copy of the game folder", &modSource_, 1);
    if (modSource_ == 1) {
        ImGui::SetNextItemWidth(em(460));
        rescan |= inputText("##original", modOriginal_);
        ImGui::SameLine();
        if (ImGui::Button("Browse...##original"))
            openFolderDialog("Clean game folder", [this](const fs::path& p) { modOriginal_ = toUtf8(p); modScanned_ = false; });
    }
    if (rescan) modScanned_ = false;
    ImGui::BeginDisabled(busy() || (modSource_ == 1 && modOriginal_.empty()));
    if (ImGui::Button("Find changed files")) {
        fs::path root = root_, orig = modSource_ == 1 ? fromUtf8(modOriginal_) : fs::path();
        saveSettings();
        runTask("Looking for changed files", true, [this, root, orig](Task& t) -> std::function<void()> {
            ops::Progress pr;
            pr.log = [this](const std::string& s) { log(s); };
            pr.step = [&t](double f, const std::string& s) { t.report(f, s); };
            pr.cancel = &t.cancel;
            auto found = std::make_shared<std::vector<nbpatch::Change>>(nbpatch::findChanges(root, orig, pr));
            return [this, found] {
                modChanges_ = std::move(*found);
                modPick_.assign(modChanges_.size(), 1);
                modScanned_ = true;
                status(std::to_string(modChanges_.size()) + " changed file(s)");
            };
        });
    }
    ImGui::EndDisabled();
    if (modScanned_) {
        ImGui::SameLine();
        if (modChanges_.empty()) ImGui::TextDisabled("No changed files.");
        else ImGui::Text("%zu changed file(s)", modChanges_.size());
    }
    if (ImGui::BeginChild("##modfiles", ImVec2(0, em(150)), ImGuiChildFlags_Borders)) {
        for (size_t i = 0; i < modChanges_.size(); ++i) {
            const auto& c = modChanges_[i];
            bool on = modPick_[i] != 0;
            if (ImGui::Checkbox(c.path.c_str(), &on)) modPick_[i] = on;
            ImGui::SameLine();
            ImGui::TextDisabled(c.original.empty() ? "new file" : "changed");
        }
    }
    ImGui::EndChild();

    ImGui::SeparatorText("Package");
    ImGui::SetNextItemWidth(em(460));
    inputText("##out", modOut_);
    ImGui::SameLine();
    if (ImGui::Button("Browse...##out")) {
        std::string name = (mod_.id.empty() ? nbpatch::slug(mod_.name) : mod_.id);
        saveFileDialog("Save mod package", {{"Mod package", "nbpatch"}}, [this](const fs::path& p) {
            fs::path q = p;
            if (lower(q.extension().string()) != ".nbpatch") q += ".nbpatch";
            modOut_ = toUtf8(q);
        }, lastDir_ / (name.empty() ? "mod.nbpatch" : name + ".nbpatch"));
    }
    size_t picked = size_t(std::count(modPick_.begin(), modPick_.end(), 1));
    ImGui::BeginDisabled(busy() || trim(mod_.name).empty() || modOut_.empty() || picked == 0);
    if (ImGui::Button("Build package")) {
        std::vector<nbpatch::Change> changes;
        for (size_t i = 0; i < modChanges_.size(); ++i)
            if (modPick_[i]) changes.push_back(modChanges_[i]);
        nbpatch::Info info = mod_;
        fs::path out = fromUtf8(modOut_);
        saveSettings();
        runTask("Building mod package", true, [this, changes, info, out](Task& t) -> std::function<void()> {
            ops::Progress pr;
            pr.log = [this](const std::string& s) { log(s); };
            pr.step = [&t](double f, const std::string& s) { t.report(f, s); };
            pr.cancel = &t.cancel;
            log("Building " + toUtf8(out));
            bool ok = nbpatch::write(out, info, changes, pr);
            return [this, ok, out] {
                if (ok) status("Wrote " + toUtf8(out));
                else {
                    error("The mod package was not written (see the log)");
                    showLog_ = true;
                }
            };
        });
    }
    ImGui::EndDisabled();
    if (trim(mod_.name).empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("Give the mod a name.");
    }
    ImGui::End();
}

void App::drawIdTool() {
    if (!showIdTool_) return;
    ImGui::SetNextWindowSize(ImVec2(520 * uiScale_, 0), ImGuiCond_Appearing);
    if (ImGui::Begin("Asset ID calculator", &showIdTool_)) {
        ImGui::TextWrapped("Type an asset name such as aid_loctext_banjox_frontend or aid_texture_banjox_particles_spanglewhite.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        inputText("##name", idToolInput_);
        if (auto id = assetdb::idFromName(trim(idToolInput_))) {
            ImGui::Text("id: %s   type: %s", hex32(*id).c_str(), assetdb::typeName(uint8_t(*id >> 24)).c_str());
            fs::path p = assetdb::pathForId(root_, *id);
            ImGui::Text("file: %s", toUtf8(root_.empty() ? assetdb::pathForId("", *id) : fs::relative(p, root_)).c_str());
            if (!root_.empty()) {
                if (fs::exists(p)) {
                    if (ImGui::Button("Open")) openPath(p);
                } else {
                    ImGui::TextDisabled("(no such file; the asset may live inside a bundle)");
                }
            }
        } else if (!idToolInput_.empty()) {
            ImGui::TextDisabled("Unknown asset type prefix.");
        }
    }
    ImGui::End();
}

void App::drawAbout() {
    if (showAbout_) ImGui::OpenPopup("About NutTool");
    if (ImGui::BeginPopupModal("About NutTool", &showAbout_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("NutTool - Banjo-Kazooie: Nuts & Bolts asset tool");
        ImGui::Spacing();
        ImGui::TextUnformatted("Reads, unpacks and edits bundles, stream bundles, text, textures,");
        ImGui::TextUnformatted("fonts, audio banks and videos. See README.md for the formats.");
        ImGui::Spacing();
        ImGui::TextDisabled("Dear ImGui %s, SDL %d.%d.%d", IMGUI_VERSION, SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
        if (ImGui::Button("Close")) { showAbout_ = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

void App::saveDocument(Document& d, bool saveAs) {
    if (!d.canSave()) return;
    if (saveAs) {
        std::string uid = d.uid;
        saveFileDialog("Save as", {}, [this, uid](const fs::path& p) {
            for (auto& doc : docs_)
                if (doc->uid == uid && doc->save(*this, &p)) status("Saved " + toUtf8(p));
        }, d.path);
        return;
    }
    if (d.save(*this, nullptr)) status("Saved " + toUtf8(d.path));
}

void App::closeDocument(size_t i) {
    if (i < docs_.size()) docs_.erase(docs_.begin() + long(i));
}

void App::requestQuit() {
    bool anyDirty = false;
    for (auto& d : docs_) anyDirty |= d->dirty();
    if (anyDirty) quitPending_ = true;
    else quit_ = true;
}

void App::drawCloseConfirm() {
    bool want = closeConfirm_ >= 0 || quitPending_;
    if (want && !ImGui::IsPopupOpen("Unsaved changes")) ImGui::OpenPopup("Unsaved changes");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    std::vector<size_t> which;
    if (quitPending_) { for (size_t i = 0; i < docs_.size(); ++i) if (docs_[i]->dirty()) which.push_back(i); }
    else if (closeConfirm_ >= 0 && size_t(closeConfirm_) < docs_.size()) which.push_back(size_t(closeConfirm_));
    ImGui::TextUnformatted("These documents have unsaved changes:");
    for (size_t i : which) ImGui::BulletText("%s", docs_[i]->title().c_str());
    auto finish = [&](bool proceed) {
        if (proceed) {
            if (quitPending_) quit_ = true;
            else if (closeConfirm_ >= 0) closeDocument(size_t(closeConfirm_));
        }
        quitPending_ = false;
        closeConfirm_ = -1;
        ImGui::CloseCurrentPopup();
    };
    if (ImGui::Button("Save")) {
        bool ok = true;
        for (size_t i : which) ok &= docs_[i]->save(*this, nullptr);
        finish(ok);
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard")) finish(true);
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) finish(false);
    ImGui::EndPopup();
}

} // namespace nut::gui
