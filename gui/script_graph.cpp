// reNut > Script graphs: nodes wired as in a visual scripting editor, each graph made into one of a mod's
// Lua scripts (src/scriptgraph.hpp). The canvas is imgui-node-editor's.
#include "app.hpp"

#include "scriptgraph.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>

#if NUTTOOL_NODE_EDITOR
#include <imgui-node-editor/imgui_node_editor.h>
#endif

namespace nut::gui {

#if !NUTTOOL_NODE_EDITOR

void App::drawScriptGraph() {
    if (!tools_.showGraph) return;
    if (ImGui::Begin("Script graphs", &tools_.showGraph))
        ImGui::TextWrapped("This NutTool was built without imgui-node-editor (vcpkg port \"imgui-node-editor\"), so the script "
                           "graph editor isn't in it. Its graphs can still be made into Lua with the graph-lua command.");
    ImGui::End();
}

#else

namespace ed = ax::NodeEditor;
namespace sg = scriptgraph;

namespace {

// The open graph and the canvas showing it.
struct Editor {
    ed::EditorContext* context = nullptr;
    ed::Style defaults;              // the editor's style as made, which the themes start from
    int generation = 0;              // counts canvases made, so per-node theme state starts afresh
    sg::Graph graph;
    std::string name;                // the open graph's name ("" for none)
    std::string modSeen;             // the mod folder `list` was read from
    std::vector<std::string> list;   // that mod's graphs
    std::vector<int> place;          // nodes to put where they are on the next frame
    bool dirty = false, changed = true, navigate = false;
    std::string lua;
    std::vector<std::string> problems;
    std::string newName = "mymod_graph";
    std::string filter;
    std::string pendingOpen;         // a graph to open once unsaved changes are dealt with
    ImVec2 menuAt{0, 0};             // where a node from the menu goes, on the canvas
    int dragNode = 0;                // a wire dropped on the canvas: its node, pin and side
    std::string dragPin;
    bool dragOutput = false;
    int choiceNode = 0;              // an input whose dropdown was clicked: its node and pin
    std::string choicePin;
    bool openChoices = false;
};

Editor& editor() {
    static Editor e;
    return e;
}

// A fresh canvas (a graph opened or made), its style's defaults kept for the themes.
void newCanvas(Editor& e) {
    if (e.context) ed::DestroyEditor(e.context);
    ed::Config config;
    config.SettingsFile = nullptr;  // positions are kept in the graph files
    e.context = ed::CreateEditor(&config);
    ed::SetCurrentEditor(e.context);
    e.defaults = ed::GetStyle();
    ed::SetCurrentEditor(nullptr);
    ++e.generation;
}

uintptr_t nodeKey(int id) { return uintptr_t(id) * 64; }
uintptr_t pinKey(int node, size_t index, bool output) { return uintptr_t(node) * 64 + 1 + (output ? 32 : 0) + index; }
uintptr_t linkKey(int id) { return (uintptr_t(1) << 40) + uintptr_t(id); }

const std::vector<sg::PinDef>& side(const sg::NodeDef& d, bool output) { return output ? d.outputs : d.inputs; }

size_t pinIndex(const sg::NodeDef& d, const std::string& name, bool output) {
    const auto& pins = side(d, output);
    for (size_t i = 0; i < pins.size(); ++i)
        if (pins[i].name == name) return i;
    return pins.size();
}

// What a pin key stands for in the graph.
bool pinOf(const sg::Graph& g, uintptr_t key, int& node, std::string& pin, bool& output, sg::PinType& type) {
    if (key == 0 || key >= (uintptr_t(1) << 40)) return false;
    node = int(key / 64);
    const size_t slot = key % 64;
    if (slot == 0) return false;
    output = slot - 1 >= 32;
    const size_t index = (slot - 1) % 32;
    const sg::Node* n = g.node(node);
    const sg::NodeDef* d = n ? sg::findDef(n->def) : nullptr;
    if (!d || index >= side(*d, output).size()) return false;
    pin = side(*d, output)[index].name;
    type = side(*d, output)[index].type;
    return true;
}

ImVec4 colourOf(sg::PinType t) {
    switch (t) {
        case sg::PinType::Exec: return {1, 1, 1, 1};
        case sg::PinType::Bool: return {0.86f, 0.27f, 0.27f, 1};
        case sg::PinType::Number: return {0.45f, 0.86f, 0.42f, 1};
        case sg::PinType::String: return {0.92f, 0.45f, 0.9f, 1};
        case sg::PinType::Vector: return {0.97f, 0.8f, 0.25f, 1};
        case sg::PinType::Part: return {0.97f, 0.56f, 0.2f, 1};
        case sg::PinType::Vehicle: return {0.36f, 0.62f, 1, 1};
        case sg::PinType::Npc: return {0.3f, 0.86f, 0.86f, 1};
        case sg::PinType::Table: return {0.68f, 0.48f, 0.97f, 1};
        case sg::PinType::Actor: return {0.98f, 0.42f, 0.6f, 1};
        case sg::PinType::Place: return {0.75f, 0.92f, 0.4f, 1};
        default: return {0.72f, 0.72f, 0.72f, 1};
    }
}

ImVec4 headerOf(const sg::NodeDef* d) {
    if (!d) return {0.5f, 0.1f, 0.1f, 1};
    switch (d->kind) {
        case sg::Kind::Event: return d->callback ? ImVec4{0.62f, 0.25f, 0.12f, 1} : ImVec4{0.62f, 0.16f, 0.16f, 1};
        case sg::Kind::Action: return {0.18f, 0.36f, 0.66f, 1};
        case sg::Kind::Pure: return {0.2f, 0.48f, 0.27f, 1};
        case sg::Kind::Flow: return {0.36f, 0.36f, 0.4f, 1};
        case sg::Kind::PartDef: return {0.7f, 0.42f, 0.08f, 1};
    }
    return {0.3f, 0.3f, 0.3f, 1};
}

// A pin's mark: a triangle for the white wires, a circle for values; filled when wired. `bubble`
// makes the circles little bubbles, with a glint.
void pinIcon(sg::PinType type, bool wired, bool bubble = false) {
    const float s = ImGui::GetTextLineHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(s, s));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = ImGui::GetColorU32(colourOf(type));
    const ImVec2 c(p.x + s * 0.5f, p.y + s * 0.5f);
    if (type == sg::PinType::Exec) {
        const ImVec2 a(c.x - s * 0.3f, c.y - s * 0.35f), b(c.x + s * 0.35f, c.y), d(c.x - s * 0.3f, c.y + s * 0.35f);
        if (wired) dl->AddTriangleFilled(a, b, d, col);
        else dl->AddTriangle(a, b, d, col, 1.5f);
    } else if (bubble) {
        const ImVec4 v = colourOf(type);
        dl->AddCircleFilled(c, s * 0.34f, ImGui::GetColorU32(ImVec4(v.x, v.y, v.z, wired ? 0.55f : 0.15f)));
        dl->AddCircle(c, s * 0.34f, col, 0, 1.3f);
        dl->AddCircleFilled(ImVec2(c.x - s * 0.11f, c.y - s * 0.12f), s * 0.07f, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.9f)));
    } else if (wired) {
        dl->AddCircleFilled(c, s * 0.3f, col);
    } else {
        dl->AddCircle(c, s * 0.3f, col, 0, 1.5f);
    }
}

