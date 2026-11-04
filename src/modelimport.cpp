#include "modelimport.hpp"

#include "assetdb.hpp"
#include "fbx.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <tuple>

namespace nut::model {
namespace {

// ---- value encoding (the inverse of the parser's decoding) -----------------------------------

uint16_t toHalf(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000;
    uint32_t mag = x & 0x7FFFFFFF;
    if (mag >= 0x7F800000) return uint16_t(sign | 0x7C00 | (mag > 0x7F800000 ? 0x200 : 0)); // inf / nan
    if (mag >= 0x477FF000) return uint16_t(sign | 0x7BFF);                                   // clamp to 65504
    if (mag < 0x38800000) {                                                                   // subnormal or zero
        if (mag < 0x33000000) return uint16_t(sign);
        uint32_t e = mag >> 23, m = (mag & 0x7FFFFF) | 0x800000;
        uint32_t shift = 126 - e; // 14 - (e - 127) + 13 - 1
        uint32_t h = m >> (shift + 1);
        uint32_t rest = m & ((2u << shift) - 1), halfway = 1u << shift;
        if (rest > halfway || (rest == halfway && (h & 1))) ++h;
        return uint16_t(sign | h);
    }
    uint32_t h = ((mag - 0x38000000) >> 13);
    uint32_t rest = mag & 0x1FFF;
    if (rest > 0x1000 || (rest == 0x1000 && (h & 1))) ++h;
    return uint16_t(sign | h);
}

float fromHalf(uint16_t h) {
    uint32_t s = uint32_t(h >> 15) << 31, e = (h >> 10) & 0x1F, m = h & 0x3FF, f;
    if (e == 0) {
        if (m == 0) f = s;
        else {
            int k = 0;
            while (!(m & 0x400)) { m <<= 1; ++k; }
            m &= 0x3FF;
            f = s | uint32_t(127 - 15 - k + 1) << 23 | m << 13;
        }
    } else if (e == 31) {
        f = s | 0x7F800000u | m << 13;
    } else {
        f = s | (e + 112) << 23 | m << 13;
    }
    float r;
    std::memcpy(&r, &f, 4);
    return r;
}

void decDec3n(uint32_t v, float out[3]) {
    for (int k = 0; k < 3; ++k) {
        int c = int((v >> (10 * k)) & 0x3FF);
        if (c >= 512) c -= 1024;
        out[k] = std::max(-1.0f, float(c) / 511.0f);
    }
}

// Keeps the top two bits of `old` (unused by the shaders, but left as they were).
uint32_t encDec3n(const float n[3], uint32_t old) {
    uint32_t v = old & 0xC0000000u;
    for (int k = 0; k < 3; ++k) {
        int c = int(std::lround(std::clamp(n[k], -1.0f, 1.0f) * 511.0f));
        v |= uint32_t(c & 0x3FF) << (10 * k);
    }
    return v;
}

using Vec3 = std::array<float, 3>;
using Vec2 = std::array<float, 2>;

float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 cross(const Vec3& a, const Vec3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
bool normalize(Vec3& v) {
    float l = std::sqrt(dot(v, v));
    if (l < 1e-12f) return false;
    for (float& x : v) x /= l;
    return true;
}

// ---- OBJ ---------------------------------------------------------------------------------------

struct Obj {
    struct Corner {
        int v = -1, vt = -1, vn = -1;
        bool operator<(const Corner& o) const { return std::tie(v, vt, vn) < std::tie(o.v, o.vt, o.vn); }
    };
    struct Object {
        std::string name;
        size_t firstV = 0, endV = 0; // the "v" lines written under this object
        bool vSet = false;
        std::vector<std::array<Corner, 3>> tris;
    };
    std::vector<Vec3> v, vn;
    std::vector<Vec2> vt;
    std::vector<Object> objects;
    std::vector<std::vector<std::pair<int, float>>> skin; // per v (FBX): skeleton bone and weight; may be empty
};

Obj parseObj(const std::string& text) {
    Obj o;
    // Objects start at "o"; files without any "o" line use "g" instead.
    bool hasO = false;
    for (size_t p = 0; p < text.size();) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        if (e - p >= 2 && text[p] == 'o' && (text[p + 1] == ' ' || text[p + 1] == '\t')) { hasO = true; break; }
        p = e + 1;
    }
    auto object = [&]() -> Obj::Object& {
        if (o.objects.empty()) o.objects.push_back({});
        return o.objects.back();
    };
    auto index = [](const char* s, size_t count) {
        long i = std::strtol(s, nullptr, 10);
        if (i < 0) i += long(count) + 1;
        return i >= 1 && size_t(i) <= count ? int(i - 1) : -1;
    };
    size_t lineNo = 0;
    for (size_t p = 0; p < text.size();) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(p, e - p);
        p = e + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t sp = line.find_first_of(" \t");
        std::string key = line.substr(0, sp);
        std::string rest = sp == std::string::npos ? "" : trim(line.substr(sp + 1));
        if (key == "o" || (key == "g" && !hasO)) {
            o.objects.push_back({});
            o.objects.back().name = rest;
        } else if (key == "v") {
            Vec3 x{};
            if (std::sscanf(rest.c_str(), "%f %f %f", &x[0], &x[1], &x[2]) < 3) fail("OBJ line " + std::to_string(lineNo) + ": bad vertex");
            auto& ob = object();
            if (!ob.vSet) { ob.firstV = o.v.size(); ob.vSet = true; }
            o.v.push_back(x);
            ob.endV = o.v.size();
        } else if (key == "vt") {
            Vec2 x{};
            std::sscanf(rest.c_str(), "%f %f", &x[0], &x[1]);
            o.vt.push_back(x);
        } else if (key == "vn") {
            Vec3 x{};
            std::sscanf(rest.c_str(), "%f %f %f", &x[0], &x[1], &x[2]);
            o.vn.push_back(x);
        } else if (key == "f") {
            std::vector<Obj::Corner> poly;
            for (auto& tok : split(rest, ' ')) {
                if (tok.empty()) continue;
                Obj::Corner c;
                auto parts = split(tok, '/');
                c.v = index(parts[0].c_str(), o.v.size());
                if (parts.size() > 1 && !parts[1].empty()) c.vt = index(parts[1].c_str(), o.vt.size());
                if (parts.size() > 2 && !parts[2].empty()) c.vn = index(parts[2].c_str(), o.vn.size());
                if (c.v < 0) fail("OBJ line " + std::to_string(lineNo) + ": face refers to a missing vertex");
                poly.push_back(c);
            }
            for (size_t k = 1; k + 1 < poly.size(); ++k) object().tris.push_back({poly[0], poly[k], poly[k + 1]});
        }
    }
    return o;
}

// The mesh an object stands for: the number after the last "mesh" in its name.
int meshOfName(const std::string& name) {
    std::string n = lower(name);
    int found = -1;
    for (size_t p = n.find("mesh"); p != std::string::npos; p = n.find("mesh", p + 1)) {
        size_t d = p + 4, e = d;
        while (e < n.size() && n[e] >= '0' && n[e] <= '9') ++e;
        if (e > d && e - d < 7) found = std::atoi(n.c_str() + d);
    }
    return found;
}

// ---- writing vertices -----------------------------------------------------------------------

struct NewVertex {
    Vec3 pos, normal;
    Vec2 uv; // as stored (OBJ v already flipped)
    bool hasUv = false;
    std::vector<std::pair<int, float>> skin; // skeleton bone and weight; empty keeps the vertex's own
};

// Up to four bone weights, largest first, quantised to sum to 255 as the game's buffers store them.
void writeSkin(uint8_t* p, const Layout& l, std::vector<std::pair<int, float>> skin, bool onlyChanged) {
    std::sort(skin.begin(), skin.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (skin.size() > 4) skin.resize(4);
    float sum = 0;
    for (auto& s : skin) sum += std::max(0.0f, s.second);
    if (sum <= 0) return;
    int q[4] = {}, total = 0;
    for (size_t k = 0; k < skin.size(); ++k) total += q[k] = int(std::lround(std::max(0.0f, skin[k].second) / sum * 255));
    q[0] += 255 - total;
    std::map<int, int> want, have;
    for (size_t k = 0; k < skin.size(); ++k)
        if (q[k] > 0) want[skin[k].first] += q[k];
    for (int k = 0; k < 4; ++k)
        if (int w = p[l.attributes + 8 + 3 - k]) have[be16(p + l.attributes + 2 * k)] += w;
    if (onlyChanged && want == have) return;
    for (int k = 0; k < 4; ++k) {
        bool used = size_t(k) < skin.size() && q[k] > 0;
        putBe16(p + l.attributes + 2 * k, uint16_t(used ? skin[size_t(k)].first : skin[0].first));
        p[l.attributes + 8 + 3 - k] = uint8_t(used ? q[k] : 0);
    }
}

// Writes position, normal and first texture coordinate into a vertex laid out as `l`, keeping every
// other field. With `onlyChanged`, a field is left untouched when it already decodes to the value.
void encodeVertex(uint8_t* p, const Layout& l, const NewVertex& nv, bool onlyChanged) {
    auto same = [](float a, float b) { return std::fabs(a - b) <= 1e-6f * std::max(1.0f, std::fabs(b)); };
    bool posSame = true;
    for (int k = 0; k < 3; ++k) {
        float cur = l.floatPosition ? bef32(p + 4 * k) : fromHalf(be16(p + 2 * k));
        posSame &= same(cur, nv.pos[size_t(k)]) || (!l.floatPosition && toHalf(nv.pos[size_t(k)]) == be16(p + 2 * k));
    }
    if (!onlyChanged || !posSame)
        for (int k = 0; k < 3; ++k) {
            if (l.floatPosition) {
                uint32_t b;
                std::memcpy(&b, &nv.pos[size_t(k)], 4);
                putBe32(p + 4 * k, b);
            } else {
                putBe16(p + 2 * k, toHalf(nv.pos[size_t(k)]));
            }
        }
    bool normalChanged = false;
    if (l.normal >= 0) {
        float cur[3];
        uint32_t old = be32(p + l.normal);
        decDec3n(old, cur);
        uint32_t enc = encDec3n(nv.normal.data(), old);
        bool nSame = enc == old || (same(cur[0], nv.normal[0]) && same(cur[1], nv.normal[1]) && same(cur[2], nv.normal[2]));
        if (!onlyChanged || !nSame) {
            putBe32(p + l.normal, enc);
            normalChanged = true;
        }
    }
    // Keep the tangent perpendicular to a changed normal.
    if (normalChanged && l.tangent >= 0) {
        uint32_t old = be32(p + l.tangent);
        float t[3];
        decDec3n(old, t);
        Vec3 tv{t[0], t[1], t[2]}, n = nv.normal;
        normalize(n);
        Vec3 o = sub(tv, {n[0] * dot(n, tv), n[1] * dot(n, tv), n[2] * dot(n, tv)});
        if (!normalize(o)) {
            o = cross(n, std::fabs(n[1]) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0});
            normalize(o);
        }
        putBe32(p + l.tangent, encDec3n(o.data(), old));
    }
    if (nv.hasUv && !l.uvSets.empty()) {
        uint8_t* q = p + l.uvSets[0];
        // OBJ stores 1 - v, which editors round to 6 decimals: allow for that.
        auto uvClose = [](float a, float b) { return std::fabs(a - b) <= 2e-6f * std::max(1.0f, std::fabs(b)); };
        bool uvSame = (uvClose(fromHalf(be16(q)), nv.uv[0]) || toHalf(nv.uv[0]) == be16(q)) &&
                      (uvClose(fromHalf(be16(q + 2)), nv.uv[1]) || toHalf(nv.uv[1]) == be16(q + 2));
        if (!onlyChanged || !uvSame) {
            putBe16(q, toHalf(nv.uv[0]));
            putBe16(q + 2, toHalf(nv.uv[1]));
        }
    }
    if (l.skinned && !nv.skin.empty()) writeSkin(p, l, nv.skin, onlyChanged);
}

uint32_t alignUp(uint32_t v, uint32_t a) { return (v + a - 1) / a * a; }

} // namespace

