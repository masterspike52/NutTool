#include "font.hpp"

#include "deflate.hpp"
#include "image.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace nut::font {
namespace {

constexpr uint32_t kPageCells = 32; // cells per page row/column

Bytes unpackCell(const Bytes& raw, uint32_t cell) {
    Bytes px(size_t(cell) * cell);
    for (size_t i = 0; i < px.size(); ++i) px[i] = (raw[i / 2] >> ((i & 1) * 4)) & 15;
    return px;
}

Bytes packCell(const Bytes& px) {
    Bytes raw((px.size() + 1) / 2, 0);
    for (size_t i = 0; i < px.size(); ++i) raw[i / 2] |= uint8_t((px[i] & 15) << ((i & 1) * 4));
    return raw;
}

std::string pageName(size_t page) {
    char t[32];
    std::snprintf(t, sizeof t, "page_%03zu.png", page);
    return t;
}

} // namespace

bool looksLikeFont(const Bytes& d) {
    if (d.size() < 16 || d[0] != 1 || d[1] != 1) return false;
    uint32_t n = be16(&d[4]);
    if (n == 0 || 6 + size_t(n) * 10 > d.size()) return false;
    uint32_t first = be32(&d[8]);
    return first == 6 + n * 10 && d[first] == 0x78;
}

Font Font::parse(const Bytes& d) {
    if (!looksLikeFont(d)) fail("not an XUI font cache (.sbp)");
    Font f;
    std::memcpy(f.header, d.data(), 4);
    if (f.header[3] != 4) fail("only 4-bit font caches are supported");
    uint32_t n = be16(&d[4]);
    struct Rec { uint16_t code; uint32_t off; int8_t m[4]; };
    std::vector<Rec> recs(n);
    for (uint32_t i = 0; i < n; ++i) {
        const uint8_t* p = &d[6 + i * 10];
        recs[i].code = be16(p);
        recs[i].off = be32(p + 2);
        for (int k = 0; k < 4; ++k) recs[i].m[k] = int8_t(p[6 + k]);
    }
    for (uint32_t i = 0; i < n; ++i) {
        size_t end = i + 1 < n ? recs[i + 1].off : d.size();
        if (recs[i].off > end || end > d.size()) fail("font: glyph data out of order");
        Glyph g;
        g.code = recs[i].code;
        std::memcpy(g.metrics, recs[i].m, 4);
        g.packed.assign(d.begin() + recs[i].off, d.begin() + end);
        Bytes raw = deflate::zlibDecompress(g.packed.data(), g.packed.size());
        if (f.cellSize == 0) f.cellSize = uint32_t(std::lround(std::sqrt(double(raw.size()) * 2)));
        if (raw.size() * 2 != size_t(f.cellSize) * f.cellSize) fail("font: inconsistent glyph cell size");
        g.pixels = unpackCell(raw, f.cellSize);
        f.glyphs.push_back(std::move(g));
    }
    return f;
}

Bytes Font::serialize() const {
    Writer w;
    w.bytes(header, 4);
    w.u16(uint16_t(glyphs.size()));
    uint32_t off = uint32_t(6 + glyphs.size() * 10);
    std::vector<Bytes> streams;
    for (auto& g : glyphs) {
        if (!g.packed.empty()) {
            streams.push_back(g.packed);
            continue;
        }
        Bytes raw = packCell(g.pixels);
        streams.push_back(deflate::zlibCompress(raw.data(), raw.size()));
    }
    for (size_t i = 0; i < glyphs.size(); ++i) {
        w.u16(glyphs[i].code);
        w.u32(off);
        for (int k = 0; k < 4; ++k) w.u8(uint8_t(glyphs[i].metrics[k]));
        off += uint32_t(streams[i].size());
    }
    for (auto& s : streams) w.bytes(s);
    return w.buf();
}