// ---- Node themes: the editor's own look, soap bubbles (oval or square), or clear glass. The bubble and glass nodes are
// drawn here, behind their content; the editor's own node background and border are then left clear.

enum class Theme { Default, BubbleOval, BubbleSquare, Glass };

const char* const kThemeNames[] = {"default", "bubble(oval)", "bubble(square)", "glass"};

bool isBubble(Theme theme) { return theme == Theme::BubbleOval || theme == Theme::BubbleSquare; }

Theme themeOf(const std::string& name) {
    if (name == "bubble(oval)" || name == "bubble") return Theme::BubbleOval;
    if (name == "bubble(square)") return Theme::BubbleSquare;
    return name == "glass" ? Theme::Glass : Theme::Default;
}

ImU32 rgba(float r, float g, float b, float a) { return ImGui::GetColorU32(ImVec4(r, g, b, a)); }

ImU32 tint(ImVec4 c, float a) { return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, a)); }

// The editor's style for the theme, over its defaults (`defaults`: the style as the editor was made).
void applyTheme(Theme theme, const ed::Style& defaults) {
    ed::Style& st = ed::GetStyle();
    st = defaults;
    if (theme == Theme::Default) return;
    const bool bubble = isBubble(theme);
    st.NodeRounding = bubble ? 20.0f : 10.0f;
    st.NodePadding = bubble ? ImVec4(14, 8, 14, 12) : ImVec4(10, 6, 10, 9);
    st.NodeBorderWidth = 0.0f;
    st.HoveredNodeBorderWidth = 2.0f;
    st.SelectedNodeBorderWidth = 2.5f;
    st.Colors[ed::StyleColor_NodeBg] = ImVec4(0, 0, 0, 0);
    st.Colors[ed::StyleColor_NodeBorder] = ImVec4(0, 0, 0, 0);
    if (bubble) {
        st.Colors[ed::StyleColor_Bg] = ImVec4(0.08f, 0.15f, 0.24f, 1);
        st.Colors[ed::StyleColor_Grid] = ImVec4(0.55f, 0.8f, 1, 0.08f);
        // A bubble draws its own hover and selection rings round its film, not the editor's box.
        st.Colors[ed::StyleColor_HovNodeBorder] = ImVec4(0, 0, 0, 0);
        st.Colors[ed::StyleColor_SelNodeBorder] = ImVec4(0, 0, 0, 0);
    } else {
        st.Colors[ed::StyleColor_Bg] = ImVec4(0.09f, 0.1f, 0.13f, 1);
        st.Colors[ed::StyleColor_Grid] = ImVec4(1, 1, 1, 0.05f);
        st.Colors[ed::StyleColor_HovNodeBorder] = ImVec4(1, 1, 1, 0.55f);
        st.Colors[ed::StyleColor_SelNodeBorder] = ImVec4(0.75f, 0.9f, 1, 0.9f);
    }
}

// ---- Theme geometry: rounded outlines of our own (never ImGui's internal path), meshes filled with a
// colour per vertex, and the detail each takes from how big the node is on screen.

// A rounded rectangle's outline, clockwise from its top left corner, `segments` steps a corner. The
// rounding is clamped to half the shorter side, so small nodes stay well formed.
std::vector<ImVec2> roundedOutline(ImVec2 a, ImVec2 b, float r, int segments) {
    constexpr float kPi = 3.14159265f;
    std::vector<ImVec2> pts;
    r = std::max(0.0f, std::min(r, 0.5f * std::min(b.x - a.x, b.y - a.y)));
    segments = std::max(1, segments);
    const ImVec2 centres[4] = {{a.x + r, a.y + r}, {b.x - r, a.y + r}, {b.x - r, b.y - r}, {a.x + r, b.y - r}};
    const float start[4] = {kPi, 1.5f * kPi, 0.0f, 0.5f * kPi};
    pts.reserve(size_t(4 * (segments + 1)));
    for (int c = 0; c < 4; ++c)
        for (int s = 0; s <= segments; ++s) {
            const float angle = start[c] + 0.5f * kPi * float(s) / float(segments);
            pts.push_back({centres[c].x + std::cos(angle) * r, centres[c].y + std::sin(angle) * r});
        }
    return pts;
}

// The outline's points moved along their direction from `centre` by `by(i)`.
std::vector<ImVec2> pushed(const std::vector<ImVec2>& pts, ImVec2 centre, const std::function<float(size_t)>& by) {
    std::vector<ImVec2> out(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
        const float dx = pts[i].x - centre.x, dy = pts[i].y - centre.y;
        const float len = std::max(1e-3f, std::sqrt(dx * dx + dy * dy));
        const float d = by(i);
        out[i] = ImVec2(pts[i].x + dx / len * d, pts[i].y + dy / len * d);
    }
    return out;
}

void closedLine(ImDrawList* dl, const std::vector<ImVec2>& pts, ImU32 col, float thickness) {
    if (pts.size() >= 2) dl->AddPolyline(pts.data(), int(pts.size()), col, ImDrawFlags_Closed, thickness);
}

// The inside of a convex outline, in `rings` bands from its edge (ring 0) to `centre` (ring `rings`),
// each vertex coloured by `colour(point, ring)`: smooth gradients ImGui's own fills can't make.
void meshFill(ImDrawList* dl, const std::vector<ImVec2>& outline, ImVec2 centre, int rings,
              const std::function<ImU32(ImVec2, int)>& colour) {
    const int n = int(outline.size());
    if (n < 3 || rings < 1) return;
    dl->PrimReserve(n * 3 + (rings - 1) * n * 6, n * rings + 1);
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    const unsigned base = dl->_VtxCurrentIdx;  // read after PrimReserve, which may start a new vertex range
    for (int k = 0; k < rings; ++k) {
        const float s = 1.0f - float(k) / float(rings);
        for (int i = 0; i < n; ++i) {
            const ImVec2 p(centre.x + (outline[size_t(i)].x - centre.x) * s, centre.y + (outline[size_t(i)].y - centre.y) * s);
            dl->PrimWriteVtx(p, uv, colour(p, k));
        }
    }
    dl->PrimWriteVtx(centre, uv, colour(centre, rings));
    auto idx = [&](unsigned v) { dl->PrimWriteIdx(ImDrawIdx(base + v)); };
    for (int k = 0; k + 1 < rings; ++k)
        for (int i = 0; i < n; ++i) {
            const unsigned j = unsigned((i + 1) % n), a0 = unsigned(k * n + i), a1 = unsigned(k * n) + j;
            const unsigned b0 = unsigned((k + 1) * n + i), b1 = unsigned((k + 1) * n) + j;
            idx(a0), idx(a1), idx(b1), idx(a0), idx(b1), idx(b0);
        }
    const unsigned last = unsigned((rings - 1) * n), mid = unsigned(rings * n);
    for (int i = 0; i < n; ++i) idx(last + unsigned(i)), idx(last + unsigned((i + 1) % n)), idx(mid);
}