// ---- OBJ / FBX import ---------------------------------------------------------------------------

namespace {

ImportReport applyObj(caff::Caff& c, size_t symbol, const Obj& obj) {
    Model m = parse(c, symbol);
    ImportReport rep;
    auto skinOf = [&](size_t v) { return v < obj.skin.size() ? obj.skin[v] : std::vector<std::pair<int, float>>(); };

    // Objects -> meshes.
    std::map<size_t, const Obj::Object*> byMesh;
    std::vector<std::string> unnamed;
    for (auto& ob : obj.objects) {
        if (ob.tris.empty() && !ob.vSet) continue; // empty "o" lines
        int mi = meshOfName(ob.name);
        if (mi < 0 || size_t(mi) >= m.meshes.size()) {
            unnamed.push_back(ob.name.empty() ? "(unnamed)" : ob.name);
            continue;
        }
        if (byMesh.count(size_t(mi))) fail("two objects name mesh " + std::to_string(mi));
        byMesh[size_t(mi)] = &ob;
    }
    if (!unnamed.empty()) {
        std::string list;
        for (size_t i = 0; i < unnamed.size() && i < 5; ++i) list += (i ? ", " : "") + unnamed[i];
        fail("object(s) " + list + " name no mesh of this model (" + std::to_string(m.meshes.size()) +
             " meshes): name each object after the mesh it replaces, as the export does (group0_mesh3)");
    }
    if (byMesh.empty()) fail("the file has no objects");

    std::map<int, Bytes> gpu; // edited copies of .gpu symbols
    auto gpuData = [&](int sym) -> Bytes& { return gpu.try_emplace(sym, c.symbolData(size_t(sym))).first->second; };
    Bytes data = c.symbolData(symbol);

    // Meshes that need new geometry, grouped by the vertex buffer they extend.
    struct Rebuild {
        size_t mesh;
        std::vector<NewVertex> verts;
        std::vector<uint32_t> tris; // indices into verts
        std::vector<uint32_t> base; // template vertex (buffer index) of each new vertex
    };
    std::vector<Rebuild> rebuilds;

    for (auto& [mi, ob] : byMesh) {
        Mesh& mesh = m.meshes[mi];
        auto& so = mesh.source;
        if (so.vbSymbol < 0 || so.vertex.size() != mesh.vertices.size()) fail("mesh " + std::to_string(mi) + ": buffer location unknown");
        size_t nv = ob->vSet ? ob->endV - ob->firstV : 0;
        bool inRange = true;
        for (auto& t : ob->tris)
            for (auto& cn : t) inRange &= size_t(cn.v) >= ob->firstV && size_t(cn.v) < ob->endV;
        // In place only when every triangle is the mesh's, in order and winding (any starting corner),
        // so vertex k of the object really is vertex k of the mesh. Editors that keep the mesh (Blender
        // does) pass; one that reordered vertices or faces gets a rebuild, which is right either way.
        bool sameTopology = nv == mesh.vertices.size() && ob->tris.size() * 3 == mesh.indices.size() && inRange;
        for (size_t k = 0; sameTopology && k < ob->tris.size(); ++k) {
            uint32_t a[3], b[3];
            for (int q = 0; q < 3; ++q) {
                a[q] = mesh.indices[3 * k + size_t(q)];
                b[q] = uint32_t(size_t(ob->tris[k][size_t(q)].v) - ob->firstV);
            }
            bool rotated = false;
            for (int r = 0; r < 3 && !rotated; ++r) rotated = a[0] == b[r] && a[1] == b[(r + 1) % 3] && a[2] == b[(r + 2) % 3];
            sameTopology = rotated;
        }
        if (sameTopology) {
            // ---- in place
            std::vector<NewVertex> nvs(nv);
            std::vector<int> normalCount(nv, 0);
            for (size_t k = 0; k < nv; ++k) {
                const Vertex& old = mesh.vertices[k];
                nvs[k].pos = obj.v[ob->firstV + k];
                nvs[k].normal = {old.normal[0], old.normal[1], old.normal[2]};
                nvs[k].uv = {old.uv[0], old.uv[1]};
                nvs[k].hasUv = true;
                nvs[k].skin = skinOf(ob->firstV + k);
            }
            // A vertex's normal: its corners' normal when they agree (exactly as written, so an
            // untouched one keeps its bytes), otherwise their normalised mean.
            std::vector<Vec3> nsum(nv, Vec3{0, 0, 0}), nfirst(nv);
            std::vector<bool> uvSet(nv, false), nAgree(nv, true);
            for (auto& t : ob->tris)
                for (auto& cn : t) {
                    size_t k = size_t(cn.v) - ob->firstV;
                    if (cn.vt >= 0 && !uvSet[k]) {
                        nvs[k].uv = {obj.vt[size_t(cn.vt)][0], 1.0f - obj.vt[size_t(cn.vt)][1]};
                        uvSet[k] = true;
                    }
                    if (cn.vn >= 0) {
                        const Vec3& n = obj.vn[size_t(cn.vn)];
                        if (!normalCount[k]) nfirst[k] = n;
                        else nAgree[k] = nAgree[k] && n == nfirst[k];
                        for (int q = 0; q < 3; ++q) nsum[k][size_t(q)] += n[size_t(q)];
                        ++normalCount[k];
                    }
                }
            for (size_t k = 0; k < nv; ++k) {
                if (!normalCount[k]) continue;
                if (nAgree[k]) nvs[k].normal = nfirst[k];
                else if (normalize(nsum[k])) nvs[k].normal = nsum[k];
            }
            Bytes& g = gpuData(so.vbSymbol);
            for (size_t k = 0; k < nv; ++k) {
                size_t at = so.vbOffset + size_t(so.vertex[k]) * mesh.stride;
                if (at + mesh.stride > g.size()) fail("mesh " + std::to_string(mi) + ": vertex outside its buffer");
                encodeVertex(&g[at], mesh.layout, nvs[k], true);
            }
            ++rep.edited;
            continue;
        }
        // ---- new geometry
        Rebuild rb;
        rb.mesh = mi;
        if (ob->tris.empty()) {
            ++rep.hidden;
            rebuilds.push_back(std::move(rb));
            continue;
        }
        std::map<Obj::Corner, uint32_t> unique;
        for (auto& t : ob->tris)
            for (auto& cn : t) {
                auto [it, fresh] = unique.emplace(cn, uint32_t(rb.verts.size()));
                if (fresh) {
                    NewVertex v;
                    v.pos = obj.v[size_t(cn.v)];
                    if (cn.vt >= 0) {
                        v.uv = {obj.vt[size_t(cn.vt)][0], 1.0f - obj.vt[size_t(cn.vt)][1]};
                        v.hasUv = true;
                    }
                    v.normal = cn.vn >= 0 ? obj.vn[size_t(cn.vn)] : Vec3{0, 0, 0};
                    v.skin = skinOf(size_t(cn.v));
                    rb.verts.push_back(v);
                }
                rb.tris.push_back(it->second);
            }
        // Missing normals: area-weighted face normals, shared by vertices at the same position.
        std::map<std::array<float, 3>, Vec3> smooth;
        bool anyMissing = false;
        for (auto& v : rb.verts) anyMissing |= dot(v.normal, v.normal) < 1e-12f;
        if (anyMissing) {
            for (size_t k = 0; k + 2 < rb.tris.size(); k += 3) {
                const Vec3 &a = rb.verts[rb.tris[k]].pos, &b = rb.verts[rb.tris[k + 1]].pos, &cc = rb.verts[rb.tris[k + 2]].pos;
                Vec3 n = cross(sub(b, a), sub(cc, a));
                for (int q = 0; q < 3; ++q) {
                    Vec3& s = smooth[rb.verts[rb.tris[k + size_t(q)]].pos];
                    for (int w = 0; w < 3; ++w) s[size_t(w)] += n[size_t(w)];
                }
            }
        }
        for (auto& v : rb.verts) {
            if (dot(v.normal, v.normal) < 1e-12f) v.normal = smooth[v.pos];
            if (!normalize(v.normal)) v.normal = {0, 1, 0};
        }
        // Each new vertex copies the fields it does not set from the nearest original one.
        for (auto& v : rb.verts) {
            size_t best = 0;
            float bestD = 3.4e38f;
            for (size_t k = 0; k < mesh.vertices.size(); ++k) {
                const float* q = mesh.vertices[k].pos;
                float d = (q[0] - v.pos[0]) * (q[0] - v.pos[0]) + (q[1] - v.pos[1]) * (q[1] - v.pos[1]) + (q[2] - v.pos[2]) * (q[2] - v.pos[2]);
                if (d < bestD) { bestD = d; best = k; }
            }
            rb.base.push_back(mesh.source.vertex[best]);
            if (!v.hasUv) {
                v.uv = {mesh.vertices[best].uv[0], mesh.vertices[best].uv[1]};
                v.hasUv = true;
            }
        }
        rebuilds.push_back(std::move(rb));
    }

    // Draws whose records another draw shares must change together or not at all.
    std::set<size_t> rebuilt;
    for (auto& rb : rebuilds) rebuilt.insert(rb.mesh);
    for (auto& rb : rebuilds) {
        const auto& so = m.meshes[rb.mesh].source;
        for (size_t i = 0; i < m.meshes.size(); ++i) {
            if (i == rb.mesh) continue;
            const auto& o = m.meshes[i].source;
            bool shared = o.ibRecord == so.ibRecord || o.indexCountAt == so.indexCountAt;
            if (!shared) continue;
            if (rebuilt.count(i)) {
                fail("meshes " + std::to_string(rb.mesh) + " and " + std::to_string(i) +
                     " are one draw in two parts; give them the same geometry by replacing only one");
            }
            if (o.vbRecord != so.vbRecord) fail("mesh " + std::to_string(i) + " shares mesh " + std::to_string(rb.mesh) + "'s indices with another vertex buffer");
            rep.notes.push_back("mesh " + std::to_string(i) + " draws the same triangles as mesh " + std::to_string(rb.mesh) + " and changes with it");
        }
    }

    // New vertex buffers: the old vertices (other draws still use them) followed by the new ones.
    struct NewBuffer {
        int gpuSym;
        uint32_t stride;
        Bytes bytes;
        uint32_t count;
    };
    std::map<uint32_t, NewBuffer> buffers; // by vb record
    std::map<size_t, uint32_t> firstNew;   // rebuild index -> buffer index of its first new vertex
    for (size_t r = 0; r < rebuilds.size(); ++r) {
        auto& rb = rebuilds[r];
        if (rb.verts.empty()) continue; // hidden: no new vertices
        const Mesh& mesh = m.meshes[rb.mesh];
        const auto& so = mesh.source;
        Bytes& g = gpuData(so.vbSymbol);
        auto it = buffers.find(so.vbRecord);
        if (it == buffers.end()) {
            size_t len = size_t(so.vbCount) * mesh.stride;
            if (so.vbOffset + len > g.size()) fail("mesh " + std::to_string(rb.mesh) + ": vertex buffer outside its symbol");
            it = buffers.emplace(so.vbRecord, NewBuffer{so.vbSymbol, mesh.stride, Bytes(g.begin() + so.vbOffset, g.begin() + so.vbOffset + len), so.vbCount}).first;
        }
        NewBuffer& nb = it->second;
        firstNew[r] = nb.count;
        for (size_t k = 0; k < rb.verts.size(); ++k) {
            size_t from = so.vbOffset + size_t(rb.base[k]) * mesh.stride;
            nb.bytes.insert(nb.bytes.end(), g.begin() + from, g.begin() + from + mesh.stride);
            encodeVertex(&nb.bytes[nb.bytes.size() - mesh.stride], mesh.layout, rb.verts[k], false);
        }
        nb.count += uint32_t(rb.verts.size());
        if (!so.wideIndices && nb.count > 65535)
            fail("mesh " + std::to_string(rb.mesh) + ": its vertex buffer would hold " + std::to_string(nb.count) +
                 " vertices, more than 16-bit indices reach (65535); use fewer vertices");
    }
    // Append the buffers to their .gpu symbols and point the records at them.
    for (auto& [record, nb] : buffers) {
        Bytes& g = gpuData(nb.gpuSym);
        uint32_t at = alignUp(uint32_t(g.size()), 32);
        g.resize(at, 0);
        g.insert(g.end(), nb.bytes.begin(), nb.bytes.end());
        uint32_t size = nb.count * nb.stride;
        const auto* so = &m.meshes[0].source;
        for (auto& rb : rebuilds)
            if (m.meshes[rb.mesh].source.vbRecord == record) so = &m.meshes[rb.mesh].source;
        putBe32(&data[record + 4], at);
        putBe32(&data[record + 8], size);
        putBe32(&data[so->vbHeader + 4], at);
        putBe32(&data[so->vbHeader + 8], size);
    }
    // Index buffers.
    for (size_t r = 0; r < rebuilds.size(); ++r) {
        auto& rb = rebuilds[r];
        Mesh& mesh = m.meshes[rb.mesh];
        const auto& so = mesh.source;
        std::vector<uint32_t> idx;
        if (rb.tris.empty()) idx = {so.vertex.empty() ? 0 : so.vertex[0], so.vertex.empty() ? 0 : so.vertex[0], so.vertex.empty() ? 0 : so.vertex[0]};
        else
            for (uint32_t t : rb.tris) idx.push_back(firstNew[r] + t);
        Bytes& g = gpuData(so.ibSymbol);
        uint32_t at = alignUp(uint32_t(g.size()), 32);
        g.resize(at, 0);
        uint32_t isz = so.wideIndices ? 4 : 2;
        g.resize(at + idx.size() * isz);
        for (size_t k = 0; k < idx.size(); ++k) {
            if (so.wideIndices) putBe32(&g[at + 4 * k], idx[k]);
            else putBe16(&g[at + 2 * k], uint16_t(idx[k]));
        }
        putBe32(&data[so.ibRecord + 4], at);
        putBe32(&data[so.ibRecord + 8], uint32_t(idx.size() * isz));
        putBe32(&data[so.indexCountAt], uint32_t(idx.size()));
        if (!rb.tris.empty()) {
            ++rep.rebuilt;
            rep.notes.push_back("mesh " + std::to_string(rb.mesh) + ": " + std::to_string(rb.verts.size()) + " vertices, " + std::to_string(rb.tris.size() / 3) +
                                " triangles (was " + std::to_string(mesh.vertices.size()) + ", " + std::to_string(mesh.indices.size() / 3) + ")");
        } else {
            rep.notes.push_back("mesh " + std::to_string(rb.mesh) + ": hidden");
        }
    }

    for (auto& [sym, d] : gpu) c.replaceSymbolData(size_t(sym), d);
    c.replaceSymbolData(symbol, data);
    return rep;
}

// Fur shells the FBX export adds ("group0_mesh3_fur2"): not meshes of the model.
bool isFurShell(const std::string& name) {
    size_t k = name.rfind("_fur");
    if (k == std::string::npos || k + 4 == name.size()) return false;
    for (size_t i = k + 4; i < name.size(); ++i)
        if (name[i] < '0' || name[i] > '9') return false;
    return true;
}

// Bone names compared ignoring case and underscores.
std::string boneKey(const std::string& n) {
    std::string out;
    for (char ch : n)
        if (ch != '_') out += char(std::tolower(uint8_t(ch)));
    return out;
}

} // namespace

