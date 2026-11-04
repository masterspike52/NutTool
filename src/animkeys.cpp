#include "animkeys.hpp"

#include "json.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>

namespace nut::model {
namespace {

using Quat = std::array<double, 4>; // x y z w
using Vec = std::array<double, 3>;

Quat qmul(const Quat& a, const Quat& b) {
    return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
            a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}
Quat qnorm(Quat q) {
    double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (n < 1e-12) return {0, 0, 0, 1};
    for (double& x : q) x /= n;
    return q;
}
// XYZ Euler degrees, X applied first: q = qz * qy * qx (FBX's default order, as fbx.cpp writes it).
Quat fromEuler(const Vec& deg) {
    constexpr double kRad = 3.14159265358979323846 / 180;
    Quat r = {0, 0, 0, 1};
    for (int k = 0; k < 3; ++k) {
        double h = deg[size_t(k)] * kRad / 2;
        Quat q = {0, 0, 0, std::cos(h)};
        q[size_t(k)] = std::sin(h);
        r = qmul(q, r);
    }
    return r;
}
// Normalised blend along the shorter way round.
Quat qblend(const Quat& a, Quat b, double u) {
    double dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (dot < 0)
        for (double& x : b) x = -x;
    Quat r;
    for (size_t k = 0; k < 4; ++k) r[k] = a[k] * (1 - u) + b[k] * u;
    return qnorm(r);
}

struct Point {
    double time = 0;
    bool base = false; // the animation's own value at `time`
    Quat rot{0, 0, 0, 1};
    Vec move{0, 0, 0};
};

struct Value {
    std::optional<Point> rot, move;
};
using Pose = std::map<int, Value>; // bone -> value

Vec vec3(const Json& j, const std::string& what) {
    if (j.type() != Json::Array || j.size() != 3) fail(what + ": expected three numbers");
    return {j[0].asNumber(), j[1].asNumber(), j[2].asNumber()};
}

} // namespace

std::vector<Key> keysFromJson(const Animation& a, const std::string& text, KeysReport* report) {
    KeysReport rep;
    const Json doc = Json::parse(text);
    if (doc.type() != Json::Object) fail("animation keys: the file must hold a JSON object");
    const uint32_t nb = a.bones;
    const size_t nf = size_t(a.frames);
    if (!nb || !nf || a.keys.size() != nf * nb) fail("animation " + a.name + " has no keys to start from");

    std::map<std::string, int> byName;
    for (size_t i = 0; i < a.boneNames.size(); ++i) byName.emplace(lower(a.boneNames[i]), int(i));
    auto boneOf = [&](const std::string& n) {
        auto it = byName.find(lower(n));
        if (it == byName.end()) fail("animation keys: " + a.name + " has no bone named " + n);
        return it->second;
    };
    auto readPose = [&](const Json& j, const std::string& where) {
        if (j.type() != Json::Object) fail("animation keys: " + where + " must be an object of bones");
        Pose pose;
        for (auto& [bone, v] : j.members()) {
            std::string w = where + " " + bone;
            if (v.type() != Json::Object) fail("animation keys: " + w + " must be an object with rot and/or move");
            Value val;
            if (v.has("rot")) {
                const Json& r = v["rot"];
                Point p;
                if (r.type() == Json::String && r.asString() == "base") p.base = true;
                else if (r.type() == Json::Array && r.size() == 4) p.rot = qnorm({r[0].asNumber(), r[1].asNumber(), r[2].asNumber(), r[3].asNumber()});
                else p.rot = fromEuler(vec3(r, w + " rot"));
                val.rot = p;
            }
            if (v.has("move")) {
                const Json& m = v["move"];
                Point p;
                if (m.type() == Json::String && m.asString() == "base") p.base = true;
                else p.move = vec3(m, w + " move");
                val.move = p;
            }
            pose[boneOf(bone)] = val;
        }
        return pose;
    };

    std::map<std::string, Pose> poses;
    if (doc.has("poses"))
        for (auto& [name, j] : doc["poses"].members()) poses[name] = readPose(j, "pose " + name);

    bool keep = true;
    if (doc.has("base")) {
        std::string b = doc["base"].asString();
        if (b != "keep" && b != "rest") fail("animation keys: base must be \"keep\" or \"rest\"");
        keep = b == "keep";
    }

    // Keys per bone, in timeline order (a later entry at the same time wins).
    std::map<int, std::vector<Point>> rotKeys, moveKeys;
    if (!doc.has("timeline") || doc["timeline"].type() != Json::Array) fail("animation keys: no timeline");
    const auto& timeline = doc["timeline"].items();
    for (size_t i = 0; i < timeline.size(); ++i) {
        const Json& e = timeline[i];
        std::string where = "timeline entry " + std::to_string(i + 1);
        if (!e.has("time")) fail("animation keys: " + where + " has no time");
        double time = e["time"].asNumber(), hold = e.has("hold") ? e["hold"].asNumber() : 0;
        Pose pose;
        auto merge = [&](const Pose& p) {
            for (auto& [b, v] : p) {
                if (v.rot) pose[b].rot = v.rot;
                if (v.move) pose[b].move = v.move;
            }
        };
        if (e.has("pose")) {
            const Json& pj = e["pose"];
            std::vector<std::string> names;
            if (pj.type() == Json::Array)
                for (auto& n : pj.items()) names.push_back(n.asString());
            else names.push_back(pj.asString());
            for (auto& n : names) {
                auto it = poses.find(n);
                if (it == poses.end()) fail("animation keys: " + where + " uses an unknown pose " + n);
                merge(it->second);
            }
        }
        if (e.has("bones")) merge(readPose(e["bones"], where));
        if (time > a.duration + 1e-3) rep.notes.push_back(where + " is after the animation's end (" + std::to_string(a.duration) + " s)");
        for (double t : {time, time + hold}) {
            for (auto& [b, v] : pose) {
                if (v.rot) {
                    Point p = *v.rot;
                    p.time = t;
                    rotKeys[b].push_back(p);
                }
                if (v.move) {
                    Point p = *v.move;
                    p.time = t;
                    moveKeys[b].push_back(p);
                }
            }
            if (hold <= 0) break;
        }
        ++rep.entries;
    }
    for (auto* keys : {&rotKeys, &moveKeys})
        for (auto& [b, k] : *keys) std::stable_sort(k.begin(), k.end(), [](const Point& x, const Point& y) { return x.time < y.time; });

    // The animation's own key of a bone at any time.
    auto original = [&](int b, double t) {
        size_t f1 = size_t(std::upper_bound(a.frameTimes.begin(), a.frameTimes.end(), float(t)) - a.frameTimes.begin());
        f1 = std::clamp<size_t>(f1, 1, std::max<size_t>(1, nf - 1));
        size_t f0 = f1 - 1;
        if (nf == 1) f0 = f1 = 0;
        double span = nf > 1 ? double(a.frameTimes[f1]) - a.frameTimes[f0] : 0;
        double u = span > 0 ? std::clamp((t - a.frameTimes[f0]) / span, 0.0, 1.0) : 0;
        const Key& k0 = a.keys[f0 * nb + size_t(b)];
        const Key& k1 = a.keys[f1 * nb + size_t(b)];
        Point p;
        p.rot = qblend({k0.rotation[0], k0.rotation[1], k0.rotation[2], k0.rotation[3]}, {k1.rotation[0], k1.rotation[1], k1.rotation[2], k1.rotation[3]}, u);
        for (size_t c = 0; c < 3; ++c) p.move[c] = k0.translation[c] * (1 - u) + k1.translation[c] * u;
        return p;
    };
    auto resolve = [&](const Point& p, int b) { return p.base ? original(b, p.time) : p; };
    // Eased between the keys around t; held before the first and after the last.
    auto sample = [&](const std::vector<Point>& k, int b, double t, bool rot) {
        size_t i = size_t(std::upper_bound(k.begin(), k.end(), t, [](double x, const Point& p) { return x < p.time; }) - k.begin());
        if (i == 0) return resolve(k.front(), b);
        if (i == k.size()) return resolve(k.back(), b);
        Point p0 = resolve(k[i - 1], b), p1 = resolve(k[i], b);
        double span = k[i].time - k[i - 1].time;
        double u = span > 1e-9 ? (t - k[i - 1].time) / span : 1;
        u = u * u * (3 - 2 * u);
        Point r;
        if (rot) r.rot = qblend(p0.rot, p1.rot, u);
        else
            for (size_t c = 0; c < 3; ++c) r.move[c] = p0.move[c] * (1 - u) + p1.move[c] * u;
        return r;
    };

    std::vector<Key> out(nf * nb);
    for (size_t f = 0; f < nf; ++f) {
        double t = f < a.frameTimes.size() ? a.frameTimes[f] : 0;
        for (uint32_t b = 0; b < nb; ++b) {
            Key k = keep ? a.keys[f * nb + b] : Key{};
            auto r = rotKeys.find(int(b));
            if (r != rotKeys.end()) {
                Quat q = sample(r->second, int(b), t, true).rot;
                for (size_t c = 0; c < 4; ++c) k.rotation[c] = float(q[c]);
            }
            auto m = moveKeys.find(int(b));
            if (m != moveKeys.end()) {
                Vec v = sample(m->second, int(b), t, false).move;
                for (size_t c = 0; c < 3; ++c) k.translation[c] = float(v[c]);
            }
            out[f * nb + b] = k;
        }
    }
    std::map<int, bool> keyed;
    for (auto& [b, k] : rotKeys) keyed[b] = true;
    for (auto& [b, k] : moveKeys) keyed[b] = true;
    rep.bones = keyed.size();
    if (report) *report = std::move(rep);
    return out;
}

} // namespace nut::model