// Steps a rounded corner takes for its size on screen: few when zoomed out, more up close.
int cornerSteps(float rounding, float zoom) { return std::clamp(int(rounding * zoom / 3.0f), 2, 14); }

float wrapped(double seconds) { return float(std::fmod(seconds, 3600.0)); }

// The hue of a soap film at a point: bands that swirl with the angle round the bubble and drift in time.
float filmHue(ImVec2 p, ImVec2 centre, float t, float seed) {
    const float angle = std::atan2(p.y - centre.y, p.x - centre.x);
    const float h = 0.5f + 0.32f * std::sin(angle * 2.0f + t * 0.55f + seed) + 0.18f * std::sin(p.x * 0.031f - p.y * 0.024f + t * 0.35f) + t * 0.025f;
    return h - std::floor(h);
}

ImU32 hsv(float h, float s, float v, float a) {
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return rgba(r, g, b, a);
}

// How a bubble node is moving: its wobble when dragged or picked, from its place last frame.
struct Wobble {
    ImVec2 last{0, 0};
    float energy = 0;
    bool selected = false, seen = false;
};

// Points on the ellipse round `c` with radii `r`, from angle `from` to `to` (clockwise on screen).
std::vector<ImVec2> ellipseArc(ImVec2 c, ImVec2 r, float from, float to, int steps) {
    std::vector<ImVec2> pts(size_t(steps + 1));
    for (int i = 0; i <= steps; ++i) {
        const float angle = from + (to - from) * float(i) / float(steps);
        pts[size_t(i)] = ImVec2(c.x + std::cos(angle) * r.x, c.y + std::sin(angle) * r.y);
    }
    return pts;
}

// A soap bubble: a film (`oval`: an ellipse holding the node's contents, else the node's rounded box)
// whose colours swirl across it, strongest at the rim, a rainbow rim over a dark hairline, highlights and a
// star glint, little bubbles drifting round it, and a wobble when it's moved or picked. The kind tints its cap.
void drawBubble(ImDrawList* dl, ImVec2 a, ImVec2 b, float headerBottom, ImVec4 kind, bool oval, float rounding, float time,
                float zoom, float wobble, int seed, bool hovered, bool selected) {
    constexpr float kPi = 3.14159265f;
    const float w = b.x - a.x, h = b.y - a.y;
    if (w < 4 || h < 4) return;
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const float r = std::min(rounding, 0.5f * std::min(w, h));
    // An oval's radii pass just outside the contents' corners (the node less its padding), never too flat.
    float rx = w * 0.5f, ry = h * 0.5f;
    if (oval) {
        rx = std::max(rx, (w * 0.5f - 10.0f) * 1.4142f + 6.0f);
        ry = std::max(std::max(ry, (h * 0.5f - 6.0f) * 1.4142f + 6.0f), rx * 0.42f);
    }
    const ImVec2 radii(rx, ry);
    const float s = float(seed) * 1.618f;
    const bool simple = w * zoom < 60.0f;  // far out: just the film and its rim
    const int steps = std::clamp(int((rx + ry) * zoom / 4.0f), 24, 96);
    // The wobble: the outline rippling out and in round the bubble, dying away; and a slow breath.
    auto wob = [&](std::vector<ImVec2> pts) {
        for (ImVec2& p : pts) {
            const float dx = p.x - c.x, dy = p.y - c.y;
            const float len = std::max(1e-3f, std::sqrt(dx * dx + dy * dy));
            const float d = wobble * 3.5f * std::sin(std::atan2(dy, dx) * 3.0f + time * 13.0f + s) + 0.5f * std::sin(time * 1.4f + s);
            p = ImVec2(p.x + dx / len * d, p.y + dy / len * d);
        }
        return pts;
    };
    std::vector<ImVec2> outline;
    if (oval) {
        outline = wob(ellipseArc(c, radii, 0.0f, 2.0f * kPi, steps));
        outline.pop_back();  // the last point repeats the first
    } else
        outline = wob(roundedOutline(a, b, r, cornerSteps(r, zoom)));

    // The film: faint in the middle, colourful towards the rim.
    static const float kRingAlpha[] = {0.34f, 0.17f, 0.08f, 0.04f, 0.03f};
    meshFill(dl, outline, c, 4, [&](ImVec2 p, int ring) {
        return hsv(filmHue(p, c, time, s), 0.6f, 1.0f, kRingAlpha[std::min(ring, 4)]);
    });
    // The cap above the title's bottom, tinted by the kind.
    const float a0 = std::asin(std::clamp((headerBottom + 6 - c.y) / ry, -1.0f, 1.0f));
    if (!oval)
        dl->AddRectFilled(a, ImVec2(b.x, std::min(b.y, headerBottom + 6)), tint(kind, 0.3f), r, ImDrawFlags_RoundCornersTop);
    else if (kPi + 2 * a0 > 0.05f) {
        const std::vector<ImVec2> cap = wob(ellipseArc(c, radii, kPi - a0, 2 * kPi + a0, std::max(8, steps / 2)));
        dl->AddConvexPolyFilled(cap.data(), int(cap.size()), tint(kind, 0.3f));
    }

    // The rim: a dark hairline outside, then the bright film edge, each segment its own hue.
    closedLine(dl, pushed(outline, c, [](size_t) { return 1.2f; }), rgba(0.02f, 0.07f, 0.16f, 0.55f), 1.2f);
    const size_t n = outline.size();
    for (size_t i = 0; i < n; ++i)
        dl->AddLine(outline[i], outline[(i + 1) % n], hsv(filmHue(outline[i], c, time, s + 0.7f), 0.7f, 1.0f, 0.85f), 2.4f);
    if (selected)
        closedLine(dl, pushed(outline, c, [](size_t) { return 4.0f; }), rgba(1, 0.75f, 1, 0.95f), 2.5f);
    else if (hovered)
        closedLine(dl, pushed(outline, c, [](size_t) { return 4.0f; }), rgba(0.6f, 0.95f, 1, 0.8f), 2.0f);
    if (simple) return;
    closedLine(dl, pushed(outline, c, [](size_t) { return -3.0f; }), rgba(1, 1, 1, 0.12f), 1.0f);

    // Highlights: a soft window of light top left (a halo and its core), a curved shine along the upper
    // left of the rim, a star glint top right, and a faint reflection low on the right.
    const ImVec2 hl = oval ? ImVec2(c.x - rx * 0.42f, c.y - ry * 0.52f) : ImVec2(a.x + w * 0.27f, a.y + std::min(h * 0.17f, 24.0f));
    const ImVec2 halo = oval ? ImVec2(std::max(2.0f, std::min(rx * 0.3f, 60.0f)), std::max(1.5f, std::min(ry * 0.16f, 12.0f)))
                             : ImVec2(std::max(2.0f, std::min(w * 0.2f, 60.0f)), std::max(1.5f, std::min(h * 0.09f, 12.0f)));
    const float tilt = oval ? -0.45f : -0.22f;
    dl->AddEllipseFilled(hl, halo, rgba(1, 1, 1, 0.12f), tilt);
    dl->AddEllipseFilled(hl, ImVec2(halo.x * 0.55f, halo.y * 0.5f), rgba(1, 1, 1, 0.42f), tilt);
    std::vector<ImVec2> shine;
    if (oval)
        shine = wob(ellipseArc(c, ImVec2(std::max(1.0f, rx - 5), std::max(1.0f, ry - 5)), 1.08f * kPi, 1.42f * kPi, std::max(6, steps / 6)));
    else {
        shine = roundedOutline(ImVec2(a.x + 4, a.y + 4), ImVec2(b.x - 4, b.y - 4), std::max(0.0f, r - 4), cornerSteps(r, zoom));
        shine.resize(shine.size() / 4);  // the top left corner's points
    }
    if (shine.size() >= 2) dl->AddPolyline(shine.data(), int(shine.size()), rgba(1, 1, 1, 0.55f), 0, 2.4f);
    const ImVec2 star = oval ? ImVec2(c.x + rx * 0.52f, c.y - ry * 0.6f) : ImVec2(b.x - r * 0.7f - 2, a.y + r * 0.7f + 2);
    const float twinkle = 0.75f + 0.25f * std::sin(time * 3.1f + s);
    dl->AddLine(ImVec2(star.x - 6 * twinkle, star.y), ImVec2(star.x + 6 * twinkle, star.y), rgba(1, 1, 1, 0.8f), 1.2f);
    dl->AddLine(ImVec2(star.x, star.y - 6 * twinkle), ImVec2(star.x, star.y + 6 * twinkle), rgba(1, 1, 1, 0.8f), 1.2f);
    dl->AddCircleFilled(star, 2.2f, rgba(1, 1, 1, 0.95f));
    if (oval)
        dl->AddEllipseFilled(ImVec2(c.x + rx * 0.38f, c.y + ry * 0.66f), ImVec2(std::max(2.0f, std::min(rx * 0.2f, 36.0f)), 3.0f),
                             rgba(1, 1, 1, 0.14f), -0.4f);
    else
        dl->AddEllipseFilled(ImVec2(b.x - w * 0.24f, b.y - std::min(h * 0.09f, 11.0f)),
                             ImVec2(std::max(2.0f, std::min(w * 0.13f, 36.0f)), 3.0f), rgba(1, 1, 1, 0.14f), 0.12f);

    // Little bubbles drifting round the bubble, each on its own slow path.
    for (int k = 0; k < 3; ++k) {
        const float dir = k % 2 ? 1.0f : -1.0f;
        const float angle = s * 2.3f + float(k) * 2.1f + time * 0.12f * dir;
        const ImVec2 p(c.x + (rx + 14) * std::cos(angle), c.y + (ry + 12) * std::sin(angle) + 4 * std::sin(time * 1.6f + float(k) + s));
        const float rad = 3.0f + float((seed + k * 7) % 4);
        dl->AddCircleFilled(p, rad, rgba(0.75f, 0.92f, 1, 0.08f));
        dl->AddCircle(p, rad, hsv(filmHue(p, c, time, s + float(k)), 0.6f, 1.0f, 0.6f), 0, 1.0f);
        dl->AddCircleFilled(ImVec2(p.x - rad * 0.35f, p.y - rad * 0.35f), std::max(0.8f, rad * 0.22f), rgba(1, 1, 1, 0.85f));
    }
}