ImportReport importObj(caff::Caff& c, size_t symbol, const std::string& objText) { return applyObj(c, symbol, parseObj(objText)); }

ImportReport importFbx(caff::Caff& c, size_t symbol, const Bytes& file) {
    std::vector<fbx::MeshData> meshes = fbx::readMeshes(file);
    const Model m = parse(c, symbol);
    std::map<std::string, int> exact, loose;
    for (size_t i = 0; i < m.bones.size(); ++i) {
        exact.emplace(m.bones[i].name, int(i));
        loose.emplace(boneKey(m.bones[i].name), int(i));
    }
    std::set<std::string> unknownBones;
    auto boneIndex = [&](const std::string& name) {
        auto e = exact.find(name);
        if (e != exact.end()) return e->second;
        auto l = loose.find(boneKey(name));
        return l != loose.end() ? l->second : -1;
    };

    Obj o;
    size_t shells = 0, skinned = 0;
    for (auto& md : meshes) {
        if (isFurShell(md.name)) {
            ++shells;
            continue;
        }
        Obj::Object ob;
        ob.name = md.name;
        ob.firstV = o.v.size();
        o.v.insert(o.v.end(), md.points.begin(), md.points.end());
        ob.endV = o.v.size();
        ob.vSet = !md.points.empty();
        o.skin.resize(o.v.size());
        bool anySkin = false;
        for (size_t k = 0; k < md.points.size() && k < md.skin.size(); ++k)
            for (auto& [name, w] : md.skin[k]) {
                int b = boneIndex(name);
                if (b < 0) {
                    unknownBones.insert(name);
                    continue;
                }
                o.skin[ob.firstV + k].push_back({b, w});
                anySkin = true;
            }
        skinned += anySkin;
        // Shared corner values become one vt / vn line, so corners of a vertex stay one vertex.
        std::map<Vec2, int> uvAt;
        std::map<Vec3, int> nAt;
        for (auto& poly : md.polygons) {
            std::vector<Obj::Corner> cs;
            for (int k : poly) {
                Obj::Corner cn;
                cn.v = int(ob.firstV) + md.cornerPoint[size_t(k)];
                if (size_t(k) < md.cornerUv.size()) {
                    auto [it, fresh] = uvAt.emplace(md.cornerUv[size_t(k)], int(o.vt.size()));
                    if (fresh) o.vt.push_back(md.cornerUv[size_t(k)]);
                    cn.vt = it->second;
                }
                if (size_t(k) < md.cornerNormal.size()) {
                    auto [it, fresh] = nAt.emplace(md.cornerNormal[size_t(k)], int(o.vn.size()));
                    if (fresh) o.vn.push_back(md.cornerNormal[size_t(k)]);
                    cn.vn = it->second;
                }
                cs.push_back(cn);
            }
            for (size_t k = 1; k + 1 < cs.size(); ++k) ob.tris.push_back({cs[0], cs[k], cs[k + 1]});
        }
        o.objects.push_back(std::move(ob));
    }
    if (o.objects.empty()) fail(shells ? "the file holds only fur shells" : "the file has no meshes");
    ImportReport rep = applyObj(c, symbol, o);
    if (skinned) rep.notes.push_back(std::to_string(skinned) + " mesh(es) took their skin weights from the file");
    if (!unknownBones.empty()) {
        std::string list;
        size_t n = 0;
        for (auto& b : unknownBones) list += (n++ ? ", " : "") + b;
        rep.notes.push_back("weights on bones the model's skeleton lacks were dropped: " + list);
    }
    if (shells) rep.notes.push_back(std::to_string(shells) + " fur shell mesh(es) ignored (the game draws fur itself)");
    return rep;
}

