#include "fbx.hpp"

#include "deflate.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>

namespace nut::fbx {
namespace {

// ---- binary FBX nodes -------------------------------------------------------------------------
// A node record: u32 end offset, u32 property count, u32 property bytes, u8 name length, name,
// properties, child records, and a 13-byte null record closing the child list.

void le32(Bytes& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
}
void le64(Bytes& b, uint64_t v) {
    for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i)));
}
void put32At(Bytes& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + size_t(i)] = uint8_t(v >> (8 * i));
}
uint64_t bitsOf(double d) {
    uint64_t u;
    std::memcpy(&u, &d, 8);
    return u;
}
uint32_t bitsOf(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

struct Node {
    std::string name;
    Bytes props;
    uint32_t count = 0;
    std::vector<std::unique_ptr<Node>> kids;
    bool block = false; // always closed with a null record, even without children (objects: readers expect a scope)

    explicit Node(std::string n) : name(std::move(n)) {}
    Node& add(const std::string& n) {
        kids.push_back(std::make_unique<Node>(n));
        return *kids.back();
    }
    Node& I(int32_t v) { return tag('I'), le32(props, uint32_t(v)), *this; }
    Node& L(int64_t v) { return tag('L'), le64(props, uint64_t(v)), *this; }
    Node& D(double v) { return tag('D'), le64(props, bitsOf(v)), *this; }
    Node& C(bool v) { return tag('C'), props.push_back(v ? 1 : 0), *this; }
    Node& S(const std::string& s) {
        tag('S');
        le32(props, uint32_t(s.size()));
        props.insert(props.end(), s.begin(), s.end());
        return *this;
    }
    Node& R(const Bytes& b) {
        tag('R');
        le32(props, uint32_t(b.size()));
        props.insert(props.end(), b.begin(), b.end());
        return *this;
    }
    Node& ai(const std::vector<int32_t>& v) {
        Bytes raw;
        for (int32_t x : v) le32(raw, uint32_t(x));
        return array('i', v.size(), raw);
    }
    Node& al(const std::vector<int64_t>& v) {
        Bytes raw;
        for (int64_t x : v) le64(raw, uint64_t(x));
        return array('l', v.size(), raw);
    }
    Node& af(const std::vector<float>& v) {
        Bytes raw;
        for (float x : v) le32(raw, bitsOf(x));
        return array('f', v.size(), raw);
    }
    Node& ad(const std::vector<double>& v) {
        Bytes raw;
        for (double x : v) le64(raw, bitsOf(x));
        return array('d', v.size(), raw);
    }

private:
    void tag(char t) {
        props.push_back(uint8_t(t));
        ++count;
    }
    // Arrays: u32 length, u32 encoding (0 raw, 1 zlib), u32 byte size, data. Larger ones are compressed.
    Node& array(char t, size_t n, const Bytes& raw) {
        tag(t);
        le32(props, uint32_t(n));
        Bytes z = raw.size() >= 128 ? deflate::zlibCompress(raw.data(), raw.size()) : Bytes();
        bool packed = !z.empty() && z.size() < raw.size();
        const Bytes& data = packed ? z : raw;
        le32(props, packed ? 1 : 0);
        le32(props, uint32_t(data.size()));
        props.insert(props.end(), data.begin(), data.end());
        return *this;
    }
};

void serialize(Bytes& out, const Node& n) {
    size_t start = out.size();
    le32(out, 0);
    le32(out, n.count);
    le32(out, uint32_t(n.props.size()));
    out.push_back(uint8_t(n.name.size()));
    out.insert(out.end(), n.name.begin(), n.name.end());
    out.insert(out.end(), n.props.begin(), n.props.end());
    for (auto& k : n.kids) serialize(out, *k);
    if (!n.kids.empty() || n.count == 0 || n.block) out.insert(out.end(), 13, 0);
    put32At(out, start, uint32_t(out.size()));
}

// Properties70 entries.
Node& P(Node& p70, const char* name, const char* type, const char* label, const char* flags) {
    return p70.add("P").S(name).S(type).S(label).S(flags);
}
void pInt(Node& p, const char* name, int32_t v) { P(p, name, "int", "Integer", "").I(v); }
void pEnum(Node& p, const char* name, int32_t v) { P(p, name, "enum", "", "").I(v); }
void pBool(Node& p, const char* name, bool v) { P(p, name, "bool", "", "").I(v ? 1 : 0); }
void pDouble(Node& p, const char* name, double v) { P(p, name, "double", "Number", "").D(v); }
void pTime(Node& p, const char* name, int64_t v) { P(p, name, "KTime", "Time", "").L(v); }
void pString(Node& p, const char* name, const std::string& v) { P(p, name, "KString", "", "").S(v); }
void pColour(Node& p, const char* name, double r, double g, double b) { P(p, name, "Color", "", "A").D(r).D(g).D(b); }
void pVec(Node& p, const char* name, const char* type, const double* v) { P(p, name, type, "", "A").D(v[0]).D(v[1]).D(v[2]); }

// "name\0\1Class": how object names are stored in binary files.
std::string objName(const std::string& name, const char* cls) { return name + std::string("\0\1", 2) + cls; }

constexpr int64_t kTicksPerSecond = 46186158000LL;
int64_t ktime(double seconds) { return int64_t(std::llround(seconds * double(kTicksPerSecond))); }

// ---- transforms -----------------------------------------------------------------------------

using Mat4 = std::array<double, 16>; // column-major
Mat4 identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            double s = 0;
            for (int k = 0; k < 4; ++k) s += a[size_t(k * 4 + row)] * b[size_t(c * 4 + k)];
            r[size_t(c * 4 + row)] = s;
        }
    return r;
}
void normalize(double q[4]) {
    double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (n > 0)
        for (int i = 0; i < 4; ++i) q[i] /= n;
    else
        q[0] = q[1] = q[2] = 0, q[3] = 1;
}
Mat4 trs(const double* t, const double* qIn, const double* s) {
    double q[4] = {qIn[0], qIn[1], qIn[2], qIn[3]};
    normalize(q);
    double x = q[0], y = q[1], z = q[2], w = q[3];
    Mat4 m = identity();
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
Mat4 rigidInverse(const Mat4& m) {
    Mat4 r = identity();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r[size_t(j * 4 + i)] = m[size_t(i * 4 + j)];
    for (int i = 0; i < 3; ++i) r[size_t(12 + i)] = -(r[size_t(i)] * m[12] + r[size_t(4 + i)] * m[13] + r[size_t(8 + i)] * m[14]);
    return r;
}
std::vector<double> asArray(const Mat4& m) { return std::vector<double>(m.begin(), m.end()); }

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = 180.0 / kPi;

// XYZ Euler angles (radians) of a quaternion: R = Rz * Ry * Rx, FBX's default rotation order.
std::array<double, 3> euler(const double* qIn) {
    double q[4] = {qIn[0], qIn[1], qIn[2], qIn[3]};
    normalize(q);
    double x = q[0], y = q[1], z = q[2], w = q[3];
    double m00 = 1 - 2 * (y * y + z * z), m10 = 2 * (x * y + z * w), m20 = 2 * (x * z - y * w);
    double m21 = 2 * (y * z + x * w), m22 = 1 - 2 * (x * x + y * y);
    double m12 = 2 * (y * z - x * w), m11 = 1 - 2 * (x * x + z * z);
    double b = std::asin(std::clamp(-m20, -1.0, 1.0));
    if (std::cos(b) > 1e-6) return {std::atan2(m21, m22), b, std::atan2(m10, m00)};
    return {std::atan2(-m12, m11), b, 0.0};
}

// The equivalent angles closest to `prev`, so curves do not jump by 360 degrees.
std::array<double, 3> continuous(std::array<double, 3> e, const std::array<double, 3>& prev) {
    auto wrapNear = [](double a, double ref) { return a + 2 * kPi * std::round((ref - a) / (2 * kPi)); };
    std::array<double, 3> alt = {e[0] + kPi, kPi - e[1], e[2] + kPi};
    double best = 1e300;
    std::array<double, 3> out = e;
    for (auto cand : {e, alt}) {
        double d = 0;
        for (int i = 0; i < 3; ++i) {
            cand[size_t(i)] = wrapNear(cand[size_t(i)], prev[size_t(i)]);
            d += std::abs(cand[size_t(i)] - prev[size_t(i)]);
        }
        if (d < best) best = d, out = cand;
    }
    return out;
}

// A bone's local transform at time t, as the viewer poses it (doc_model.cpp applyPose).
void sampleLocal(const model::Bone& b, const model::Animation& a, int track, double t, double tr[3], double q[4], double s[3]) {
    for (int c = 0; c < 3; ++c) tr[c] = b.local[c], s[c] = 1;
    for (int c = 0; c < 4; ++c) q[c] = b.rotation[c];
    if (track < 0 || uint32_t(track) >= a.bones || a.frames <= 0 || a.keys.size() < size_t(a.frames) * a.bones) return;
    int f0 = 0, f1 = 0;
    double w = 0;
    if (a.frameTimes.size() == size_t(a.frames) && a.frames > 1) {
        auto it = std::upper_bound(a.frameTimes.begin(), a.frameTimes.end(), float(t));
        f1 = std::clamp(int(it - a.frameTimes.begin()), 1, a.frames - 1);
        f0 = f1 - 1;
        double span = a.frameTimes[size_t(f1)] - a.frameTimes[size_t(f0)];
        w = span > 0 ? std::clamp((t - a.frameTimes[size_t(f0)]) / span, 0.0, 1.0) : 0.0;
    }
    const auto& k0 = a.keys[size_t(f0) * a.bones + size_t(track)];
    const auto& k1 = a.keys[size_t(f1) * a.bones + size_t(track)];
    double dot = 0;
    for (int c = 0; c < 4; ++c) dot += double(k0.rotation[c]) * k1.rotation[c];
    double sign = dot < 0 ? -1 : 1;
    for (int c = 0; c < 4; ++c) q[c] = k0.rotation[c] * (1 - w) + sign * k1.rotation[c] * w;
    for (int c = 0; c < 3; ++c) {
        tr[c] += k0.translation[c] * (1 - w) + k1.translation[c] * w;
        s[c] = k0.scale[c] * (1 - w) + k1.scale[c] * w;
    }
}

// Keeps the keys linear interpolation needs to stay within `tol` of every sample (a channel that
// never changes keeps one key).
void reduceKeys(const std::vector<float>& v, const std::vector<int64_t>& t, float tol, std::vector<int64_t>& kt, std::vector<float>& kv) {
    size_t n = v.size();
    if (std::all_of(v.begin(), v.end(), [&](float x) { return std::abs(x - v.front()) <= tol; })) {
        kt = {0};
        kv = {v.front()};
        return;
    }
    size_t a = 0;
    kt.push_back(t[0]);
    kv.push_back(v[0]);
    while (a + 1 < n) {
        // Furthest b whose straight line from a covers every sample between.
        size_t b = a + 1;
        for (size_t c = a + 2; c < n; ++c) {
            bool fits = true;
            for (size_t k = a + 1; k < c && fits; ++k) {
                double f = double(t[k] - t[a]) / double(t[c] - t[a]);
                fits = std::abs(v[a] + (v[c] - v[a]) * f - v[k]) <= tol;
            }
            if (!fits) break;
            b = c;
        }
        kt.push_back(t[b]);
        kv.push_back(v[b]);
        a = b;
    }
}

std::string shortTextureName(const std::string& n) {
    for (const char* p : {"aid_texture_banjox_", "aid_texture_banjoxdlc_", "aid_texture_"})
        if (startsWith(n, p)) return n.substr(std::strlen(p));
    return n;
}

std::string shortModelName(const std::string& n) {
    for (const char* p : {"aid_model_banjox_", "aid_model_banjoxdlc_", "aid_model_"})
        if (startsWith(n, p)) return n.substr(std::strlen(p));
    return n;
}

} // namespace