// A bubble node's wobble state; all of it goes when the canvas is made anew (another graph).
Wobble& wobbleOf(int generation, int node) {
    static int seenGeneration = -1;
    static std::map<int, Wobble> all;
    if (generation != seenGeneration) all.clear(), seenGeneration = generation;
    return all[node];
}

// Clear glass: whatever is behind the node shows through a pane faintly bright at the top, with a soft
// shadow round its edge only, a gloss band and diagonal streaks, a bevelled edge (light top left, dark
// bottom right) with a faint coloured fringe, and a rim glowing in the node's kind's colour.
void drawGlass(ImDrawList* dl, ImVec2 a, ImVec2 b, float headerBottom, ImVec4 kind, float rounding, float zoom) {
    const float w = b.x - a.x, h = b.y - a.y;
    if (w < 4 || h < 4) return;
    const float r = std::min(rounding, 0.5f * std::min(w, h));
    const bool simple = w * zoom < 60.0f;
    const int steps = cornerSteps(r, zoom);
    const std::vector<ImVec2> outline = roundedOutline(a, b, r, steps);
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);

    // The shadow: rings outside the pane, so nothing under the glass is darkened.
    for (int k = 1; k <= 3; ++k)
        closedLine(dl, pushed(outline, c, [k](size_t) { return float(k) * 2.0f; }), rgba(0, 0, 0, 0.16f / float(k)), 2.0f);
    meshFill(dl, outline, c, 2, [&](ImVec2 p, int) {
        const float t = std::clamp((p.y - a.y) / h, 0.0f, 1.0f);
        return rgba(0.86f, 0.93f, 1, 0.1f - 0.07f * t);
    });
    dl->AddRectFilled(a, ImVec2(b.x, std::min(b.y, headerBottom + 5)), tint(kind, 0.4f), r, ImDrawFlags_RoundCornersTop);
    if (!simple) {
        dl->AddRectFilledMultiColor(ImVec2(a.x + r * 0.7f, a.y + 2), ImVec2(b.x - r * 0.7f, a.y + h * 0.38f), rgba(1, 1, 1, 0.16f),
                                    rgba(1, 1, 1, 0.16f), rgba(1, 1, 1, 0), rgba(1, 1, 1, 0));
        dl->AddQuadFilled(ImVec2(a.x + w * 0.14f, a.y + 2), ImVec2(a.x + w * 0.27f, a.y + 2), ImVec2(a.x + w * 0.15f, b.y - 2),
                          ImVec2(a.x + w * 0.04f, b.y - 2), rgba(1, 1, 1, 0.06f));
        dl->AddQuadFilled(ImVec2(a.x + w * 0.31f, a.y + 2), ImVec2(a.x + w * 0.35f, a.y + 2), ImVec2(a.x + w * 0.23f, b.y - 2),
                          ImVec2(a.x + w * 0.19f, b.y - 2), rgba(1, 1, 1, 0.05f));
    }

    // The bevel: the inner edge lit along the top and left, shaded along the bottom and right.
    const std::vector<ImVec2> inner = pushed(outline, c, [](size_t) { return -1.5f; });
    const size_t quarter = inner.size() / 4;
    if (quarter >= 1) {
        std::vector<ImVec2> lit(inner.begin(), inner.begin() + std::ptrdiff_t(quarter * 2));
        std::vector<ImVec2> shade(inner.begin() + std::ptrdiff_t(quarter * 2), inner.end());
        dl->AddPolyline(lit.data(), int(lit.size()), rgba(1, 1, 1, 0.55f), 0, 1.3f);
        dl->AddPolyline(shade.data(), int(shade.size()), rgba(0, 0, 0, 0.35f), 0, 1.3f);
    }
    if (!simple) {
        // A faint fringe, as thick glass splits light at its edge.
        std::vector<ImVec2> cyan(outline), magenta(outline);
        for (ImVec2& p : cyan) p.x -= 0.8f, p.y -= 0.8f;
        for (ImVec2& p : magenta) p.x += 0.8f, p.y += 0.8f;
        closedLine(dl, cyan, rgba(0.3f, 0.95f, 1, 0.18f), 1.0f);
        closedLine(dl, magenta, rgba(1, 0.35f, 0.9f, 0.15f), 1.0f);
        closedLine(dl, pushed(outline, c, [](size_t) { return 2.0f; }), tint(kind, 0.22f), 3.5f);
    }
    closedLine(dl, outline, tint(ImVec4(std::min(1.0f, kind.x + 0.25f), std::min(1.0f, kind.y + 0.25f), std::min(1.0f, kind.z + 0.25f), 1), 0.85f), 1.5f);
}