ImportReport importMeshFile(caff::Caff& c, size_t symbol, const Bytes& file) {
    if (fbx::isFbx(file)) return importFbx(c, symbol, file);
    return importObj(c, symbol, std::string(file.begin(), file.end()));
}

// ---- copying a model -----------------------------------------------------------------------------

namespace {

// A model's own symbols: the .data part and the same-named symbols it points at (.gpu, .stream).
std::vector<size_t> modelGroup(const caff::Caff& c, size_t sym) {
    std::vector<size_t> g{sym};
    std::string name = c.symbolName(sym);
    for (int set = 0; set < 2; ++set)
        for (auto& r : c.relocs[set])
            if (r.from == sym + 1 && r.to != sym + 1 && c.symbolName(r.to - 1) == name && std::find(g.begin(), g.end(), r.to - 1) == g.end())
                g.push_back(r.to - 1);
    return g;
}

const caff::Pool* poolOf(const caff::Caff& c, size_t sym) {
    for (auto& p : c.pools)
        if (p.symbol == sym + 1) return &p;
    return nullptr;
}

// A texture asset ready to add to another CAFF: its header (.data) and pixels (.texturegpu), which
// share one name and point only at each other.
struct TextureCopy {
    std::string name, symbolName;
    Bytes data[2];
    uint8_t kind[2] = {};
    std::string section[2];
    struct Reloc {
        int set, from, to; // 0 = header, 1 = pixels
        std::vector<uint32_t> offsets;
    };
    std::vector<Reloc> relocs;
    std::vector<std::pair<int, caff::SwapEntry>> swaps; // set, entry (symbol = 1 or 2)
    std::optional<streambundle::Entry> top;
};

TextureCopy gatherTexture(const std::string& name, const TextureSource& src) {
    const caff::Caff& c = *src.caff;
    if (src.header >= c.symbols.size()) fail("texture " + name + ": bad source symbol");
    size_t sym[2] = {src.header, SIZE_MAX};
    for (size_t k = 0; k < c.symbols.size(); ++k)
        if (k != src.header && c.symbols[k].nameIndex == c.symbols[src.header].nameIndex && c.sectionOf(c.symbols[k]).name == ".texturegpu") sym[1] = k;
    if (sym[1] == SIZE_MAX) fail("texture " + name + ": its pixel data was not found next to its header");
    TextureCopy t;
    t.name = name;
    t.symbolName = c.symbolName(src.header);
    for (int i = 0; i < 2; ++i) {
        t.data[i] = c.symbolData(sym[i]);
        t.kind[i] = c.symbols[sym[i]].kind;
        t.section[i] = c.sectionOf(c.symbols[sym[i]]).name;
    }
    auto which = [&](uint32_t one) { return one == sym[0] + 1 ? 0 : one == sym[1] + 1 ? 1 : -1; };
    for (int set = 0; set < 2; ++set) {
        for (auto& r : c.relocs[set]) {
            int f = which(r.from), to = which(r.to);
            if (f < 0) continue;
            if (to < 0) fail("texture " + name + " refers to " + c.symbolName(r.to - 1) + ", which is not copied with it");
            t.relocs.push_back({set, f, to, r.offsets});
        }
        for (auto& w : c.swaps[set])
            if (which(w.symbol) >= 0) {
                caff::SwapEntry e = w;
                e.symbol = uint32_t(which(w.symbol) + 1);
                t.swaps.push_back({set, e});
            }
    }
    t.top = src.top;
    return t;
}

// A bundle's manifest (its last symbol, magic 0x438CB47C) lists its assets for the game:
//   +0x08 ->entries {u32 asset id, u32 symbol name index (0-based)}   +0x0C entry count
//   +0x10 ->bundle dependencies (u32 ids)  +0x14 count    +0x18 ->streamed asset ids  +0x1C count
// with the two lists after the entries. Adds an entry for each name (1-based indices).
void addManifestEntries(caff::Caff& c, const std::vector<uint32_t>& names) {
    if (names.empty() || c.symbols.empty() || c.symbolName(c.symbols.size() - 1) != "manifest") return;
    size_t ms = c.symbols.size() - 1;
    Bytes d = c.symbolData(ms);
    if (d.size() < 0x20 || be32(&d[0]) != 0x438CB47Cu) fail("unexpected bundle manifest");
    uint32_t at = be32(&d[8]), n = be32(&d[12]), end = at + 8 * n;
    if (end > d.size()) fail("bundle manifest entries outside it");
    Bytes add(8 * names.size());
    for (size_t k = 0; k < names.size(); ++k) {
        auto id = assetdb::idFromName(assetdb::baseAssetName(c.symbolNames.at(names[k] - 1)));
        if (!id) fail("no asset id for " + c.symbolNames[names[k] - 1]);
        putBe32(&add[8 * k], *id);
        putBe32(&add[8 * k + 4], names[k] - 1);
    }
    d.insert(d.begin() + end, add.begin(), add.end());
    putBe32(&d[12], n + uint32_t(names.size()));
    for (uint32_t field : {0x10u, 0x18u})
        if (be32(&d[field + 4]) && be32(&d[field]) >= end) putBe32(&d[field], be32(&d[field]) + uint32_t(add.size()));
    c.replaceSymbolData(ms, d);
}

// A symbol's bytes with the target of each relocated pointer in it, for reading strings.
struct SymView {
    const caff::Caff* c = nullptr;
    size_t sym = 0;
    Bytes data;
    std::map<uint32_t, uint32_t> target; // offset -> target symbol (0-based)
    SymView(const caff::Caff& cc, size_t s) : c(&cc), sym(s), data(cc.symbolData(s)) {
        for (int set = 0; set < 2; ++set)
            for (auto& r : cc.relocs[set])
                if (r.from == s + 1)
                    for (uint32_t o : r.offsets) target[o] = r.to - 1;
    }
    std::optional<uint32_t> targetOf(uint32_t at) const {
        auto it = target.find(at);
        if (it == target.end()) return std::nullopt;
        return it->second;
    }
    std::string cstr(uint32_t ptrAt) const {
        auto t = targetOf(ptrAt);
        if (!t || ptrAt + 4 > data.size()) return {};
        Bytes other;
        const Bytes& d = *t == sym ? data : (other = c->symbolData(*t));
        uint32_t o = be32(&data[ptrAt]);
        if (o >= d.size()) return {};
        size_t e = o;
        while (e < d.size() && d[e]) ++e;
        return std::string(reinterpret_cast<const char*>(&d[o]), e - o);
    }
};

// The rendergraph header in a model's .data bytes: chunk 0x1E of the chunk directory.
std::optional<uint32_t> rendergraphIn(const Bytes& d) {
    if (d.size() < 8) return std::nullopt;
    uint32_t dir = be32(&d[0]), n = be32(&d[4]);
    for (uint32_t i = 0; i < n && i < 64 && dir + 8 * i + 8 <= d.size(); ++i)
        if (be32(&d[dir + 8 * i]) == 0x1E) {
            uint32_t at = be32(&d[dir + 8 * i + 4]);
            if (at + 4 > d.size()) break;
            uint32_t rg = be32(&d[at]);
            if (rg + 0x108 <= d.size() && std::memcmp(&d[rg], "rendergraph", 11) == 0) return rg;
        }
    return std::nullopt;
}

int sectionIndex(const caff::Caff& c, const std::string& name) {
    for (size_t i = 0; i < c.sections.size(); ++i)
        if (c.sections[i].name == name) return int(i + 1);
    return -1;
}

} // namespace

