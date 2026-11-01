#include "commands.hpp"

#include "assetdb.hpp"
#include "container.hpp"
#include "font.hpp"
#include "json.hpp"
#include "loctext.hpp"
#include "media.hpp"
#include "model.hpp"
#include "nbpatch.hpp"
#include "ops.hpp"
#include "xctd.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>

namespace nut::cli {

using ops::safeName;
using ops::validSection;

// ---- argument helpers ------------------------------------------------------

const std::string& Args::at(size_t i, const char* what) const {
    if (i >= pos.size()) fail(std::string("missing argument: ") + what);
    return pos[i];
}

std::string Args::opt(const std::string& k, const std::string& def) const {
    auto it = opts.find(k);
    return it == opts.end() ? def : it->second;
}

namespace {

caff::Caff& requireCaff(Loaded& l, const fs::path& p) {
    if (!l.caff) fail(p.string() + " is not a CAFF/bundle file (" + kindName(l.kind) + ")");
    return *l.caff;
}

void printCaffSummary(const caff::Caff& c) {
    std::printf("sections:\n");
    for (auto& s : c.sections) std::printf("  %-12s %10zu bytes (align %u)\n", s.name.c_str(), s.data.size(), 1u << s.alignLog2);
    size_t relocs = 0;
    for (int k = 0; k < 2; ++k) for (auto& r : c.relocs[k]) relocs += r.offsets.size();
    std::printf("symbols: %zu, names: %zu, relocations: %zu, string pools: %zu\n", c.symbols.size(), c.symbolNames.size(), relocs, c.pools.size());
}

ops::Progress consoleProgress() {
    ops::Progress p;
    p.log = [](const std::string& s) { std::fprintf(stderr, "  %s\n", s.c_str()); };
    auto last = std::make_shared<std::string>();
    p.step = [last](double, const std::string& label) {
        if (label == *last) return;
        *last = label;
        std::printf("[%s]\n", label.c_str());
        std::fflush(stdout);
    };
    return p;
}

} // namespace

// ---- commands --------------------------------------------------------------

int info(const Args& a) {
    fs::path p = a.at(0, "file");
    Loaded l = loadAsset(p);
    std::printf("%s: %s%s, %zu bytes\n", p.string().c_str(), kindName(l.kind), l.xctd ? " (XCTD-compressed)" : "", l.raw.size());
    if (auto id = assetdb::idFromPath(p))
        std::printf("asset id %s (type %s)\n", hex32(*id).c_str(), assetdb::typeName(uint8_t(*id >> 24)).c_str());
    switch (l.kind) {
        case FileKind::Caff: {
            auto& c = *l.caff;
            printCaffSummary(c);
            if (loctext::isLoctext(c)) std::printf("content: loctext with %zu strings\n", loctext::read(c).entries.size());
            size_t tex = ops::findTextures(c).size();
            if (tex) std::printf("textures: %zu\n", tex);
            for (size_t i = 0; i < c.symbols.size() && i < 8; ++i) std::printf("  #%zu %s\n", i + 1, c.symbolName(i).c_str());
            if (c.symbols.size() > 8) std::printf("  ... (use 'list' for all)\n");
            break;
        }
        case FileKind::StreamBundle:
            std::printf("entries: %zu, dependencies: %zu, version %.2f\n", l.sb->entries.size(), l.sb->dependencies.size(), l.sb->version);
            break;
        case FileKind::Font: {
            auto f = font::Font::parse(l.raw);
            std::printf("glyphs: %zu, cell %ux%u, line height %u, %u bpp\n", f.glyphs.size(), f.cellSize, f.cellSize, f.header[2], f.header[3]);
            break;
        }
        case FileKind::Asf: std::printf("%s", media::describeAsf(l.raw).c_str()); break;
        case FileKind::WaveBank: {
            auto wb = media::parseWaveBank(l.raw);
            std::printf("wave bank '%s' v%u, %zu waves\n", wb.name.c_str(), wb.version, wb.waves.size());
            break;
        }
        case FileKind::DbIndex: std::printf("entries: %zu\n", assetdb::readDbIndex(p).size()); break;
        default: break;
    }
    return 0;
}

int hash(const Args& a) {
    if (a.pos.empty()) fail("usage: hash <asset name>...");
    for (auto& n : a.pos) {
        auto id = assetdb::idFromName(n);
        if (!id) { std::printf("%s: unknown asset type\n", n.c_str()); continue; }
        std::printf("%s  %s  %s\n", hex32(*id).c_str(), n.c_str(), assetdb::pathForId("", *id).generic_string().c_str());
    }
    return 0;
}

int index(const Args& a) {
    fs::path root = a.at(0, "assets dir");
    auto entries = assetdb::readDbIndex(root / "Debug" / "db_index.txt");
    Json arr = Json::array();
    for (auto& e : entries) {
        fs::path p = assetdb::pathForEntry(root, e);
        bool exists = fs::exists(p);
        if (a.flag("--json")) {
            Json o = Json::object();
            o["id"] = "0x" + hex32(e.id);
            o["name"] = e.name;
            o["path"] = fs::relative(p, root).generic_string();
            o["exists"] = exists;
            o["timestamp"] = e.timestamp;
            o["version"] = e.version;
            arr.push(o);
        } else {
            std::printf("%s  %-60s %s%s\n", hex32(e.id).c_str(), e.name.c_str(), fs::relative(p, root).generic_string().c_str(), exists ? "" : "  (missing)");
        }
    }
    if (a.flag("--json")) std::printf("%s", arr.dump().c_str());
    return 0;
}

int list(const Args& a) {
    fs::path p = a.at(0, "file");
    Loaded l = loadAsset(p);
    if (l.caff) {
        auto& c = *l.caff;
        std::printf("%5s %-11s %10s %10s %4s  %s\n", "#", "section", "offset", "size", "kind", "name");
        for (size_t i = 0; i < c.symbols.size(); ++i) {
            const auto& s = c.symbols[i];
            std::printf("%5zu %-11s %10u %10u %4u  %s\n", i + 1, validSection(c, s) ? c.sections[s.section - 1].name.c_str() : "-", s.offset, s.size,
                        s.kind, c.symbolName(i).c_str());
        }
    } else if (l.sb) {
        std::printf("%5s %-10s %-16s %10s  %s\n", "#", "id", "type", "size", "name");
        for (size_t i = 0; i < l.sb->entries.size(); ++i) {
            const auto& e = l.sb->entries[i];
            std::printf("%5zu %s %-16s %10zu  %s\n", i + 1, hex32(e.id).c_str(), assetdb::typeName(uint8_t(e.id >> 24)).c_str(), e.data.size(),
                        ops::entryName(e).c_str());
        }
        std::printf("dependencies:");
        for (uint32_t d : l.sb->dependencies) std::printf(" %s", hex32(d).c_str());
        std::printf("\n");
    } else if (l.kind == FileKind::WaveBank) {
        auto wb = media::parseWaveBank(l.raw);
        static const char* fmts[] = {"PCM", "XMA", "ADPCM", "WMA"};
        for (size_t i = 0; i < wb.waves.size(); ++i) {
            const auto& w = wb.waves[i];
            std::printf("%4zu %-5s %u ch %6u Hz %8u samples %8zu bytes  %s\n", i, fmts[w.formatTag], w.channels, w.sampleRate, w.durationSamples, w.data.size(),
                        w.name.c_str());
        }
    } else {
        fail("cannot list contents of a " + std::string(kindName(l.kind)) + " file");
    }
    return 0;
}

int decompress(const Args& a) {
    Bytes d = readFile(a.at(0, "input"));
    if (!xctd::isXctd(d)) fail("input is not XCTD-compressed");
    Bytes out = xctd::decompress(d);
    writeFile(a.at(1, "output"), out);
    std::printf("decompressed %zu -> %zu bytes\n", d.size(), out.size());
    return 0;
}

int compress(const Args& a) {
    Bytes d = readFile(a.at(0, "input"));
    if (xctd::isXctd(d)) fail("input is already XCTD-wrapped");
    writeFile(a.at(1, "output"), xctd::wrapStored(d));
    std::printf("wrapped %zu bytes in an uncompressed XCTD container\n", d.size());
    return 0;
}

int extract(const Args& a) {
    fs::path p = a.at(0, "file");
    std::string sel = a.at(1, "symbol");
    fs::path out = a.at(2, "output");
    Loaded l = loadAsset(p);
    if (l.sb) {
        // For stream bundles the selector is an entry id or index.
        for (size_t i = 0; i < l.sb->entries.size(); ++i) {
            const auto& e = l.sb->entries[i];
            if (sel == "#" + std::to_string(i + 1) || lower(sel) == hex32(e.id) || lower(sel) == "0x" + hex32(e.id) ||
                (!e.data.empty() && lower(ops::entryName(e)).find(lower(sel)) != std::string::npos)) {
                writeFile(out, e.data);
                std::printf("wrote entry %s (%zu bytes) to %s\n", hex32(e.id).c_str(), e.data.size(), out.string().c_str());
                return 0;
            }
        }
        fail("no stream bundle entry matches '" + sel + "'");
    }
    auto& c = requireCaff(l, p);
    auto hits = ops::selectSymbols(c, sel, a.opt("--section"));
    if (hits.empty()) fail("no symbol matches '" + sel + "'");
    if (hits.size() == 1) {
        writeFile(out, c.symbolData(hits[0]));
        std::printf("wrote symbol #%zu (%u bytes) to %s\n", hits[0] + 1, c.symbols[hits[0]].size, out.string().c_str());
    } else {
        for (size_t i : hits) {
            fs::path f = out / (std::to_string(i + 1) + "_" + safeName(assetdb::baseAssetName(c.symbolName(i))) + "." + ops::sectionTag(c, c.symbols[i]) + ".bin");
            writeFile(f, c.symbolData(i));
            std::printf("wrote symbol #%zu to %s\n", i + 1, f.string().c_str());
        }
    }
    return 0;
}

int replace(const Args& a) {
    fs::path p = a.at(0, "file");
    std::string sel = a.at(1, "symbol");
    Bytes data = readFile(a.at(2, "data file"));
    Loaded l = loadAsset(p);
    fs::path out = prepareOutput(p, a.opt("-o"));
    if (l.sb) {
        for (size_t i = 0; i < l.sb->entries.size(); ++i) {
            auto& e = l.sb->entries[i];
            if (sel == "#" + std::to_string(i + 1) || lower(sel) == hex32(e.id) || lower(sel) == "0x" + hex32(e.id)) {
                e.data = data;
                writeFile(out, l.sb->serialize());
                std::printf("replaced entry %s; wrote %s\n", hex32(e.id).c_str(), out.string().c_str());
                return 0;
            }
        }
        fail("no stream bundle entry matches '" + sel + "' (use #index or the hex id)");
    }
    auto& c = requireCaff(l, p);
    auto hits = ops::selectSymbols(c, sel, a.opt("--section"));
    if (hits.size() != 1) fail(hits.empty() ? "no symbol matches '" + sel + "'" : "'" + sel + "' matches " + std::to_string(hits.size()) + " symbols; use #index or --section");
    uint32_t oldSize = c.symbols[hits[0]].size;
    c.replaceSymbolData(hits[0], data);
    saveCaff(out, c, a.flag("--xctd"));
    std::printf("replaced symbol #%zu (%u -> %zu bytes); wrote %s\n", hits[0] + 1, oldSize, data.size(), out.string().c_str());
    if (data.size() != oldSize) std::printf("note: relocation offsets inside this symbol were kept; update them if its layout changed\n");
    return 0;
}

int caffUnpack(const Args& a) {
    fs::path p = a.at(0, "file"), dir = a.at(1, "output dir");
    Loaded l = loadAsset(p);
    auto& c = requireCaff(l, p);
    ops::unpackCaff(c, l.xctd, dir);
    std::printf("unpacked %zu symbols from %s into %s\n", c.symbols.size(), p.string().c_str(), dir.string().c_str());
    return 0;
}

int caffPack(const Args& a) {
    fs::path dir = a.at(0, "unpacked dir"), out = a.at(1, "output file");
    bool wasXctd = false;
    int changed = 0;
    caff::Caff c = ops::packCaff(dir, &wasXctd, &changed);
    saveCaff(out, c, a.flag("--xctd"));
    std::printf("packed %s (%d resized/edited symbols)%s\n", out.string().c_str(), changed, wasXctd && !a.flag("--xctd") ? " as plain CAFF" : "");
    return 0;
}

int sbUnpack(const Args& a) {
    fs::path p = a.at(0, "file"), dir = a.at(1, "output dir");
    Loaded l = loadAsset(p);
    if (!l.sb) fail(p.string() + " is not a stream bundle");
    ops::unpackStreamBundle(*l.sb, dir);
    std::printf("unpacked %zu entries into %s\n", l.sb->entries.size(), dir.string().c_str());
    return 0;
}

int sbPack(const Args& a) {
    fs::path dir = a.at(0, "unpacked dir"), out = a.at(1, "output file");
    auto sb = ops::packStreamBundle(dir);
    writeFile(out, sb.serialize());
    std::printf("packed %zu entries into %s\n", sb.entries.size(), out.string().c_str());
    return 0;
}

int texExport(const Args& a) {
    fs::path p = a.at(0, "file"), dir = a.at(1, "output dir");
    bool png = a.flag("--png") || a.flag("--png-only"), dds = !a.flag("--png-only");
    Loaded l = loadAsset(p);
    if (!l.caff && !l.sb) fail("texture export needs a CAFF, bundle or stream bundle");
    // Stream bundle entries are parsed into owned CAFFs; pointers are taken once the vectors are final.
    auto parseEntries = [](const streambundle::StreamBundle& sb) {
        std::vector<caff::Caff> v;
        for (auto& e : sb.entries) if (caff::isCaff(e.data)) v.push_back(caff::Caff::parse(e.data));
        return v;
    };
    std::vector<caff::Caff> mainEntries, streamEntries;
    if (l.sb) mainEntries = parseEntries(*l.sb);
    if (!a.opt("--stream").empty()) {
        Loaded s = loadAsset(a.opt("--stream"));
        if (!s.sb) fail("--stream must be a stream bundle");
        streamEntries = parseEntries(*s.sb);
    }
    std::vector<caff::Caff*> mains, streams;
    if (l.caff) mains.push_back(&*l.caff);
    for (auto& c : mainEntries) mains.push_back(&c);
    for (auto& c : streamEntries) streams.push_back(&c);
    ops::Progress pr = consoleProgress();
    ops::TexStats st = ops::exportTextures(mains, streams, dir, dds, png, nullptr, &pr);
    std::printf("exported %d textures (%d merged with streamed top mips, %d skipped) to %s\n", st.written, st.merged, st.skipped, dir.string().c_str());
    return 0;
}

int modelExport(const Args& a) {
    fs::path p = a.at(0, "file"), dir = a.at(1, "output dir");
    bool allGroups = a.flag("--all-groups");
    Loaded l = loadAsset(p);
    std::vector<caff::Caff> entries;
    if (l.sb)
        for (auto& e : l.sb->entries) if (caff::isCaff(e.data)) entries.push_back(caff::Caff::parse(e.data));
    std::vector<caff::Caff*> caffs;
    if (l.caff) caffs.push_back(&*l.caff);
    for (auto& c : entries) caffs.push_back(&c);
    if (caffs.empty()) fail("model export needs a CAFF, bundle or stream bundle");
    int written = 0;
    for (caff::Caff* c : caffs) {
        for (size_t i = 0; i < c->symbols.size(); ++i) {
            if (!model::isModelSymbol(*c, i)) continue;
            model::Model m;
            try {
                m = model::parse(*c, i);
            } catch (std::exception& e) {
                std::printf("warning: %s: %s\n", c->symbolName(i).c_str(), e.what());
                continue;
            }
            std::vector<bool> include(m.meshes.size());
            size_t verts = 0, tris = 0, shown = 0;
            for (size_t k = 0; k < m.meshes.size(); ++k) {
                auto& mesh = m.meshes[k];
                include[k] = allGroups || m.groups[size_t(mesh.group)].shown;
                if (include[k]) verts += mesh.vertices.size(), tris += mesh.indices.size() / 3;
            }
            for (auto& g : m.groups) shown += allGroups || g.shown;
            std::string base = safeName(m.name, 150);
            fs::create_directories(dir);
            writeText(dir / (base + ".obj"), model::toObj(m, base + ".mtl", {}, include));
            writeText(dir / (base + ".mtl"), model::toMtl(m));
            std::string skel = "# bone parent local_xyz world_xyz rotation_xyzw\n";
            for (size_t b = 0; b < m.bones.size(); ++b) {
                auto& bn = m.bones[b];
                char line[512];
                std::snprintf(line, sizeof line, "%zu %s %d  %g %g %g  %g %g %g  %g %g %g %g\n", b, bn.name.c_str(), bn.parent, bn.local[0], bn.local[1],
                              bn.local[2], bn.world[0], bn.world[1], bn.world[2], bn.rotation[0], bn.rotation[1], bn.rotation[2], bn.rotation[3]);
                skel += line;
            }
            if (!m.bones.empty()) writeText(dir / (base + ".bones.txt"), skel);
            std::printf("%s: %zu of %zu draw groups, %zu vertices, %zu triangles, %zu bones, %zu textures\n", m.name.c_str(), shown, m.groups.size(), verts,
                        tris, m.bones.size(), m.textures.size());
            for (auto& w : m.warnings) std::printf("  warning: %s\n", w.c_str());
            ++written;
        }
    }
    if (written && a.flag("--png")) {
        ops::Progress pr = consoleProgress();
        ops::exportTextures(caffs, {}, dir, false, true, nullptr, &pr);
    }
    std::printf("exported %d model(s) to %s\n", written, dir.string().c_str());
    return 0;
}

int texImport(const Args& a) {
    fs::path p = a.at(0, "file");
    std::string name = lower(a.at(1, "texture name"));
    fs::path img = a.at(2, "image");
    Loaded l = loadAsset(p);
    auto& c = requireCaff(l, p);
    std::vector<ops::TexRef> matches;
    for (auto& t : ops::findTextures(c)) {
        std::string n = lower(t.name);
        if (n == name || lower(ops::fullName(t.name)) == name) { matches = {t}; break; }
        if (n.find(name) != std::string::npos) matches.push_back(t);
    }
    if (matches.size() != 1)
        fail(matches.empty() ? "no texture matches '" + name + "'" : "'" + name + "' matches " + std::to_string(matches.size()) + " textures; be more specific");
    std::optional<Loaded> stream;
    if (!a.opt("--stream").empty()) {
        stream = loadAsset(a.opt("--stream"));
        if (!stream->sb) fail("--stream must be a stream bundle");
    }
    bool streamChanged = false;
    std::string summary = ops::importTexture(c, matches[0], img, stream ? &*stream->sb : nullptr, &streamChanged);
    fs::path out = prepareOutput(p, a.opt("-o"));
    saveCaff(out, c, a.flag("--xctd"));
    std::printf("%s: %s; wrote %s\n", matches[0].name.c_str(), summary.c_str(), out.string().c_str());
    if (streamChanged) {
        fs::path sout = prepareOutput(a.opt("--stream"), a.opt("--stream-out"));
        writeFile(sout, stream->sb->serialize());
        std::printf("wrote %s\n", sout.string().c_str());
    }
    return 0;
}

int textExport(const Args& a) {
    fs::path p = a.at(0, "loctext file"), out = a.at(1, "output .json");
    Loaded l = loadAsset(p);
    auto& c = requireCaff(l, p);
    auto t = loctext::read(c);
    Json j = loctext::toJson(t);
    if (!c.symbols.empty()) j["asset"] = assetdb::baseAssetName(c.symbolName(0));
    writeText(out, j.dump());
    std::printf("exported %zu strings to %s\n", t.entries.size(), out.string().c_str());
    return 0;
}

int textImport(const Args& a) {
    fs::path p = a.at(0, "loctext file");
    Json j = Json::parse(readText(a.at(1, "input .json")));
    Loaded l = loadAsset(p);
    auto& c = requireCaff(l, p);
    auto t = loctext::read(c);
    size_t before = t.entries.size();
    loctext::applyJson(t, j, a.flag("--prune"));
    loctext::write(c, t);
    fs::path out = prepareOutput(p, a.opt("-o"));
    saveCaff(out, c);
    std::printf("imported %zu strings (%zu before) into %s\n", t.entries.size(), before, out.string().c_str());
    return 0;
}

int fontExport(const Args& a) {
    fs::path p = a.at(0, "font .sbp"), dir = a.at(1, "output dir");
    auto f = font::Font::parse(readFile(p));
    font::exportTo(f, dir);
    std::printf("exported %zu glyphs to %s\n", f.glyphs.size(), dir.string().c_str());
    return 0;
}

int fontImport(const Args& a) {
    fs::path dir = a.at(0, "exported dir"), out = a.at(1, "output .sbp");
    std::optional<font::Font> base;
    if (!a.opt("--base").empty()) base = font::Font::parse(readFile(a.opt("--base")));
    auto f = font::importFrom(dir, base ? &*base : nullptr);
    writeFile(out, f.serialize());
    std::printf("wrote %zu glyphs to %s\n", f.glyphs.size(), out.string().c_str());
    return 0;
}

int xmvInfo(const Args& a) {
    fs::path p = a.at(0, "file");
    Bytes d = readFile(p);
    std::printf("%s", media::describeAsf(d).c_str());
    if (a.pos.size() > 1) {
        writeFile(a.pos[1], d);
        std::printf("copied to %s (plain WMV/ASF)\n", a.pos[1].c_str());
    }
    return 0;
}

int waveExport(const Args& a) {
    fs::path p = a.at(0, "file"), dir = a.at(1, "output dir");
    Loaded l = loadAsset(p);
    int banks = 0;
    if (l.kind == FileKind::WaveBank) { ops::exportWaveBank(l.raw, dir, p.stem().string()); ++banks; }
    else if (l.sb) {
        for (auto& e : l.sb->entries)
            if (media::isWaveBank(e.data)) { ops::exportWaveBank(e.data, dir, hex32(e.id) + "_" + ops::entryName(e)); ++banks; }
    } else fail("no wave banks in " + p.string());
    std::printf("exported %d wave bank(s) to %s\n", banks, dir.string().c_str());
    return 0;
}

int iniExport(const Args& a) {
    Bytes d = readFile(a.at(0, "ini"));
    Bytes out = d.size() >= 2 && d[0] == 0xFE && d[1] == 0xFF ? ops::precacheToUtf8(d) : d;
    writeFile(a.at(1, "output"), out);
    std::printf("wrote %s\n", a.pos[1].c_str());
    return 0;
}

int iniImport(const Args& a) {
    std::string text = readText(a.at(0, "text file"));
    fs::path out = a.at(1, "output .ini");
    if (a.flag("--utf16")) writeFile(out, ops::utf8ToPrecache(text));
    else writeText(out, text);
    std::printf("wrote %s\n", out.string().c_str());
    return 0;
}

int extractAll(const Args& a) {
    fs::path root = a.at(0, "assets dir"), out = a.at(1, "output dir");
    ops::ExtractOptions opt;
    opt.png = a.flag("--png");
    if (!a.opt("--only").empty()) for (auto& s : split(a.opt("--only"), ',')) opt.only.insert(trim(s));
    int failures = ops::extractAll(root, out, opt, consoleProgress());
    std::printf("extract-all finished with %d error(s); output in %s\n", failures, out.string().c_str());
    return failures ? 1 : 0;
}

int verify(const Args& a) {
    ops::Progress pr;
    pr.log = [](const std::string& s) { std::printf("%s\n", s.c_str()); };
    auto next = std::make_shared<int>(0);
    pr.step = [next](double f, const std::string& label) {
        int pct = int(f * 100);
        if (pct >= *next) { std::printf("  ... %d%% (%s)\n", pct, label.c_str()); std::fflush(stdout); *next = pct + 10; }
    };
    ops::VerifyResult r = ops::verifyAll(a.at(0, "assets dir"), pr);
    std::printf("verify: %d passed, %d failed\n", r.passed, r.failed);
    return r.failed ? 1 : 0;
}

int makeNbpatch(const Args& a) {
    fs::path root = a.at(0, "game dir"), out = a.at(1, "out.nbpatch");
    nbpatch::Info info;
    info.name = a.opt("--name");
    info.id = a.opt("--id");
    info.version = a.opt("--version", "1");
    info.author = a.opt("--author");
    info.category = a.opt("--category");
    info.description = a.opt("--description");
    if (info.name.empty()) fail("make-nbpatch needs --name");
    ops::Progress pr;
    pr.log = [](const std::string& s) { std::printf("%s\n", s.c_str()); };
    auto changes = nbpatch::findChanges(root, a.opt("--original"), pr);
    if (changes.empty()) {
        std::printf("no changed files found (%s)\n", a.opt("--original").empty() ? "no <file>.bak backups differ from their files" : "the folders match");
        return 1;
    }
    std::printf("%zu changed file(s):\n", changes.size());
    if (!nbpatch::write(out, info, changes, pr)) return 1;
    std::printf("wrote %s\n", out.string().c_str());
    return 0;
}

} // namespace nut::cli