bool wiredInto(const sg::Graph& g, int node, const std::string& pin) {
    for (const sg::Link& l : g.links)
        if (l.toNode == node && l.toPin == pin) return true;
    return false;
}

bool wiredFrom(const sg::Graph& g, int node, const std::string& pin) {
    for (const sg::Link& l : g.links)
        if (l.fromNode == node && l.fromPin == pin) return true;
    return false;
}

// An input's typed value, as its node shows it, with an arrow for its dropdown when it has choices.
bool valueField(Editor& e, sg::Node& n, const sg::PinDef& p, size_t index) {
    auto it = n.values.find(p.name);
    std::string value = it != n.values.end() ? it->second : p.value;
    const std::string id = "##v" + std::to_string(index);
    bool edited = false;
    if (p.type == sg::PinType::Bool) {
        bool on = value == "true" || value == "1";
        if (ImGui::Checkbox(id.c_str(), &on)) value = on ? "true" : "false", edited = true;
    } else {
        const bool wide = p.type == sg::PinType::String || p.type == sg::PinType::Vector || p.type == sg::PinType::Actor ||
                          p.type == sg::PinType::Place;
        ImGui::SetNextItemWidth(p.name == "code" ? em(220) : wide ? em(110) : em(60));
        const char* hint = p.type == sg::PinType::Vector  ? "x y z"
                           : p.type == sg::PinType::Place ? "x y z"
                           : p.type == sg::PinType::Actor ? "nearest"
                           : p.optional                   ? "(none)"
                                                          : "";
        edited = inputTextWithHint(id.c_str(), hint, value);
        if (!p.choices.empty()) {
            ImGui::SameLine(0, 2);
            if (ImGui::ArrowButton(("##c" + std::to_string(index)).c_str(), ImGuiDir_Down))
                e.choiceNode = n.id, e.choicePin = p.name, e.openChoices = true;
        }
    }
    if (edited) n.values[p.name] = value;
    return edited;
}

std::string lowerCopy(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

bool validName(const std::string& s) {
    if (s.empty() || s.size() > 60) return false;
    for (char c : s)
        if (!(std::islower(uint8_t(c)) || std::isdigit(uint8_t(c)) || c == '_')) return false;
    return true;
}

fs::path modPath(const std::string& s) { return trim(s).empty() ? fs::path() : fromUtf8(trim(s)); }

void note(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

std::vector<std::string> graphsIn(const fs::path& mod) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(mod / "graphs", ec))
        if (entry.path().extension() == ".json") out.push_back(entry.path().stem().string());
    std::sort(out.begin(), out.end());
    return out;
}

// Links `node`'s first pin that fits to the dragged pin.
void autoLink(Editor& e, const sg::Node& node) {
    const sg::NodeDef* d = sg::findDef(node.def);
    const sg::Node* other = e.graph.node(e.dragNode);
    const sg::NodeDef* od = other ? sg::findDef(other->def) : nullptr;
    if (!d || !od) return;
    const size_t oi = pinIndex(*od, e.dragPin, e.dragOutput);
    if (oi >= side(*od, e.dragOutput).size()) return;
    const sg::PinType ot = side(*od, e.dragOutput)[oi].type;
    for (const sg::PinDef& p : side(*d, !e.dragOutput)) {
        if (p.field) continue;
        const bool fits = e.dragOutput ? sg::compatible(ot, p.type) : sg::compatible(p.type, ot);
        if (!fits) continue;
        sg::Link l;
        l.id = e.graph.nextId++;
        if (e.dragOutput) l.fromNode = other->id, l.fromPin = e.dragPin, l.toNode = node.id, l.toPin = p.name;
        else l.fromNode = node.id, l.fromPin = p.name, l.toNode = other->id, l.toPin = e.dragPin;
        // A value input and a white-wire output take one wire each.
        const bool execOut = ot == sg::PinType::Exec && e.dragOutput;
        std::erase_if(e.graph.links, [&](const sg::Link& x) {
            return (x.toNode == l.toNode && x.toPin == l.toPin && p.type != sg::PinType::Exec) ||
                   (execOut && x.fromNode == l.fromNode && x.fromPin == l.fromPin);
        });
        e.graph.links.push_back(l);
        return;
    }
}

} // namespace