fs::path pairedStreamBundle(const fs::path& bundle) { return bundle.parent_path().parent_path() / "50" / bundle.filename(); }

FindTexture texturesIn(std::shared_ptr<const caff::Caff> caff, const fs::path& streamBundle) {
    auto index = std::make_shared<std::optional<std::vector<streambundle::IndexEntry>>>();
    return [caff, streamBundle, index](const std::string& name) -> std::optional<TextureSource> {
        bool split = false;
        size_t header = SIZE_MAX;
        for (size_t s = 0; s < caff->symbols.size() && header == SIZE_MAX; ++s) {
            if (caff->sectionOf(caff->symbols[s]).name != ".data") continue;
            std::string n = assetdb::baseAssetName(caff->symbolName(s));
            if (n == name || n == name + "mip") {
                header = s;
                split = n != name;
            }
        }
        if (header == SIZE_MAX) return std::nullopt;
        TextureSource src{caff, header, std::nullopt};
        if (split) {
            // The top level is its own asset in the stream bundle, found by id.
            if (!index->has_value()) {
                std::vector<streambundle::IndexEntry> idx;
                if (!streamBundle.empty() && fs::exists(longPath(streamBundle))) streambundle::readIndex(longPath(streamBundle), idx);
                *index = std::move(idx);
            }
            auto id = assetdb::idFromName(name + "top");
            for (auto& e : **index)
                if (id && e.id == *id) src.top = streambundle::Entry{e.id, streambundle::readEntry(longPath(streamBundle), e)};
            if (!src.top) return std::nullopt; // half a texture would not load
        }
        return src;
    };
}