void exportTo(const Font& f, const fs::path& dir) {
    fs::create_directories(longPath(dir));
    Json j = Json::object();
    j["lineHeight"] = unsigned(f.header[2]);
    j["header"] = Json::array();
    for (int k = 0; k < 4; ++k) j["header"].push(unsigned(f.header[k]));
    j["cellSize"] = f.cellSize;
    j["cellsPerRow"] = kPageCells;
    j["note"] = "Glyph pixels are white with alpha = coverage (16 levels). metrics = 4 signed bytes from the game (bearing/advance).";
    Json glyphs = Json::array();
    size_t perPage = kPageCells * kPageCells;
    size_t pages = (f.glyphs.size() + perPage - 1) / perPage;
    for (size_t p = 0; p < pages; ++p) {
        image::Rgba img;
        img.width = img.height = kPageCells * f.cellSize;
        img.pixels.assign(size_t(img.width) * img.height * 4, 0);
        for (size_t c = 0; c < perPage && p * perPage + c < f.glyphs.size(); ++c) {
            const Glyph& g = f.glyphs[p * perPage + c];
            uint32_t ox = uint32_t(c % kPageCells) * f.cellSize, oy = uint32_t(c / kPageCells) * f.cellSize;
            for (uint32_t y = 0; y < f.cellSize; ++y)
                for (uint32_t x = 0; x < f.cellSize; ++x) {
                    uint8_t* o = &img.pixels[((size_t(oy) + y) * img.width + ox + x) * 4];
                    o[0] = o[1] = o[2] = 255;
                    o[3] = uint8_t(g.pixels[size_t(y) * f.cellSize + x] * 17);
                }
            Json e = Json::object();
            char cp[16];
            std::snprintf(cp, sizeof cp, "U+%04X", g.code);
            e["code"] = cp;
            e["page"] = unsigned(p);
            e["cell"] = unsigned(c);
            Json m = Json::array();
            for (int k = 0; k < 4; ++k) m.push(int(g.metrics[k]));
            e["metrics"] = m;
            glyphs.push(e);
        }
        writeFile(dir / pageName(p), image::encodePng(img));
    }
    j["glyphs"] = glyphs;
    writeText(dir / "font.json", j.dump());
}

Font importFrom(const fs::path& dir, const Font* base) {
    Json j = Json::parse(readText(dir / "font.json"));
    std::map<uint16_t, const Glyph*> original;
    if (base) for (auto& g : base->glyphs) original[g.code] = &g;
    Font f;
    for (int k = 0; k < 4; ++k) f.header[k] = uint8_t(j["header"][size_t(k)].asU32());
    f.header[2] = uint8_t(j["lineHeight"].asU32());
    f.cellSize = j["cellSize"].asU32();
    uint32_t perRow = j["cellsPerRow"].asU32();
    std::vector<image::Rgba> pages;
    for (auto& e : j["glyphs"].items()) {
        Glyph g;
        std::string code = e["code"].asString();
        g.code = uint16_t(parseHex(startsWith(code, "U+") ? code.substr(2) : code));
        for (int k = 0; k < 4; ++k) g.metrics[k] = int8_t(e["metrics"][size_t(k)].asNumber());
        size_t page = e["page"].asU32(), cell = e["cell"].asU32();
        while (pages.size() <= page) {
            fs::path pp = dir / pageName(pages.size());
            pages.push_back(fs::exists(pp) ? image::decodePng(readFile(pp)) : image::Rgba{});
        }
        const image::Rgba& img = pages[page];
        if (img.pixels.empty()) fail("missing " + pageName(page));
        g.pixels.resize(size_t(f.cellSize) * f.cellSize);
        uint32_t ox = uint32_t(cell % perRow) * f.cellSize, oy = uint32_t(cell / perRow) * f.cellSize;
        if (ox + f.cellSize > img.width || oy + f.cellSize > img.height) fail("glyph cell outside " + pageName(page));
        for (uint32_t y = 0; y < f.cellSize; ++y)
            for (uint32_t x = 0; x < f.cellSize; ++x) {
                const uint8_t* p = &img.pixels[((size_t(oy) + y) * img.width + ox + x) * 4];
                // Coverage comes from alpha; fully opaque pages (no alpha) fall back to luminance.
                uint8_t a = p[3] == 255 ? uint8_t((p[0] + p[1] + p[2]) / 3) : p[3];
                g.pixels[size_t(y) * f.cellSize + x] = uint8_t((a + 8) / 17);
            }
        auto it = original.find(g.code);
        if (it != original.end() && it->second->pixels == g.pixels) g.packed = it->second->packed;
        f.glyphs.push_back(std::move(g));
    }
    std::sort(f.glyphs.begin(), f.glyphs.end(), [](const Glyph& a, const Glyph& b) { return a.code < b.code; });
    for (size_t i = 1; i < f.glyphs.size(); ++i)
        if (f.glyphs[i].code == f.glyphs[i - 1].code) fail("duplicate glyph code in font.json");
    return f;
}

} // namespace nut::font