void App::drawScriptGraph() {
    ToolWindows& t = tools_;
    Editor& e = editor();
    if (!t.showGraph) return;
    ImGui::SetNextWindowSize(ImVec2(em(1200), em(760)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Script graphs", &t.showGraph)) {
        ImGui::End();
        return;
    }
    if (!e.context) newCanvas(e);
    const fs::path mod = modPath(t.graphMod);
    if (e.modSeen != t.graphMod) {
        e.modSeen = t.graphMod;
        e.list = mod.empty() ? std::vector<std::string>{} : graphsIn(mod);
    }

    auto open = [&](const std::string& name) {
        try {
            e.graph = sg::load(mod / "graphs" / (name + ".json"));
        } catch (std::exception& ex) {
            error("Opening graph " + name + ": " + ex.what());
            return;
        }
        e.name = name;
        newCanvas(e);
        e.place.clear();
        for (const sg::Node& n : e.graph.nodes) e.place.push_back(n.id);
        e.dirty = false;
        e.changed = e.navigate = true;
    };
    auto save = [&]() -> bool {
        if (e.name.empty() || mod.empty()) return false;
        ed::SetCurrentEditor(e.context);
        for (sg::Node& n : e.graph.nodes) {
            const ImVec2 p = ed::GetNodePosition(nodeKey(n.id));
            if (p.x < 1e30f && p.x > -1e30f) n.x = p.x, n.y = p.y;
        }
        ed::SetCurrentEditor(nullptr);
        try {
            const fs::path json = mod / "graphs" / (e.name + ".json"), script = mod / "scripts" / (e.name + ".lua");
            std::error_code ec;
            if (fs::is_regular_file(script, ec)) {
                const std::string first = readText(script).substr(0, std::string(sg::kLuaMark).size());
                if (first != sg::kLuaMark) {
                    error("scripts\\" + e.name + ".lua is a hand-written script: give the graph another name");
                    return false;
                }
            }
            fs::create_directories(json.parent_path());
            fs::create_directories(script.parent_path());
            writeText(json, sg::toJson(e.graph));
            std::vector<std::string> problems;
            writeText(script, sg::toLua(e.graph, e.name, problems));
            e.dirty = false;
            e.list = graphsIn(mod);
            status("Saved graphs\\" + e.name + ".json and scripts\\" + e.name + ".lua" +
                   (problems.empty() ? "" : " (" + std::to_string(problems.size()) + " problem(s): see Problems)"));
            return true;
        } catch (std::exception& ex) {
            error("Saving graph " + e.name + ": " + ex.what());
            return false;
        }
    };

    // ---- the left side: the mod, its graphs, the variables and the problems
    ImGui::BeginChild("side", ImVec2(em(300), 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    note("A graph is made into one of the mod's scripts: graphs\\<name>.json becomes scripts\\<name>.lua when saved. "
         "Right-click the canvas (or drop a wire on it) for nodes; Delete removes the selected ones.");
    pathField(*this, "Mod folder##graph", t.graphMod, PathPick::Folder, {}, "mods\\<your mod>");
    {
        static const char* const kThemeLabels[] = {"Default", "Bubble(oval)", "Bubble(square)", "Glass"};
        int theme = int(themeOf(t.graphTheme));
        ImGui::SetNextItemWidth(em(120));
        if (ImGui::Combo("Node theme##graph", &theme, kThemeLabels, 4)) t.graphTheme = kThemeNames[theme];
        helpMarker("How the nodes look: the editor's own, soap bubbles (a clear film with a shimmering rainbow rim) as ovals "
                   "or rounded squares, or clear glass. Only the look changes.");
    }
    ImGui::SeparatorText("Graphs");
    for (const std::string& name : e.list) {
        const bool current = name == e.name;
        if (ImGui::Selectable((name + (current && e.dirty ? " *" : "") + "##" + name).c_str(), current) && !current) {
            if (e.dirty) e.pendingOpen = name, ImGui::OpenPopup("Unsaved graph");
            else open(name);
        }
    }
    if (e.list.empty()) ImGui::TextDisabled(mod.empty() ? "Pick a mod folder." : "No graphs yet.");
    ImGui::SetNextItemWidth(em(160));
    inputText("##newgraph", e.newName);
    ImGui::SameLine();
    ImGui::BeginDisabled(mod.empty() || !validName(e.newName) ||
                         std::find(e.list.begin(), e.list.end(), e.newName) != e.list.end());
    if (ImGui::Button("New graph")) {
        e.graph = sg::Graph{};
        e.name = e.newName;
        newCanvas(e);
        e.place.clear();
        e.dirty = e.changed = true;
        save();
    }
    ImGui::EndDisabled();
    helpMarker("A name of a-z, 0-9 and _ (the script's file name): start it with your mod's name.");
    ImGui::BeginDisabled(e.name.empty());
    if (ImGui::Button("Save##graph")) save();
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal("Unsaved graph", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s has unsaved changes.", e.name.c_str());
        if (ImGui::Button("Save it")) {
            if (save()) open(e.pendingOpen);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Drop them")) {
            open(e.pendingOpen);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (!e.name.empty()) {
        ImGui::SeparatorText("Variables");
        helpMarker("Values the script keeps between events. Get variable and Set variable nodes name them.");
        static const char* const kTypes[] = {"any", "bool", "num", "str", "vec", "part", "vehicle", "npc", "table", "actor", "place"};
        constexpr int kTypeCount = int(std::size(kTypes));
        for (size_t i = 0; i < e.graph.variables.size(); ++i) {
            sg::Variable& v = e.graph.variables[i];
            ImGui::PushID(int(i));
            ImGui::SetNextItemWidth(em(90));
            if (inputText("##name", v.name)) e.dirty = e.changed = true;
            ImGui::SameLine();
            int type = 0;
            for (int k = 0; k < kTypeCount; ++k)
                if (sg::typeFromName(kTypes[k]) == v.type) type = k;
            ImGui::SetNextItemWidth(em(70));
            if (ImGui::Combo("##type", &type, kTypes, kTypeCount)) v.type = sg::typeFromName(kTypes[type]), e.dirty = e.changed = true;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(em(60));
            if (inputTextWithHint("##value", "start", v.value)) e.dirty = e.changed = true;
            ImGui::SameLine();
            const bool remove = ImGui::SmallButton("x");
            ImGui::PopID();
            if (remove) {
                e.graph.variables.erase(e.graph.variables.begin() + std::ptrdiff_t(i));
                e.dirty = e.changed = true;
                break;
            }
        }
        if (ImGui::SmallButton("Add variable")) {
            e.graph.variables.push_back({"myvar" + std::to_string(e.graph.variables.size() + 1), sg::PinType::Number, "0"});
            e.dirty = e.changed = true;
        }
        if (e.changed) {
            e.problems.clear();
            e.lua = sg::toLua(e.graph, e.name, e.problems);
            e.changed = false;
        }
        ImGui::SeparatorText("Problems");
        if (e.problems.empty()) ImGui::TextDisabled("None.");
        for (const std::string& p : e.problems) ImGui::TextWrapped("- %s", p.c_str());
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // ---- the right side: the canvas and the Lua it makes
    ImGui::BeginChild("main");
    if (e.name.empty()) {
        ImGui::TextDisabled("Open or make a graph on the left.");
    } else if (ImGui::BeginTabBar("graphtabs")) {
        if (ImGui::BeginTabItem("Graph")) {
            ed::SetCurrentEditor(e.context);
            const Theme theme = themeOf(t.graphTheme);
            const bool bubbles = isBubble(theme);
            applyTheme(theme, e.defaults);
            // The canvas's place on screen, to skip drawing the themes of nodes outside it.
            const ImVec2 viewMin = ImGui::GetCursorScreenPos();
            const ImVec2 viewSize = ImGui::GetContentRegionAvail();
            ed::Begin("canvas");
            const ImVec2 seenMin = ed::ScreenToCanvas(viewMin);
            const ImVec2 seenMax = ed::ScreenToCanvas(ImVec2(viewMin.x + viewSize.x, viewMin.y + viewSize.y));
            const float zoom = std::max(0.05f, ed::GetCurrentZoom());
            const float time = wrapped(ImGui::GetTime());
            for (int id : e.place)
                if (const sg::Node* n = e.graph.node(id)) ed::SetNodePosition(nodeKey(id), ImVec2(n->x, n->y));
            e.place.clear();

            for (sg::Node& n : e.graph.nodes) {
                const sg::NodeDef* d = sg::findDef(n.def);
                ed::BeginNode(nodeKey(n.id));
                ImGui::PushID(n.id);
                ImGui::TextUnformatted(d ? d->title.c_str() : ("? " + n.def).c_str());
                const float headerBottom = ImGui::GetItemRectMax().y;
                if (d) {
                    ImGui::BeginGroup();
                    for (size_t i = 0; i < d->inputs.size(); ++i) {
                        const sg::PinDef& p = d->inputs[i];
                        if (p.field) {
                            ImGui::TextUnformatted(p.name.c_str());
                            ImGui::SameLine();
                            if (valueField(e, n, p, i)) e.dirty = e.changed = true;
                            continue;
                        }
                        const bool wired = wiredInto(e.graph, n.id, p.name);
                        ed::BeginPin(pinKey(n.id, i, false), ed::PinKind::Input);
                        ed::PinPivotAlignment(ImVec2(0.1f, 0.5f));
                        pinIcon(p.type, wired, bubbles);
                        ed::EndPin();
                        if (p.type != sg::PinType::Exec || p.name != "in") {
                            ImGui::SameLine();
                            ImGui::TextUnformatted(p.name.c_str());
                        }
                        if (!wired && p.type != sg::PinType::Exec && p.type != sg::PinType::Part && p.type != sg::PinType::Vehicle &&
                            p.type != sg::PinType::Npc) {
                            ImGui::SameLine();
                            if (valueField(e, n, p, i)) e.dirty = e.changed = true;
                        }
                    }
                    ImGui::EndGroup();
                    ImGui::SameLine(0, em(16));
                    ImGui::BeginGroup();
                    for (size_t i = 0; i < d->outputs.size(); ++i) {
                        const sg::PinDef& p = d->outputs[i];
                        if (p.type != sg::PinType::Exec || p.name != "then") {
                            ImGui::TextUnformatted(p.name.c_str());
                            ImGui::SameLine();
                        }
                        ed::BeginPin(pinKey(n.id, i, true), ed::PinKind::Output);
                        ed::PinPivotAlignment(ImVec2(0.9f, 0.5f));
                        pinIcon(p.type, wiredFrom(e.graph, n.id, p.name), bubbles);
                        ed::EndPin();
                    }
                    ImGui::EndGroup();
                }
                ImGui::PopID();
                ed::EndNode();
                // Behind the node: its theme's look, or the editor's own with a header band in its kind's colour.
                const ImVec2 pos = ed::GetNodePosition(nodeKey(n.id)), size = ed::GetNodeSize(nodeKey(n.id));
                const ImVec2 end(pos.x + size.x, pos.y + size.y);
                // Room for a bubble's ellipse and drifting bubbles past the node, and the glass's shadow.
                const float margin = 40.0f + 0.3f * std::max(size.x, size.y);
                const bool onScreen = end.x + margin >= seenMin.x && pos.x - margin <= seenMax.x && end.y + margin >= seenMin.y &&
                                      pos.y - margin <= seenMax.y;
                if (size.x > 0 && size.y > 0 && onScreen) {
                    ImDrawList* bg = ed::GetNodeBackgroundDrawList(nodeKey(n.id));
                    const float rounding = ed::GetStyle().NodeRounding;
                    if (isBubble(theme)) {
                        // Dragging it or picking it sets it wobbling, which dies away.
                        Wobble& wb = wobbleOf(e.generation, n.id);
                        const bool selected = ed::IsNodeSelected(nodeKey(n.id));
                        if (wb.seen) {
                            const float dx = pos.x - wb.last.x, dy = pos.y - wb.last.y;
                            wb.energy += std::min(0.5f, std::sqrt(dx * dx + dy * dy) * 0.05f);
                            if (selected != wb.selected) wb.energy += 0.6f;
                        }
                        wb.energy = std::min(wb.energy, 1.5f) * std::exp(-ImGui::GetIO().DeltaTime * 3.0f);
                        wb.last = pos, wb.selected = selected, wb.seen = true;
                        const bool hovered = ed::GetHoveredNode().Get() == nodeKey(n.id);
                        drawBubble(bg, pos, end, headerBottom, headerOf(d), theme == Theme::BubbleOval, rounding, time, zoom,
                                   wb.energy, n.id, hovered, selected);
                    } else if (theme == Theme::Glass) {
                        drawGlass(bg, pos, end, headerBottom, headerOf(d), rounding, zoom);
                    } else
                        bg->AddRectFilled(ImVec2(pos.x + 1, pos.y + 1), ImVec2(end.x - 1, headerBottom + 4), ImGui::GetColorU32(headerOf(d)),
                                          rounding, ImDrawFlags_RoundCornersTop);
                }
            }

            for (const sg::Link& l : e.graph.links) {
                const sg::Node* from = e.graph.node(l.fromNode);
                const sg::Node* to = e.graph.node(l.toNode);
                const sg::NodeDef* fd = from ? sg::findDef(from->def) : nullptr;
                const sg::NodeDef* td = to ? sg::findDef(to->def) : nullptr;
                if (!fd || !td) continue;
                const size_t fi = pinIndex(*fd, l.fromPin, true), ti = pinIndex(*td, l.toPin, false);
                if (fi >= fd->outputs.size() || ti >= td->inputs.size()) continue;
                const sg::PinType type = fd->outputs[fi].type;
                ed::Link(linkKey(l.id), pinKey(l.fromNode, fi, true), pinKey(l.toNode, ti, false), colourOf(type),
                         type == sg::PinType::Exec ? 3.0f : 2.0f);
            }

            // Wires drawn between pins, or dropped on the canvas (which offers the nodes that fit).
            bool openMenu = false;
            if (ed::BeginCreate(ImVec4(1, 1, 1, 1), 2.0f)) {
                ed::PinId a, b;
                if (ed::QueryNewLink(&a, &b) && a && b) {
                    int an = 0, bn = 0;
                    std::string ap, bp;
                    bool ao = false, bo = false;
                    sg::PinType at = sg::PinType::Any, bt = sg::PinType::Any;
                    const bool ok = pinOf(e.graph, a.Get(), an, ap, ao, at) && pinOf(e.graph, b.Get(), bn, bp, bo, bt) && ao != bo && an != bn;
                    if (ok && !ao) std::swap(an, bn), std::swap(ap, bp), std::swap(at, bt);
                    if (!ok || !sg::compatible(at, bt)) {
                        ed::RejectNewItem(ImVec4(1, 0.3f, 0.3f, 1), 2.0f);
                    } else if (ed::AcceptNewItem(ImVec4(0.5f, 1, 0.5f, 1), 3.0f)) {
                        std::erase_if(e.graph.links, [&](const sg::Link& x) {
                            return (x.toNode == bn && x.toPin == bp && bt != sg::PinType::Exec) ||
                                   (at == sg::PinType::Exec && x.fromNode == an && x.fromPin == ap);
                        });
                        e.graph.links.push_back({e.graph.nextId++, an, bn, ap, bp});
                        e.dirty = e.changed = true;
                    }
                }
                ed::PinId dropped;
                if (ed::QueryNewNode(&dropped)) {
                    int dn = 0;
                    std::string dp;
                    bool dout = false;
                    sg::PinType dt = sg::PinType::Any;
                    if (pinOf(e.graph, dropped.Get(), dn, dp, dout, dt) && ed::AcceptNewItem()) {
                        e.dragNode = dn, e.dragPin = dp, e.dragOutput = dout;
                        openMenu = true;
                    }
                }
            }
            ed::EndCreate();

            if (ed::BeginDelete()) {
                ed::LinkId l;
                while (ed::QueryDeletedLink(&l))
                    if (ed::AcceptDeletedItem()) {
                        std::erase_if(e.graph.links, [&](const sg::Link& x) { return linkKey(x.id) == l.Get(); });
                        e.dirty = e.changed = true;
                    }
                ed::NodeId nid;
                while (ed::QueryDeletedNode(&nid))
                    if (ed::AcceptDeletedItem()) {
                        const int id = int(nid.Get() / 64);
                        std::erase_if(e.graph.links, [&](const sg::Link& x) { return x.fromNode == id || x.toNode == id; });
                        std::erase_if(e.graph.nodes, [&](const sg::Node& x) { return x.id == id; });
                        e.dirty = e.changed = true;
                    }
            }
            ed::EndDelete();

            // Inside the canvas the mouse is already in canvas coordinates (the editor remaps it).
            const ImVec2 mouse = ImGui::GetMousePos();
            const ed::NodeId hovered = ed::GetHoveredNode();
            ed::Suspend();
            if (ed::ShowBackgroundContextMenu()) {
                e.dragNode = 0;
                openMenu = true;
            }
            if (openMenu) {
                e.menuAt = mouse;
                e.filter.clear();
                ImGui::OpenPopup("Add node");
            }
            if (hovered && !ImGui::IsPopupOpen("Add node")) {
                const sg::Node* n = e.graph.node(int(hovered.Get() / 64));
                const sg::NodeDef* d = n ? sg::findDef(n->def) : nullptr;
                if (d && !d->help.empty()) ImGui::SetTooltip("%s", d->help.c_str());
            }
            ImGui::SetNextWindowSizeConstraints(ImVec2(em(260), 0), ImVec2(em(360), em(520)));
            if (ImGui::BeginPopup("Add node")) {
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                ImGui::SetNextItemWidth(-1);
                inputTextWithHint("##filter", "search", e.filter);
                // The dragged pin's type, when a wire was dropped: only nodes that fit it are listed.
                sg::PinType dragType = sg::PinType::Any;
                bool dragging = false;
                if (const sg::Node* dn = e.dragNode ? e.graph.node(e.dragNode) : nullptr)
                    if (const sg::NodeDef* dd = sg::findDef(dn->def)) {
                        const size_t i = pinIndex(*dd, e.dragPin, e.dragOutput);
                        if (i < side(*dd, e.dragOutput).size()) dragType = side(*dd, e.dragOutput)[i].type, dragging = true;
                    }
                const std::string f = lowerCopy(trim(e.filter));
                std::string category;
                bool close = false;
                ImGui::BeginChild("nodes", ImVec2(0, em(420)));
                for (const sg::NodeDef& d : sg::catalogue()) {
                    if (!f.empty() && lowerCopy(d.title + " " + d.category + " " + d.id).find(f) == std::string::npos) continue;
                    if (dragging) {
                        bool fits = false;
                        for (const sg::PinDef& p : side(d, !e.dragOutput))
                            if (!p.field && (e.dragOutput ? sg::compatible(dragType, p.type) : sg::compatible(p.type, dragType))) fits = true;
                        if (!fits) continue;
                    }
                    if (d.category != category) {
                        category = d.category;
                        ImGui::SeparatorText(category.c_str());
                    }
                    if (ImGui::Selectable((d.title + "##" + d.id).c_str())) {
                        sg::Node n;
                        n.id = e.graph.nextId++;
                        n.def = d.id;
                        n.x = e.menuAt.x, n.y = e.menuAt.y;
                        e.graph.nodes.push_back(n);
                        e.place.push_back(n.id);
                        if (dragging) autoLink(e, e.graph.nodes.back());
                        e.dirty = e.changed = true;
                        close = true;
                    }
                    if (ImGui::IsItemHovered() && !d.help.empty()) ImGui::SetTooltip("%s", d.help.c_str());
                }
                ImGui::EndChild();
                if (close) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            // An input's dropdown: picking a choice types it in.
            if (e.openChoices) ImGui::OpenPopup("Pin choices"), e.openChoices = false;
            if (ImGui::BeginPopup("Pin choices")) {
                sg::Node* cn = e.graph.node(e.choiceNode);
                const sg::NodeDef* cd = cn ? sg::findDef(cn->def) : nullptr;
                const size_t ci = cd ? pinIndex(*cd, e.choicePin, false) : 0;
                if (!cd || ci >= cd->inputs.size()) ImGui::CloseCurrentPopup();
                else
                    for (const std::string& c : cd->inputs[ci].choices)
                        if (ImGui::Selectable(c.c_str())) {
                            cn->values[e.choicePin] = c;
                            e.dirty = e.changed = true;
                        }
                ImGui::EndPopup();
            }
            ed::Resume();
            if (e.navigate) ed::NavigateToContent(0.0f), e.navigate = false;
            ed::End();
            ed::SetCurrentEditor(nullptr);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Lua")) {
            note("The script this graph makes (written to scripts\\<name>.lua when you save).");
            std::string shown = e.lua;
            inputTextMultiline("##lua", shown, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::End();
}

#endif

} // namespace nut::gui