int addStreamEntries(streambundle::StreamBundle& sb, const std::vector<streambundle::Entry>& entries) {
    int added = 0;
    for (auto& e : entries) {
        bool have = std::any_of(sb.entries.begin(), sb.entries.end(), [&](const streambundle::Entry& x) { return x.id == e.id; });
        if (!have) {
            sb.entries.push_back(e);
            ++added;
        }
    }
    return added;
}

CopyResult copyModel(caff::Caff& dst, size_t symbol, const caff::Caff& srcIn, size_t srcSymbol, const FindTexture& find) {
    CopyResult result;
    std::vector<std::string>& notes = result.notes;
    const bool same = &srcIn == &dst;
    // The source may be `dst` itself; work from a snapshot of what is needed.
    std::vector<size_t> sg = modelGroup(srcIn, srcSymbol), dg = modelGroup(dst, symbol);
    if (same && std::find(sg.begin(), sg.end(), symbol) != sg.end()) fail("a model cannot replace itself");
    auto sectionName = [](const caff::Caff& c, size_t s) { return c.sectionOf(c.symbols[s]).name; };
    std::map<size_t, size_t> map; // source symbol -> destination symbol
    for (size_t s : sg) {
        size_t to = SIZE_MAX;
        for (size_t d : dg)
            if (sectionName(dst, d) == sectionName(srcIn, s)) to = d;
        if (to == SIZE_MAX) fail("the source model has a " + sectionName(srcIn, s) + " part the destination does not");
        map[s] = to;
    }
    for (size_t d : dg)
        if (std::none_of(map.begin(), map.end(), [&](auto& kv) { return kv.second == d; }))
            fail("the destination model has a " + sectionName(dst, d) + " part the source does not");

    struct SrcReloc {
        int set;
        uint32_t from, to;
        std::vector<uint32_t> offsets;
    };
    std::vector<SrcReloc> relocs;
    std::vector<caff::SwapEntry> swaps[2];
    std::map<size_t, Bytes> data;
    for (size_t s : sg) data[s] = srcIn.symbolData(s);
    for (int set = 0; set < 2; ++set) {
        for (auto& r : srcIn.relocs[set])
            if (map.count(r.from - 1)) relocs.push_back({set, r.from - 1, r.to - 1, r.offsets});
        for (auto& w : srcIn.swaps[set])
            if (map.count(w.symbol - 1)) swaps[set].push_back(w);
    }

    // Pointers into pools: the same pool when copying within a file, otherwise the destination's
    // pool of the same section, adding the entries it lacks.
    std::map<size_t, size_t> poolMap;          // source pool symbol -> destination pool symbol
    std::map<size_t, Bytes> poolData;          // destination pool symbols being extended
    std::map<size_t, caff::Pool> poolEdit;     // their entry lists
    int added = 0;
    for (auto& r : relocs) {
        if (map.count(r.to)) continue;
        const caff::Pool* sp = poolOf(srcIn, r.to);
        if (!sp) fail("the source model refers to " + srcIn.symbolName(r.to) + " in " + sectionName(srcIn, r.to) + ", which cannot be copied");
        size_t dp = r.to;
        if (!same) {
            dp = SIZE_MAX;
            for (auto& p : dst.pools)
                if (sectionName(dst, p.symbol - 1) == sectionName(srcIn, r.to)) dp = p.symbol - 1;
            if (dp == SIZE_MAX) fail("the destination has no " + sectionName(srcIn, r.to) + " pool");
        }
        poolMap[r.to] = dp;
        if (same) continue;
        Bytes srcPool = srcIn.symbolData(r.to);
        if (!poolData.count(dp)) {
            poolData[dp] = dst.symbolData(dp);
            poolEdit[dp] = *poolOf(dst, dp);
        }
        Bytes& pd = poolData[dp];
        caff::Pool& pe = poolEdit[dp];
        // Entries by start offset, for finding the one a pointer falls in.
        std::map<uint32_t, const caff::PoolString*> srcBy;
        for (auto& e : sp->strings) srcBy[e.offset] = &e;
        Bytes& sd = data[r.from];
        for (uint32_t off : r.offsets) {
            uint32_t v = be32(&sd[off]);
            auto it = srcBy.upper_bound(v);
            if (it == srcBy.begin()) fail("pool pointer outside every entry");
            --it;
            const caff::PoolString& e = *it->second;
            if (v >= e.offset + std::max<uint32_t>(e.length, 1)) fail("pool pointer outside every entry");
            if (e.offset + e.length > srcPool.size()) fail("pool entry outside its symbol");
            // The same entry in the destination: equal hash, length and bytes.
            const caff::PoolString* match = nullptr;
            for (auto& de : pe.strings)
                if (de.hash == e.hash && de.length == e.length && de.offset + de.length <= pd.size() &&
                    std::equal(srcPool.begin() + e.offset, srcPool.begin() + e.offset + e.length, pd.begin() + de.offset)) {
                    match = &de;
                    break;
                }
            uint32_t at;
            if (match) {
                at = match->offset;
            } else {
                // Keep the entry's alignment: the largest power of two (up to 4096) its source offset has.
                uint32_t align = 4096;
                while (align > 1 && e.offset % align) align >>= 1;
                at = alignUp(uint32_t(pd.size()), align);
                pd.resize(at, 0);
                pd.insert(pd.end(), srcPool.begin() + e.offset, srcPool.begin() + e.offset + e.length);
                // The game does not read pool tables at load (0x82A026B0 applies relocations only),
                // so the entry is simply listed last.
                caff::PoolString ne = e;
                ne.offset = at;
                pe.strings.push_back(ne);
                ++added;
            }
            putBe32(&sd[off], at + (v - e.offset));
        }
    }

    // Pointers from elsewhere into the destination's parts must point at their start (the asset table).
    for (int set = 0; set < 2; ++set)
        for (auto& r : dst.relocs[set]) {
            bool fromGroup = std::find(dg.begin(), dg.end(), r.from - 1) != dg.end();
            bool toGroup = std::find(dg.begin(), dg.end(), r.to - 1) != dg.end();
            if (fromGroup || !toGroup) continue;
            Bytes from = dst.symbolData(r.from - 1);
            for (uint32_t off : r.offsets)
                if (off + 4 <= from.size() && be32(&from[off]) != 0)
                    fail(dst.symbolName(r.from - 1) + " points inside " + dst.symbolName(symbol) + "; it cannot be replaced");
        }

    // Textures. When a model loads, the game binds each texturefileN to that texture's data
    // (0x822D8078 -> 0x822D90F0); one whose bundle is not loaded has none, and the game crashes
    // reading it. Textures the destination bundle holds, or the original model already used (so
    // something loads them), are fine; the others are copied in.
    Model a = parse(dst, symbol), b = parse(srcIn, srcSymbol);
    std::vector<TextureCopy> textures;
    {
        std::set<std::string> safe(a.textures.begin(), a.textures.end());
        std::vector<std::string> missing;
        for (auto& t : b.textures) {
            if (safe.count(t)) continue;
            bool held = false;
            for (size_t s = 0; s < dst.symbols.size() && !held; ++s) {
                // Texture symbols are named like a path ending in the asset; split ones add "mip"/"top".
                std::string n = assetdb::baseAssetName(dst.symbolName(s));
                held = n == t || n == t + "mip" || n == t + "top";
            }
            if (held) continue;
            std::optional<TextureSource> from = find ? find(t) : std::nullopt;
            if (!from || !from->caff) missing.push_back(t);
            else textures.push_back(gatherTexture(t, *from));
        }
        if (!missing.empty()) {
            std::string list;
            for (size_t i = 0; i < missing.size() && i < 4; ++i) list += "\n  " + missing[i];
            if (missing.size() > 4) list += "\n  ... and " + std::to_string(missing.size() - 4) + " more";
            fail(std::to_string(missing.size()) + " texture(s) of " + b.name + " are not in this bundle and could not be found to copy in "
                 "(the game crashes when a model's texture is not loaded):" + list);
        }
        for (auto& t : textures)
            for (auto& sec : t.section)
                if (sectionIndex(dst, sec) < 0) fail("the destination has no " + sec + " section for texture " + t.name);
    }
    // Skeletons. The game animates a model by bone index with the skeleton its animations were made
    // for (0x822109B0 blends pose buffers sized from it), so a different skeleton crashes it. The
    // copy is retargeted onto the original's skeleton: its pose pointer leads to the original's pose
    // (kept in a symbol of its own), the dictionary's bone entries are the original's, and every
    // vertex is re-bound to the original's bones by name.
    constexpr uint32_t kSkeleton = 0xFFFFFFF0u; // relocation target: the kept skeleton symbol
    std::optional<size_t> keptSkeleton;          // existing symbol holding the original's pose
    bool keepNewSkeleton = false;                // or: add a copy of the original .data
    Bytes dstOriginal;                           // the original .data, for that copy
    {
        std::vector<std::string> an, bn;
        for (auto& x : a.bones) an.push_back(x.name);
        for (auto& x : b.bones) bn.push_back(x.name);
        bool retarget = !a.bones.empty() && !b.bones.empty() && an != bn;
        if (retarget) {
            SymView dv(dst, symbol), sv(srcIn, srcSymbol);
            auto drg = rendergraphIn(dv.data), srg = rendergraphIn(sv.data);
            if (!drg || !srg) fail("cannot find the rendergraph to retarget the skeleton");
            auto poseSym = dv.targetOf(*drg + 0x104);
            if (!poseSym || !sv.targetOf(*srg + 0x104) || *sv.targetOf(*srg + 0x104) != srcSymbol) fail("unexpected skeleton layout");
            uint32_t poseAt = be32(&dv.data[*drg + 0x104]);
            // The skeleton lives in a kept symbol (this model was retargeted before) or in the .data part.
            if (*poseSym == symbol) {
                keepNewSkeleton = true;
                dstOriginal = dv.data;
            } else {
                keptSkeleton = *poseSym;
            }

            // Source bone -> original bone: same name, then the same name ignoring case and
            // underscores, then the nearest ancestor that maps.
            std::map<std::string, int> exact, loose;
            for (size_t i = 0; i < a.bones.size(); ++i) {
                exact.emplace(a.bones[i].name, int(i));
                loose.emplace(boneKey(a.bones[i].name), int(i));
            }
            std::vector<int> boneMap(b.bones.size(), -1);
            int byName = 0, byParent = 0;
            for (size_t i = 0; i < b.bones.size(); ++i) {
                auto e = exact.find(b.bones[i].name);
                auto f = loose.find(boneKey(b.bones[i].name));
                if (e != exact.end()) boneMap[i] = e->second;
                else if (f != loose.end()) boneMap[i] = f->second;
                if (boneMap[i] >= 0) ++byName;
            }
            for (size_t i = 0; i < b.bones.size(); ++i) {
                if (boneMap[i] >= 0) continue;
                int p = b.bones[i].parent;
                for (int guard = 0; p >= 0 && boneMap[size_t(p)] < 0 && guard < 256; ++guard) p = b.bones[size_t(p)].parent;
                boneMap[i] = p >= 0 ? boneMap[size_t(p)] : 0;
                ++byParent;
            }

            // Vertices are re-bound, not moved. In the bind pose a skin matrix is the identity whatever
            // the skeleton, so the model keeps its exact shape; animated, a joint turns about the
            // original's joint position, which for related rigs is close (fat banjo's hips, knees,
            // shoulders and elbows sit within 0.04 of the tuxedo's). Moving vertices by the bind
            // differences instead pulls parts whose bones differ (shorts, belly) out of shape.
            std::set<std::pair<int, uint32_t>> done;
            for (auto& mesh : b.meshes) {
                const auto& so = mesh.source;
                if (!mesh.layout.skinned || !done.insert({so.vbSymbol, so.vbOffset}).second) continue;
                auto it = data.find(size_t(so.vbSymbol));
                if (it == data.end()) fail("a vertex buffer of the source model is outside it");
                Bytes& g = it->second;
                const Layout& L = mesh.layout;
                for (uint32_t k = 0; k < so.vbCount; ++k) {
                    uint8_t* p = &g[so.vbOffset + size_t(k) * mesh.stride];
                    for (int q = 0; q < 4; ++q) {
                        uint16_t bi = be16(p + L.attributes + 2 * q);
                        if (bi < boneMap.size()) putBe16(p + L.attributes + 2 * q, uint16_t(boneMap[bi]));
                    }
                }
            }

            // The local relocation list of the copy's .data part.
            SrcReloc* local = nullptr;
            for (auto& r : relocs)
                if (r.from == srcSymbol && r.to == srcSymbol) { local = &r; break; }
            if (!local) fail("unexpected model layout (no local pointers)");
            std::vector<uint32_t> toSkeleton;
            Bytes& cd = data.at(srcSymbol);
            auto dropLocal = [&](uint32_t off) { local->offsets.erase(std::remove(local->offsets.begin(), local->offsets.end(), off), local->offsets.end()); };

            // Pose pointer -> the original's pose.
            putBe32(&cd[*srg + 0x104], poseAt);
            dropLocal(*srg + 0x104);
            toSkeleton.push_back(*srg + 0x104);

            // Dictionary: the source's entries without its bones, plus the original's bone entries.
            struct DictEntry {
                uint32_t hash, ent;
                bool skeleton; // ent is in the kept skeleton symbol
            };
            std::vector<DictEntry> entries;
            std::map<size_t, SymView> views;
            auto readDict = [&](const SymView& v, uint32_t rg, const std::set<std::string>& bones, bool keepBones, bool dst) {
                if (!v.targetOf(rg + 0x20)) return;
                uint32_t dict = be32(&v.data[rg + 0x20]), count = be32(&v.data[dict]), arr = be32(&v.data[dict + 8]);
                for (uint32_t k = 0; k < count; ++k) {
                    uint32_t e = arr + 12 * k, ent = be32(&v.data[e + 8]);
                    auto entSym = v.targetOf(e + 8);
                    if (!entSym) continue;
                    // The entry record: in this symbol, or (an original retargeted before) in the kept one.
                    if (*entSym != v.sym && !views.count(*entSym)) views.emplace(*entSym, SymView(*v.c, *entSym));
                    const SymView& ev = *entSym == v.sym ? v : views.at(*entSym);
                    bool isInt = !ev.targetOf(ent + 8);
                    bool bone = isInt && bones.count(ev.cstr(ent));
                    if (bone != keepBones) continue;
                    entries.push_back({be32(&v.data[e]), ent, dst});
                }
            };
            std::set<std::string> aSet(an.begin(), an.end()), bSet(bn.begin(), bn.end());
            readDict(sv, *srg, bSet, false, false);
            readDict(dv, *drg, aSet, true, true);
            std::stable_sort(entries.begin(), entries.end(), [](const DictEntry& x, const DictEntry& y) { return x.hash < y.hash; });
            uint32_t arrAt = alignUp(uint32_t(cd.size()), 4);
            cd.resize(arrAt + 12 * entries.size(), 0);
            for (size_t k = 0; k < entries.size(); ++k) {
                uint32_t e = arrAt + 12 * uint32_t(k);
                uint32_t dup = 0;
                for (size_t j = k; j > 0 && entries[j - 1].hash == entries[k].hash; --j) ++dup;
                putBe32(&cd[e], entries[k].hash);
                putBe32(&cd[e + 4], dup);
                putBe32(&cd[e + 8], entries[k].ent);
                if (entries[k].skeleton) toSkeleton.push_back(e + 8);
                else local->offsets.push_back(e + 8);
            }
            uint32_t dict = be32(&cd[*srg + 0x20]);
            putBe32(&cd[dict], uint32_t(entries.size()));
            putBe32(&cd[dict + 8], arrAt);
            std::sort(local->offsets.begin(), local->offsets.end());
            relocs.push_back({0, uint32_t(srcSymbol), kSkeleton, toSkeleton});

            notes.push_back("retargeted onto the original's skeleton (" + std::to_string(a.bones.size()) + " bones): " + std::to_string(byName) + " of its " +
                            std::to_string(b.bones.size()) + " bones matched by name" +
                            (byParent ? ", " + std::to_string(byParent) + " follow their nearest matching parent" : ""));
        }
        if (b.switches.size() < a.switches.size() || (b.nodes.empty() && !a.nodes.empty()))
            notes.push_back("the new model has " + std::to_string(b.switches.size()) + " part switch(es), the original " + std::to_string(a.switches.size()) +
                            "; parts the game switches on and off (Kazooie, the wrench...) may be missing");
    }

    // Apply. A copy of the original .data part keeps its skeleton (not an asset, so not in the manifest).
    std::vector<caff::Relocation> skeletonRelocs[2];
    std::vector<caff::SwapEntry> skeletonSwaps[2];
    if (keepNewSkeleton) {
        for (int set = 0; set < 2; ++set) {
            for (auto& r : dst.relocs[set]) {
                if (r.from != symbol + 1) continue;
                bool self = r.to == symbol + 1, pool = poolOf(dst, r.to - 1) != nullptr;
                if (self || pool) skeletonRelocs[set].push_back({0, self ? 0u : r.to, r.offsets}); // from/to(self) set below
            }
            for (auto& w : dst.swaps[set])
                if (w.symbol == symbol + 1) skeletonSwaps[set].push_back(w);
        }
        uint32_t name = dst.symbolNameIndex(dst.symbolName(symbol) + "_skeleton");
        size_t k = dst.appendSymbol(name, dst.symbols[symbol].section, dst.symbols[symbol].kind, dstOriginal);
        keptSkeleton = k;
        for (int set = 0; set < 2; ++set) {
            for (auto& r : skeletonRelocs[set]) {
                r.from = uint32_t(k + 1);
                if (r.to == 0) r.to = uint32_t(k + 1);
                dst.relocs[set].push_back(r);
            }
            for (auto w : skeletonSwaps[set]) {
                w.symbol = uint32_t(k + 1);
                dst.swaps[set].push_back(w);
            }
        }
    }
    // Relocations and swap entries of the destination parts are replaced by the source's.
    std::set<uint32_t> dset;
    for (size_t d : dg) dset.insert(uint32_t(d + 1));
    for (int set = 0; set < 2; ++set) {
        auto& rl = dst.relocs[set];
        auto firstOwn = std::find_if(rl.begin(), rl.end(), [&](const caff::Relocation& r) { return dset.count(r.from); });
        size_t insertAt = size_t(firstOwn - rl.begin());
        rl.erase(std::remove_if(rl.begin(), rl.end(), [&](const caff::Relocation& r) { return dset.count(r.from); }), rl.end());
        insertAt = std::min(insertAt, rl.size());
        std::vector<caff::Relocation> add;
        for (auto& r : relocs) {
            if (r.set != set) continue;
            caff::Relocation nr;
            nr.from = uint32_t(map.at(r.from) + 1);
            nr.to = r.to == kSkeleton ? uint32_t(*keptSkeleton + 1) : uint32_t((map.count(r.to) ? map.at(r.to) : poolMap.at(r.to)) + 1);
            nr.offsets = r.offsets;
            add.push_back(std::move(nr));
        }
        rl.insert(rl.begin() + std::ptrdiff_t(insertAt), add.begin(), add.end());

        auto& sw = dst.swaps[set];
        auto firstSw = std::find_if(sw.begin(), sw.end(), [&](const caff::SwapEntry& w) { return dset.count(w.symbol); });
        size_t swAt = size_t(firstSw - sw.begin());
        sw.erase(std::remove_if(sw.begin(), sw.end(), [&](const caff::SwapEntry& w) { return dset.count(w.symbol); }), sw.end());
        swAt = std::min(swAt, sw.size());
        std::vector<caff::SwapEntry> addSw;
        for (auto w : swaps[set]) {
            w.symbol = uint32_t(map.at(w.symbol - 1) + 1);
            addSw.push_back(std::move(w));
        }
        sw.insert(sw.begin() + std::ptrdiff_t(swAt), addSw.begin(), addSw.end());
    }
    for (auto& [dp, pd] : poolData) {
        dst.replaceSymbolData(dp, pd);
        for (auto& p : dst.pools)
            if (p.symbol == dp + 1) p.strings = poolEdit[dp].strings;
    }
    for (auto& [s, d] : data) dst.replaceSymbolData(map.at(s), d);
    if (added) notes.push_back("added " + std::to_string(added) + " shared string(s) and shader(s) to the destination's pools");

    // The textures it needs, as new symbols listed in the bundle's manifest (and their streamed top
    // levels for the stream bundle).
    std::vector<uint32_t> newNames;
    for (auto& t : textures) {
        uint32_t nameIndex = dst.symbolNameIndex(t.symbolName);
        newNames.push_back(nameIndex);
        size_t at[2];
        for (int i = 0; i < 2; ++i) at[i] = dst.appendSymbol(nameIndex, uint8_t(sectionIndex(dst, t.section[i])), t.kind[i], t.data[i]);
        for (auto& r : t.relocs) {
            caff::Relocation nr;
            nr.from = uint32_t(at[r.from] + 1);
            nr.to = uint32_t(at[r.to] + 1);
            nr.offsets = r.offsets;
            dst.relocs[r.set].push_back(std::move(nr));
        }
        for (auto& [set, w] : t.swaps) {
            caff::SwapEntry e = w;
            e.symbol = uint32_t(at[w.symbol - 1] + 1);
            dst.swaps[set].push_back(std::move(e));
        }
        if (t.top) result.streamEntries.push_back(*t.top);
    }
    addManifestEntries(dst, newNames);
    if (!textures.empty()) {
        size_t bytes = 0;
        for (auto& t : textures) bytes += t.data[0].size() + t.data[1].size() + (t.top ? t.top->data.size() : 0);
        notes.push_back("copied " + std::to_string(textures.size()) + " texture(s) it needs into the bundle (" + std::to_string(bytes / 1024) + " KB" +
                        (result.streamEntries.empty() ? "" : ", " + std::to_string(result.streamEntries.size()) + " streamed top level(s) for the stream bundle") +
                        ")");
    }
    return result;
}

} // namespace nut::model