Bytes write(const model::Model& m, const Options& opt, Report* report) {
    Report rep;
    int64_t nextId = 1000000;
    auto newId = [&] { return nextId++; };

    Node objects("Objects"), connections("Connections");
    auto oo = [&](int64_t child, int64_t parent) { connections.add("C").S("OO").L(child).L(parent); };
    auto op = [&](int64_t child, int64_t parent, const char* prop) { connections.add("C").S("OP").L(child).L(parent).S(prop); };
    std::map<std::string, int> objectCounts; // for Definitions
    auto object = [&](const char* type) -> Node& {
        ++objectCounts[type];
        Node& n = objects.add(type);
        n.block = true;
        return n;
    };
    auto modelNode = [&](int64_t id, const std::string& name, const char* kind) -> Node& {
        Node& n = object("Model").L(id).S(objName(name, "Model")).S(kind);
        n.add("Version").I(232);
        return n;
    };

    // Root: a Null named after the model, holding the skeleton and the meshes.
    std::string rootName = shortModelName(m.name);
    int64_t rootId = newId();
    {
        int64_t attr = newId();
        object("NodeAttribute").L(attr).S(objName(rootName, "NodeAttribute")).S("Null").add("TypeFlags").S("Null");
        Node& n = modelNode(rootId, rootName, "Null");
        n.add("Properties70");
        n.add("Shading").C(true);
        n.add("Culling").S("CullingOff");
        oo(attr, rootId);
        oo(rootId, 0);
    }

    // Skeleton. World bind matrices follow the hierarchy, whatever order the bones are listed in.
    size_t nb = m.bones.size();
    std::vector<int64_t> boneId(nb);
    std::vector<Mat4> bindWorld(nb, identity());
    {
        std::vector<int> state(nb, 0); // 0 todo, 1 in progress, 2 done
        std::function<void(size_t)> solve = [&](size_t i) {
            if (state[i] == 2) return;
            state[i] = 1;
            const auto& b = m.bones[i];
            double t[3] = {b.local[0], b.local[1], b.local[2]}, q[4] = {b.rotation[0], b.rotation[1], b.rotation[2], b.rotation[3]}, one[3] = {1, 1, 1};
            Mat4 local = trs(t, q, one);
            int p = b.parent;
            if (p >= 0 && size_t(p) < nb && state[size_t(p)] != 1) {
                solve(size_t(p));
                bindWorld[i] = mul(bindWorld[size_t(p)], local);
            } else {
                bindWorld[i] = local;
            }
            state[i] = 2;
        };
        for (size_t i = 0; i < nb; ++i) solve(i);
    }
    for (size_t i = 0; i < nb; ++i) {
        const auto& b = m.bones[i];
        boneId[i] = newId();
        int64_t attr = newId();
        std::string name = b.name.empty() ? "bone" + std::to_string(i) : b.name;
        Node& a = object("NodeAttribute").L(attr).S(objName(name, "NodeAttribute")).S("LimbNode");
        a.add("TypeFlags").S("Skeleton");
        Node& n = modelNode(boneId[i], name, "LimbNode");
        Node& p70 = n.add("Properties70");
        double t[3] = {b.local[0], b.local[1], b.local[2]}, q[4] = {b.rotation[0], b.rotation[1], b.rotation[2], b.rotation[3]};
        auto e = euler(q);
        double r[3] = {e[0] * kDeg, e[1] * kDeg, e[2] * kDeg};
        pVec(p70, "Lcl Translation", "Lcl Translation", t);
        pVec(p70, "Lcl Rotation", "Lcl Rotation", r);
        n.add("Shading").C(true);
        n.add("Culling").S("CullingOff");
        oo(attr, boneId[i]);
    }
    for (size_t i = 0; i < nb; ++i) {
        int p = m.bones[i].parent;
        oo(boneId[i], p >= 0 && size_t(p) < nb && size_t(p) != i ? boneId[size_t(p)] : rootId);
    }
    rep.bones = nb;

    // Textures (colour maps), created on first use.
    std::map<int, int64_t> textureId;
    auto textureFor = [&](int tex) -> int64_t {
        auto it = textureId.find(tex);
        if (it != textureId.end()) return it->second;
        const std::string& full = m.textures[size_t(tex)];
        std::string name = shortTextureName(full), file = full + ".png";
        int64_t id = newId();
        Node& t = object("Texture").L(id).S(objName(name, "Texture")).S("");
        t.add("Type").S("TextureVideoClip");
        t.add("Version").I(202);
        t.add("TextureName").S(objName(name, "Texture"));
        Node& p70 = t.add("Properties70");
        pBool(p70, "UseMaterial", true);
        t.add("Media").S(objName(name, "Video"));
        t.add("FileName").S(file);
        t.add("RelativeFilename").S(file);
        t.add("ModelUVTranslation").D(0).D(0);
        t.add("ModelUVScaling").D(1).D(1);
        t.add("Texture_Alpha_Source").S("None");
        t.add("Cropping").I(0).I(0).I(0).I(0);
        std::optional<Bytes> png = opt.texture ? opt.texture(full) : std::nullopt;
        int64_t vid = newId();
        Node& v = object("Video").L(vid).S(objName(name, "Video")).S("Clip");
        v.add("Type").S("Clip");
        Node& vp = v.add("Properties70");
        P(vp, "Path", "KString", "XRefUrl", "").S(file);
        v.add("UseMipMap").I(0);
        v.add("Filename").S(file);
        v.add("RelativeFilename").S(file);
        if (png) {
            v.add("Content").R(*png);
            ++rep.embedded;
        } else {
            rep.notes.push_back("texture " + full + " not embedded (not found)");
        }
        oo(vid, id);
        ++rep.textures;
        textureId[tex] = id;
        return id;
    };

    // Materials: one per distinct colour map + colour.
    std::map<std::tuple<int, float, float, float, std::string>, int64_t> materialId;
    std::map<std::string, int> materialNames;
    auto materialFor = [&](const model::Mesh& me) -> int64_t {
        int tex = me.texture >= 0 && size_t(me.texture) < m.textures.size() ? me.texture : -1;
        float r = 1, g = 1, b = 1;
        if (me.hasColour || tex < 0) r = me.colour[0], g = me.colour[1], b = me.colour[2];
        auto key = std::make_tuple(tex, r, g, b, me.material);
        auto it = materialId.find(key);
        if (it != materialId.end()) return it->second;
        std::string base = tex >= 0 ? shortTextureName(m.textures[size_t(tex)]) : me.material.empty() ? std::string("material") : me.material;
        int k = materialNames[base]++;
        std::string name = k ? base + "_" + std::to_string(k) : base;
        int64_t id = newId();
        Node& n = object("Material").L(id).S(objName(name, "Material")).S("");
        n.add("Version").I(102);
        n.add("ShadingModel").S("phong");
        n.add("MultiLayer").I(0);
        Node& p70 = n.add("Properties70");
        pColour(p70, "DiffuseColor", r, g, b);
        pDouble(p70, "DiffuseFactor", 1);
        pColour(p70, "SpecularColor", me.specular[0], me.specular[1], me.specular[2]);
        pDouble(p70, "SpecularFactor", 1);
        pDouble(p70, "Shininess", me.specularPower);
        pDouble(p70, "ShininessExponent", me.specularPower);
        pDouble(p70, "Opacity", 1);
        if (tex >= 0) op(textureFor(tex), id, "DiffuseColor");
        ++rep.materials;
        materialId[key] = id;
        return id;
    };

    // Meshes.
    std::vector<int64_t> meshModels;
    for (size_t i = 0; i < m.meshes.size(); ++i) {
        const auto& me = m.meshes[i];
        bool use = i < opt.include.size() ? opt.include[i] : size_t(me.group) < m.groups.size() && m.groups[size_t(me.group)].shown;
        if (!use || me.vertices.empty() || me.indices.size() < 3) continue;
        std::string name = "group" + std::to_string(me.group) + "_mesh" + std::to_string(i);
        size_t nv = me.vertices.size(), ni = me.indices.size() / 3 * 3;

        std::vector<double> pos, normals, uv, uv2;
        pos.reserve(nv * 3);
        for (auto& v : me.vertices) pos.insert(pos.end(), {double(v.pos[0]), double(v.pos[1]), double(v.pos[2])});
        std::vector<int32_t> poly(ni), uvIndex(ni);
        normals.reserve(ni * 3);
        for (size_t k = 0; k < ni; ++k) {
            uint32_t vi = me.indices[k] < nv ? me.indices[k] : 0;
            poly[k] = k % 3 == 2 ? ~int32_t(vi) : int32_t(vi);
            uvIndex[k] = int32_t(vi);
            const auto& v = me.vertices[vi];
            normals.insert(normals.end(), {double(v.normal[0]), double(v.normal[1]), double(v.normal[2])});
        }
        bool second = me.layout.uvSets.size() >= 2;
        for (auto& v : me.vertices) {
            uv.insert(uv.end(), {double(v.uv[0]), 1.0 - double(v.uv[1])});
            if (second) uv2.insert(uv2.end(), {double(v.uv2[0]), 1.0 - double(v.uv2[1])});
        }

        int64_t geo = newId();
        Node& g = object("Geometry").L(geo).S(objName(name, "Geometry")).S("Mesh");
        g.add("Properties70");
        g.add("GeometryVersion").I(124);
        g.add("Vertices").ad(pos);
        g.add("PolygonVertexIndex").ai(poly);
        {
            Node& ln = g.add("LayerElementNormal").I(0);
            ln.add("Version").I(101);
            ln.add("Name").S("");
            ln.add("MappingInformationType").S("ByPolygonVertex");
            ln.add("ReferenceInformationType").S("Direct");
            ln.add("Normals").ad(normals);
        }
        auto uvLayer = [&](int index, const char* layerName, const std::vector<double>& data) {
            Node& lu = g.add("LayerElementUV").I(index);
            lu.add("Version").I(101);
            lu.add("Name").S(layerName);
            lu.add("MappingInformationType").S("ByPolygonVertex");
            lu.add("ReferenceInformationType").S("IndexToDirect");
            lu.add("UV").ad(data);
            lu.add("UVIndex").ai(uvIndex);
        };
        uvLayer(0, "UVMap", uv);
        if (second) uvLayer(1, "UVMap2", uv2);
        {
            Node& lm = g.add("LayerElementMaterial").I(0);
            lm.add("Version").I(101);
            lm.add("Name").S("");
            lm.add("MappingInformationType").S("AllSame");
            lm.add("ReferenceInformationType").S("IndexToDirect");
            lm.add("Materials").ai({0});
        }
        {
            Node& layer = g.add("Layer").I(0);
            layer.add("Version").I(100);
            for (const char* t : {"LayerElementNormal", "LayerElementMaterial", "LayerElementUV"}) {
                Node& e = layer.add("LayerElement");
                e.add("Type").S(t);
                e.add("TypedIndex").I(0);
            }
        }
        if (second) {
            Node& layer = g.add("Layer").I(1);
            layer.add("Version").I(100);
            Node& e = layer.add("LayerElement");
            e.add("Type").S("LayerElementUV");
            e.add("TypedIndex").I(1);
        }

        int64_t mid = newId();
        Node& mn = modelNode(mid, name, "Mesh");
        mn.add("Properties70");
        mn.add("Shading").C(true);
        mn.add("Culling").S("CullingOff");
        oo(mid, rootId);
        oo(geo, mid);
        oo(materialFor(me), mid);
        meshModels.push_back(mid);

        // Skin: one cluster per bone the mesh uses.
        if (me.layout.skinned && nb) {
            std::map<size_t, std::map<int32_t, double>> perBone;
            for (size_t v = 0; v < nv; ++v)
                for (int k = 0; k < 4; ++k) {
                    float w = me.vertices[v].weights[k];
                    size_t b = me.vertices[v].bones[k];
                    if (w > 0 && b < nb) perBone[b][int32_t(v)] += w;
                }
            if (!perBone.empty()) {
                int64_t skin = newId();
                Node& s = object("Deformer").L(skin).S(objName(name, "Deformer")).S("Skin");
                s.add("Version").I(101);
                s.add("Link_DeformAcuracy").D(50);
                oo(skin, geo);
                for (auto& [b, weights] : perBone) {
                    std::vector<int32_t> idx;
                    std::vector<double> wts;
                    for (auto& [v, w] : weights) idx.push_back(v), wts.push_back(w);
                    int64_t cl = newId();
                    Node& c = object("Deformer").L(cl).S(objName(m.bones[b].name, "SubDeformer")).S("Cluster");
                    c.add("Version").I(100);
                    c.add("UserData").S("").S("");
                    c.add("Indexes").ai(idx);
                    c.add("Weights").ad(wts);
                    // As other exporters write it: the mesh relative to the bone, and the bone's world matrix, at bind time.
                    c.add("Transform").ad(asArray(rigidInverse(bindWorld[b])));
                    c.add("TransformLink").ad(asArray(bindWorld[b]));
                    oo(cl, skin);
                    oo(boneId[b], cl);
                }
            }
        }
        ++rep.meshes;
        rep.vertices += nv;
        rep.triangles += ni / 3;
    }
    if (!rep.meshes) rep.notes.push_back("no meshes written");

    // Bind pose.
    if (nb) {
        int64_t id = newId();
        Node& p = object("Pose").L(id).S(objName(rootName, "Pose")).S("BindPose");
        p.add("Type").S("BindPose");
        p.add("Version").I(100);
        p.add("NbPoseNodes").I(int32_t(1 + nb + meshModels.size()));
        auto poseNode = [&](int64_t node, const Mat4& mat) {
            Node& pn = p.add("PoseNode");
            pn.add("Node").L(node);
            pn.add("Matrix").ad(asArray(mat));
        };
        poseNode(rootId, identity());
        for (size_t i = 0; i < nb; ++i) poseNode(boneId[i], bindWorld[i]);
        for (int64_t mm : meshModels) poseNode(mm, identity());
    }

    // Animations: one stack + base layer each; curves only for matched bones.
    double longest = 0;
    std::string firstTake;
    const float fps = opt.fps > 0 ? opt.fps : 30;
    for (auto& na : opt.animations) {
        if (!na.anim) continue;
        const model::Animation& a = *na.anim;
        std::map<std::string, int> byName;
        for (size_t t = 0; t < a.boneNames.size(); ++t) byName.emplace(a.boneNames[t], int(t));
        double dur = std::max(0.0f, a.duration);
        int frames = dur > 0 ? std::max(1, int(std::lround(dur * fps))) + 1 : 1;
        std::vector<int64_t> times(static_cast<size_t>(frames));
        std::vector<double> secs(static_cast<size_t>(frames));
        for (int f = 0; f < frames; ++f) {
            secs[size_t(f)] = std::min(dur, f / double(fps));
            times[size_t(f)] = ktime(secs[size_t(f)]);
        }
        int64_t stack = newId(), layer = newId();
        Node& st = object("AnimationStack").L(stack).S(objName(na.name, "AnimStack")).S("");
        Node& sp = st.add("Properties70");
        pTime(sp, "LocalStart", 0);
        pTime(sp, "LocalStop", ktime(dur));
        pTime(sp, "ReferenceStart", 0);
        pTime(sp, "ReferenceStop", ktime(dur));
        object("AnimationLayer").L(layer).S(objName("BaseLayer", "AnimLayer")).S("");
        oo(layer, stack);
        if (firstTake.empty()) firstTake = na.name;
        longest = std::max(longest, dur);

        int matched = 0;
        for (size_t b = 0; b < nb; ++b) {
            auto it = byName.find(m.bones[b].name);
            if (it == byName.end()) continue;
            ++matched;
            std::vector<float> ch[9]; // tx ty tz, rx ry rz (degrees), sx sy sz
            for (auto& c : ch) c.reserve(size_t(frames));
            std::array<double, 3> prev{};
            for (int f = 0; f < frames; ++f) {
                double tr[3], q[4], s[3];
                sampleLocal(m.bones[b], a, it->second, secs[size_t(f)], tr, q, s);
                auto e = euler(q);
                if (f) e = continuous(e, prev);
                prev = e;
                for (int c = 0; c < 3; ++c) {
                    ch[c].push_back(float(tr[c]));
                    ch[3 + c].push_back(float(e[size_t(c)] * kDeg));
                    ch[6 + c].push_back(float(s[c]));
                }
            }
            const char* props[3] = {"Lcl Translation", "Lcl Rotation", "Lcl Scaling"};
            const char* nodeNames[3] = {"T", "R", "S"};
            const char* axes[3] = {"d|X", "d|Y", "d|Z"};
            // Channels that stay at the bone's rest transform need no curves: importers take those
            // values from the bone (Blender fills every un-keyed channel of an animated bone that way).
            const auto& bone = m.bones[b];
            double rq[4] = {bone.rotation[0], bone.rotation[1], bone.rotation[2], bone.rotation[3]};
            auto re = euler(rq);
            const double rest[9] = {bone.local[0], bone.local[1], bone.local[2], re[0] * kDeg, re[1] * kDeg, re[2] * kDeg, 1, 1, 1};
            for (int kind = 0; kind < 3; ++kind) {
                float tol = kind == 1 ? 0.01f : 1e-4f;
                bool atRest = true;
                for (int c = 0; c < 3 && atRest; ++c)
                    for (float x : ch[kind * 3 + c])
                        if (std::abs(double(x) - rest[kind * 3 + c]) > tol) {
                            atRest = false;
                            break;
                        }
                if (atRest) continue;
                int64_t cn = newId();
                Node& n = object("AnimationCurveNode").L(cn).S(objName(nodeNames[kind], "AnimCurveNode")).S("");
                Node& p70 = n.add("Properties70");
                for (int c = 0; c < 3; ++c) P(p70, axes[c], "Number", "", "A").D(ch[kind * 3 + c].front());
                oo(cn, layer);
                op(cn, boneId[b], props[kind]);
                for (int c = 0; c < 3; ++c) {
                    const auto& v = ch[kind * 3 + c];
                    std::vector<int64_t> kt;
                    std::vector<float> kv;
                    reduceKeys(v, times, tol, kt, kv);
                    int64_t cv = newId();
                    Node& curve = object("AnimationCurve").L(cv).S(objName("", "AnimCurve")).S("");
                    curve.add("Default").D(v.front());
                    curve.add("KeyVer").I(4009);
                    curve.add("KeyTime").al(kt);
                    curve.add("KeyValueFloat").af(kv);
                    // Linear interpolation, as Blender's exporter writes it.
                    curve.add("KeyAttrFlags").ai({24836});
                    float extra;
                    uint32_t bits = 218434821u;
                    std::memcpy(&extra, &bits, 4);
                    curve.add("KeyAttrDataFloat").af({0, 0, extra, 0});
                    curve.add("KeyAttrRefCount").ai({int32_t(kt.size())});
                    op(cv, cn, axes[c]);
                }
            }
        }
        if (!matched) rep.notes.push_back("animation " + na.name + " matches none of the model's bones");
        ++rep.animations;
    }

    // ---- file ----
    Bytes out;
    const char magic[] = "Kaydara FBX Binary  ";
    out.insert(out.end(), magic, magic + 20);
    out.insert(out.end(), {0x00, 0x1a, 0x00});
    le32(out, 7400);

    {
        Node h("FBXHeaderExtension");
        h.add("FBXHeaderVersion").I(1003);
        h.add("FBXVersion").I(7400);
        h.add("EncryptionType").I(0);
        Node& ts = h.add("CreationTimeStamp");
        ts.add("Version").I(1000);
        static const std::pair<const char*, int> stamp[] = {{"Year", 1970}, {"Month", 1}, {"Day", 1}, {"Hour", 10}, {"Minute", 0}, {"Second", 0}, {"Millisecond", 0}};
        for (auto& [k, v] : stamp) ts.add(k).I(v);
        h.add("Creator").S("NutTool");
        serialize(out, h);
    }
    // The file id and creation time pair with the footer below (as Blender writes them).
    {
        static const uint8_t fileId[16] = {0x28, 0xb3, 0x2a, 0xeb, 0xb6, 0x24, 0xcc, 0xc2, 0xbf, 0xc8, 0xb0, 0x2a, 0xa9, 0x2b, 0xfc, 0xf1};
        Node f("FileId");
        f.R(Bytes(fileId, fileId + 16));
        serialize(out, f);
        Node t("CreationTime");
        t.S("1970-01-01 10:00:00:000");
        serialize(out, t);
        Node c("Creator");
        c.S("NutTool");
        serialize(out, c);
    }
    {
        Node gs("GlobalSettings");
        gs.add("Version").I(1000);
        Node& p = gs.add("Properties70");
        pInt(p, "UpAxis", 1);
        pInt(p, "UpAxisSign", 1);
        pInt(p, "FrontAxis", 2);
        pInt(p, "FrontAxisSign", 1);
        pInt(p, "CoordAxis", 0);
        pInt(p, "CoordAxisSign", 1);
        pInt(p, "OriginalUpAxis", 1);
        pInt(p, "OriginalUpAxisSign", 1);
        pDouble(p, "UnitScaleFactor", opt.unitScale);
        pDouble(p, "OriginalUnitScaleFactor", opt.unitScale);
        pColour(p, "AmbientColor", 0, 0, 0);
        pString(p, "DefaultCamera", "Producer Perspective");
        pEnum(p, "TimeMode", 6); // 30 frames a second
        pTime(p, "TimeSpanStart", 0);
        pTime(p, "TimeSpanStop", ktime(longest > 0 ? longest : 1));
        pDouble(p, "CustomFrameRate", fps);
        serialize(out, gs);
    }
    {
        Node d("Documents");
        d.add("Count").I(1);
        int64_t docId = newId();
        Node& doc = d.add("Document").L(docId).S("").S("Scene");
        Node& p = doc.add("Properties70");
        P(p, "SourceObject", "object", "", "");
        pString(p, "ActiveAnimStackName", firstTake);
        doc.add("RootNode").L(0);
        serialize(out, d);
    }
    {
        Node r("References");
        serialize(out, r);
    }
    {
        Node d("Definitions");
        d.add("Version").I(100);
        int total = 1;
        for (auto& [k, v] : objectCounts) total += v;
        d.add("Count").I(total);
        d.add("ObjectType").S("GlobalSettings").add("Count").I(1);
        for (auto& [k, v] : objectCounts) d.add("ObjectType").S(k).add("Count").I(v);
        serialize(out, d);
    }
    serialize(out, objects);
    serialize(out, connections);
    {
        Node t("Takes");
        t.add("Current").S(firstTake);
        for (auto& na : opt.animations) {
            if (!na.anim) continue;
            Node& take = t.add("Take").S(na.name);
            take.add("FileName").S(na.name + ".tak");
            int64_t stop = ktime(std::max(0.0f, na.anim->duration));
            take.add("LocalTime").L(0).L(stop);
            take.add("ReferenceTime").L(0).L(stop);
        }
        serialize(out, t);
    }
    out.insert(out.end(), 13, 0); // end of the top-level records

    // Footer.
    static const uint8_t footId[16] = {0xfa, 0xbc, 0xab, 0x09, 0xd0, 0xc8, 0xd4, 0x66, 0xb1, 0x76, 0xfb, 0x83, 0x1c, 0xf7, 0x26, 0x7e};
    out.insert(out.end(), footId, footId + 16);
    out.insert(out.end(), 4, 0);
    size_t pad = ((out.size() + 15) & ~size_t(15)) - out.size();
    out.insert(out.end(), pad ? pad : 16, 0);
    le32(out, 7400);
    out.insert(out.end(), 120, 0);
    static const uint8_t footMagic[16] = {0xf8, 0x5a, 0x8c, 0x6a, 0xde, 0xf5, 0xd9, 0x7e, 0xec, 0xe9, 0x0c, 0xe3, 0x75, 0x8f, 0x29, 0x0b};
    out.insert(out.end(), footMagic, footMagic + 16);

    if (report) *report = std::move(rep);
    return out;
}

} // namespace nut::fbx
