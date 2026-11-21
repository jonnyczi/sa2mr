#include "roomrun/room.hpp"

#include <algorithm>
#include <cmath>

namespace rr {

static Vec2 vec2Of(const Json& j, Vec2 def = {}) {
    if (!j.isArray() || j.size() < 2) return def;
    return {j[0].num, j[1].num};
}

Room roomFromJson(const Json& j) {
    Room r;
    for (auto& p : j["floor"].arr) r.floor.push_back(vec2Of(p));
    for (auto& b : j["boxes"].arr) {
        Box box;
        box.rect.c = vec2Of(b["center"]);
        Vec2 sz = vec2Of(b["size"], {0.5, 0.5});
        box.rect.half = sz * 0.5;
        box.rect.yaw = b.numOr("yaw", 0.0) * kPi / 180.0;
        box.height = b.numOr("height", 0.5);
        box.label = b.strOr("label", "box");
        r.boxes.push_back(box);
    }
    const Json& pl = j["player"];
    r.player = vec2Of(pl["pos"]);
    r.playerRadius = pl.numOr("radius", 0.7);
    normalizeRoom(r);
    return r;
}

Json roomToJson(const Room& r) {
    Json j = Json::object();
    Json fl = Json::array();
    for (auto& p : r.floor) { Json v = Json::array(); v.push(p.x); v.push(p.y); fl.push(v); }
    j.set("floor", fl);
    Json bx = Json::array();
    for (auto& b : r.boxes) {
        Json o = Json::object();
        Json c = Json::array(); c.push(b.rect.c.x); c.push(b.rect.c.y);
        Json s = Json::array(); s.push(b.rect.half.x * 2); s.push(b.rect.half.y * 2);
        o.set("center", c); o.set("size", s);
        o.set("yaw", b.rect.yaw * 180.0 / kPi);
        o.set("height", b.height);
        o.set("label", b.label);
        bx.push(o);
    }
    j.set("boxes", bx);
    Json pl = Json::object();
    Json pp = Json::array(); pp.push(r.player.x); pp.push(r.player.y);
    pl.set("pos", pp); pl.set("radius", r.playerRadius);
    j.set("player", pl);
    return j;
}

void normalizeRoom(Room& r) {
    if (signedArea(r.floor) < 0) std::reverse(r.floor.begin(), r.floor.end());
}

bool validateRoom(const Room& r, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (r.floor.size() < 3) return fail("floor polygon needs >= 3 vertices");
    if (!isSimplePolygon(r.floor)) return fail("floor polygon is not simple (self-intersecting or degenerate)");
    if (std::fabs(signedArea(r.floor)) < 1.0) return fail("floor area < 1 m^2");
    if (!pointInPolygon(r.floor, r.player)) return fail("player position outside floor");
    if (r.playerRadius < 0.1 || r.playerRadius > 2.0) return fail("player radius out of range");
    for (auto& b : r.boxes) {
        if (b.height <= 0 || b.height > 3.0) return fail("box height out of range");
        if (b.rect.half.x <= 0 || b.rect.half.y <= 0) return fail("box size must be positive");
    }
    return true;
}

static double snap(double v, double q) { return std::round(v / q) * q; }

Room quantizeRoom(const Room& in, double q) {
    Room r = in;
    for (auto& p : r.floor) { p.x = snap(p.x, q); p.y = snap(p.y, q); }
    // remove duplicates created by snapping
    Polygon f;
    for (auto& p : r.floor)
        if (f.empty() || (p - f.back()).len() > 1e-9) f.push_back(p);
    while (f.size() > 1 && (f.front() - f.back()).len() < 1e-9) f.pop_back();
    r.floor = f;
    for (auto& b : r.boxes) {
        b.rect.c = {snap(b.rect.c.x, q), snap(b.rect.c.y, q)};
        b.rect.half = {std::max(q / 2, snap(b.rect.half.x * 2, q) / 2), std::max(q / 2, snap(b.rect.half.y * 2, q) / 2)};
        b.rect.yaw = snap(b.rect.yaw, kPi / 36.0);  // 5 degrees
        b.height = std::max(q, snap(b.height, q));
    }
    r.player = {snap(r.player.x, q), snap(r.player.y, q)};
    r.playerRadius = snap(r.playerRadius, q);
    return r;
}

static int64_t qi(double v) { return (int64_t)std::llround(v * 1000.0); }

uint64_t roomHash(const Room& r) {
    Hasher h;
    h.i64((int64_t)r.floor.size());
    for (auto& p : r.floor) { h.i64(qi(p.x)); h.i64(qi(p.y)); }
    h.i64((int64_t)r.boxes.size());
    for (auto& b : r.boxes) {
        h.i64(qi(b.rect.c.x)); h.i64(qi(b.rect.c.y)); h.i64(qi(b.rect.half.x)); h.i64(qi(b.rect.half.y));
        h.i64(qi(b.rect.yaw)); h.i64(qi(b.height));
    }
    h.i64(qi(r.player.x)); h.i64(qi(r.player.y)); h.i64(qi(r.playerRadius));
    return h.value();
}

// ---------------------------------------------------------------- random rooms

const char* roomTypeName(RoomType t) {
    switch (t) {
        case RoomType::Rectangle: return "rectangle";
        case RoomType::LShape: return "l-shape";
        case RoomType::Hallway: return "hallway";
        case RoomType::Irregular: return "irregular";
    }
    return "?";
}

static Polygon randomFloor(Rng& rng, RoomType type) {
    Polygon p;
    switch (type) {
        case RoomType::Rectangle: {
            double w = rng.range(1.5, 6.0), h = rng.range(1.5, 6.0);
            p = {{0, 0}, {w, 0}, {w, h}, {0, h}};
            break;
        }
        case RoomType::LShape: {
            double w = rng.range(2.5, 6.0), h = rng.range(2.5, 6.0);
            double cw = w * rng.range(0.3, 0.6), ch = h * rng.range(0.3, 0.6);
            p = {{0, 0}, {w, 0}, {w, h - ch}, {w - cw, h - ch}, {w - cw, h}, {0, h}};
            break;
        }
        case RoomType::Hallway: {
            double w = rng.range(1.5, 2.2), h = rng.range(3.5, 6.0);
            if (rng.chance(0.5)) p = {{0, 0}, {h, 0}, {h, w}, {0, w}};
            else p = {{0, 0}, {w, 0}, {w, h}, {0, h}};
            break;
        }
        case RoomType::Irregular: {
            // star-shaped polygon around a center: always simple.
            int n = rng.irange(5, 9);
            double R = rng.range(1.2, 3.0);
            double a0 = rng.range(0, 2 * kPi);
            for (int i = 0; i < n; ++i) {
                double a = a0 + 2 * kPi * (i + rng.range(-0.25, 0.25)) / n;
                double rad = R * rng.range(0.65, 1.0);
                p.push_back({3.0 + rad * std::cos(a), 3.0 + rad * std::sin(a)});
            }
            break;
        }
    }
    return p;
}

Room randomRoom(Rng& rng, RoomType type) {
    struct Furniture { const char* label; double w0, w1, d0, d1, h; };
    static const Furniture kinds[] = {
        {"couch", 1.4, 2.2, 0.8, 1.0, 0.45}, {"table", 0.8, 1.6, 0.6, 1.0, 0.75},
        {"bed", 1.4, 2.0, 1.0, 1.8, 0.55},   {"shelf", 0.6, 1.2, 0.3, 0.45, 1.8},
        {"counter", 1.0, 2.0, 0.5, 0.7, 0.9}, {"ottoman", 0.4, 0.7, 0.4, 0.7, 0.40},
        {"desk", 1.0, 1.5, 0.5, 0.75, 0.74},
    };
    Room r;
    for (int attempt = 0; attempt < 50; ++attempt) {
        r.floor = randomFloor(rng, type);
        if (isSimplePolygon(r.floor) && std::fabs(signedArea(r.floor)) >= 2.0) break;
    }
    normalizeRoom(r);
    double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    for (auto& p : r.floor) { minx = std::min(minx, p.x); miny = std::min(miny, p.y); maxx = std::max(maxx, p.x); maxy = std::max(maxy, p.y); }
    auto randomInside = [&]() {
        for (int t = 0; t < 200; ++t) {
            Vec2 q{rng.range(minx, maxx), rng.range(miny, maxy)};
            if (pointInPolygon(r.floor, q)) return q;
        }
        return r.floor[0];
    };
    int nb = rng.irange(0, 4);
    for (int i = 0; i < nb; ++i) {
        const Furniture& f = kinds[rng.irange(0, 6)];
        for (int t = 0; t < 40; ++t) {
            Box b;
            b.label = f.label;
            b.height = f.h * rng.range(0.9, 1.1);
            b.rect.half = {rng.range(f.w0, f.w1) / 2, rng.range(f.d0, f.d1) / 2};
            b.rect.yaw = rng.chance(0.7) ? (rng.chance(0.5) ? 0.0 : kPi / 2) : rng.range(0, kPi);
            b.rect.c = randomInside();
            bool ok = true;
            for (auto& c : b.rect.corners()) if (!pointInPolygon(r.floor, c)) ok = false;
            for (auto& o : r.boxes)
                if ((o.rect.c - b.rect.c).len() < (o.rect.half.len() + b.rect.half.len()) * 0.8) ok = false;
            if (ok) { r.boxes.push_back(b); break; }
        }
    }
    // player: inside the floor, not on furniture, at least 0.3 m from walls.
    for (int t = 0; t < 500; ++t) {
        Vec2 q = randomInside();
        bool ok = distToPolygonBoundary(r.floor, q) > 0.3;
        for (auto& b : r.boxes) if (b.rect.dist(q) < 0.25) ok = false;
        if (ok) { r.player = q; break; }
        r.player = q;
    }
    r.playerRadius = 0.7;
    return r;
}

}  // namespace rr
