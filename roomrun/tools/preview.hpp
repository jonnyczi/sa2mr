// A small software renderer for course previews (roomrun-gen --ppm): the play mesh, its grind rails and
// objects, the floor and the user's free circle, from a standing user's eye at a room corner and from the
// side, z-buffered with flat shading. Our own art only (the course's colour groups); PPM out (convert to PNG).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "roomrun/metrics.hpp"
#include "roomrun/objects.hpp"
#include "roomrun/playmesh.hpp"

namespace rrpreview {

using rr::Vec3;

struct Image {
    int w = 0, h = 0;
    std::vector<float> rgb, z;
    void init(int w_, int h_) { w = w_; h = h_; rgb.assign((size_t)w * h * 3, 1.0f); z.assign((size_t)w * h, 1e30f); }
};

struct Cam {   // a pinhole camera in the play mesh's frame (Y up)
    Vec3 eye, fwd, right, up;
    double f = 1;     // focal length in pixels
    double px = 0, py = 0;   // the principal point's offset from the viewport centre (pixels)
    int ox = 0, oy = 0, vw = 0, vh = 0;   // its viewport in the image
    bool ortho = false;
    double orthoScale = 1;   // pixels per metre
    static Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
    static Vec3 norm(Vec3 a) { double l = a.len(); return l > 1e-12 ? a * (1.0 / l) : a; }
    static double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    void lookAt(Vec3 e, Vec3 at) {
        eye = e;
        fwd = norm(at - e);
        right = norm(cross(fwd, Vec3{0, 1, 0}));
        up = cross(right, fwd);
    }
    bool project(Vec3 p, double& sx, double& sy, double& depth) const {
        Vec3 d = p - eye;
        double zc = dot(d, fwd);
        if (!ortho && zc < 0.05) return false;
        double xc = dot(d, right), yc = dot(d, up);
        if (ortho) { sx = ox + vw * 0.5 + xc * orthoScale; sy = oy + vh * 0.5 - yc * orthoScale; }
        else { sx = ox + vw * 0.5 + px + f * xc / zc; sy = oy + vh * 0.5 + py - f * yc / zc; }
        depth = zc;
        return true;
    }
};

inline void triangle(Image& im, const Cam& c, Vec3 a, Vec3 b, Vec3 cc, const float col[3], bool twoSided = true) {
    double ax, ay, az, bx, by, bz, cx, cy, cz;
    if (!c.project(a, ax, ay, az) || !c.project(b, bx, by, bz) || !c.project(cc, cx, cy, cz)) return;
    Vec3 n = Cam::norm(Cam::cross(b - a, cc - a));
    Vec3 view = Cam::norm(c.ortho ? c.fwd * -1.0 : c.eye - a);
    double facing = Cam::dot(n, view);
    if (facing < 0) { if (!twoSided) return; n = n * -1.0; }
    const Vec3 L = Cam::norm(Vec3{0.35, 0.85, 0.4});
    double shade = 0.45 + 0.55 * std::max(0.0, Cam::dot(n, L));
    int x0 = std::max(c.ox, (int)std::floor(std::min({ax, bx, cx}))), x1 = std::min(c.ox + c.vw - 1, (int)std::ceil(std::max({ax, bx, cx})));
    int y0 = std::max(c.oy, (int)std::floor(std::min({ay, by, cy}))), y1 = std::min(c.oy + c.vh - 1, (int)std::ceil(std::max({ay, by, cy})));
    double area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (std::fabs(area) < 1e-9) return;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            double px = x + 0.5, py = y + 0.5;
            double w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) / area;
            double w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) / area;
            double w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            double z = c.ortho ? w0 * az + w1 * bz + w2 * cz : 1.0 / (w0 / az + w1 / bz + w2 / cz);
            size_t i = (size_t)y * im.w + x;
            if (z >= im.z[i] - 1e-5) continue;
            im.z[i] = (float)z;
            for (int k = 0; k < 3; ++k) im.rgb[3 * i + k] = (float)std::min(1.0, col[k] * shade);
        }
}

inline void line(Image& im, const Cam& c, Vec3 a, Vec3 b, const float col[3], double width = 1.5) {
    double ax, ay, az, bx, by, bz;
    if (!c.project(a, ax, ay, az) || !c.project(b, bx, by, bz)) return;
    int n = (int)std::ceil(std::max(std::fabs(bx - ax), std::fabs(by - ay))) + 1;
    for (int k = 0; k <= n; ++k) {
        double t = (double)k / n, x = ax + (bx - ax) * t, y = ay + (by - ay) * t, z = az + (bz - az) * t;
        for (int dy = -(int)width / 2; dy <= (int)width / 2; ++dy)
            for (int dx = -(int)width / 2; dx <= (int)width / 2; ++dx) {
                int px = (int)x + dx, py = (int)y + dy;
                if (px < c.ox || py < c.oy || px >= c.ox + c.vw || py >= c.oy + c.vh) continue;
                size_t i = (size_t)py * im.w + px;
                if (z > im.z[i] + 0.02) continue;
                for (int q = 0; q < 3; ++q) im.rgb[3 * i + q] = col[q];
            }
    }
}

// 5x7 digits and a few letters for the labels
inline void text(Image& im, int x, int y, const std::string& s, int scale = 2) {
    static const char* glyphs[] = {
        "0111010001100111010110011100010111000000", "0010001100001000010000100001000111000000",
        "0111010001000010001000100010001111100000", "1111100010001000001000001100010111000000",
        "0001000110010101001011111000100001000000", "1111110000111100000100001100010111000000",
        "0011001000100001111010001100010111000000", "1111100001000100010001000010000100000000",
        "0111010001100010111010001100010111000000", "0111010001100010111100001000100110000000"};
    int cx = x;
    for (char ch : s) {
        if (ch >= '0' && ch <= '9') {
            const char* g = glyphs[ch - '0'];
            for (int r = 0; r < 8; ++r)
                for (int q = 0; q < 5; ++q)
                    if (g[r * 5 + q] == '1')
                        for (int sy = 0; sy < scale; ++sy)
                            for (int sx = 0; sx < scale; ++sx) {
                                int px = cx + q * scale + sx, py = y + r * scale + sy;
                                if (px >= 0 && py >= 0 && px < im.w && py < im.h)
                                    for (int k = 0; k < 3; ++k) im.rgb[3 * ((size_t)py * im.w + px) + k] = 0.1f;
                            }
        } else if (ch == '.') {
            for (int sy = 0; sy < scale; ++sy)
                for (int sx = 0; sx < scale; ++sx) {
                    int px = cx + 2 * scale + sx, py = y + 6 * scale + sy;
                    if (px >= 0 && py >= 0 && px < im.w && py < im.h)
                        for (int k = 0; k < 3; ++k) im.rgb[3 * ((size_t)py * im.w + px) + k] = 0.1f;
                }
        }
        cx += 6 * scale;
    }
}

inline void drawScene(Image& im, const Cam& c, const rr::GenResult& r, const rr::PlayMesh& pm, const rr::CourseObjects& co,
                      const rr::PlayGroup& grind) {
    // the floor and its 0.5 m grid
    const float floorCol[3] = {0.86f, 0.84f, 0.80f}, gridCol[3] = {0.70f, 0.68f, 0.64f}, wallCol[3] = {0.25f, 0.25f, 0.28f};
    const auto& F = r.room.floor;
    double minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
    for (auto& p : F) { minx = std::min(minx, p.x); maxx = std::max(maxx, p.x); miny = std::min(miny, p.y); maxy = std::max(maxy, p.y); }
    for (size_t i = 1; i + 1 < F.size(); ++i)   // fan (fine for the convex rooms we preview; others get a little overdraw)
        triangle(im, c, Vec3{F[0].x, 0, -F[0].y}, Vec3{F[i].x, 0, -F[i].y}, Vec3{F[i + 1].x, 0, -F[i + 1].y}, floorCol);
    for (double x = std::ceil(minx * 2) / 2; x <= maxx; x += 0.5) line(im, c, Vec3{x, 0.001, -miny}, Vec3{x, 0.001, -maxy}, gridCol, 1);
    for (double y = std::ceil(miny * 2) / 2; y <= maxy; y += 0.5) line(im, c, Vec3{minx, 0.001, -y}, Vec3{maxx, 0.001, -y}, gridCol, 1);
    for (size_t i = 0; i < F.size(); ++i) {
        auto a = F[i], b = F[(i + 1) % F.size()];
        line(im, c, Vec3{a.x, 0.002, -a.y}, Vec3{b.x, 0.002, -b.y}, wallCol, 3);
        line(im, c, Vec3{a.x, 0, -a.y}, Vec3{a.x, 1.6, -a.y}, gridCol, 1);   // the 1.6 m reach marks at the corners
    }
    // furniture
    for (auto& b : r.room.boxes) {
        const float col[3] = {0.78f, 0.66f, 0.48f};
        auto cs = b.rect.corners();
        for (int k = 0; k < 4; ++k) {
            auto p = cs[k], q = cs[(k + 1) % 4];
            triangle(im, c, Vec3{p.x, 0, -p.y}, Vec3{q.x, 0, -q.y}, Vec3{q.x, b.height, -q.y}, col);
            triangle(im, c, Vec3{p.x, 0, -p.y}, Vec3{q.x, b.height, -q.y}, Vec3{p.x, b.height, -p.y}, col);
        }
        triangle(im, c, Vec3{cs[0].x, b.height, -cs[0].y}, Vec3{cs[1].x, b.height, -cs[1].y}, Vec3{cs[2].x, b.height, -cs[2].y}, col);
        triangle(im, c, Vec3{cs[0].x, b.height, -cs[0].y}, Vec3{cs[2].x, b.height, -cs[2].y}, Vec3{cs[3].x, b.height, -cs[3].y}, col);
    }
    // the user: the free circle on the floor and a 1.7 m pole
    {
        const float red[3] = {0.85f, 0.15f, 0.2f};
        const auto u = r.room.player;
        const double R = r.room.playerRadius;
        for (int k = 0; k < 48; ++k) {
            double a0 = 2 * rr::kPi * k / 48, a1 = 2 * rr::kPi * (k + 1) / 48;
            line(im, c, Vec3{u.x + R * std::cos(a0), 0.003, -(u.y + R * std::sin(a0))}, Vec3{u.x + R * std::cos(a1), 0.003, -(u.y + R * std::sin(a1))}, red, 2);
        }
        line(im, c, Vec3{u.x, 0, -u.y}, Vec3{u.x, 1.7, -u.y}, red, 3);
    }
    for (auto& g : pm.groups)
        for (size_t t = 0; t < g.triangles(); ++t) {
            const float* v = &g.pos[9 * t];
            triangle(im, c, Vec3{v[0], v[1], v[2]}, Vec3{v[3], v[4], v[5]}, Vec3{v[6], v[7], v[8]}, g.rgb);
        }
    for (size_t t = 0; t < grind.triangles(); ++t) {
        const float* v = &grind.pos[9 * t];
        triangle(im, c, Vec3{v[0], v[1], v[2]}, Vec3{v[3], v[4], v[5]}, Vec3{v[6], v[7], v[8]}, grind.rgb);
    }
    // objects: springs red, dash panels cyan, ramps orange, Beetles black; the spring flights as dotted arcs
    const double m = pm.mpu;
    for (auto& o : co.objs) {
        float col[3] = {1, 1, 1};
        double s = 3;
        switch (o.kind) {
            case rr::ObjKind::Spring: col[0] = 0.95f; col[1] = 0.1f; col[2] = 0.1f; s = 4; break;
            case rr::ObjKind::DashPanel: col[0] = 0.1f; col[1] = 0.85f; col[2] = 1.0f; break;
            case rr::ObjKind::DashRamp: col[0] = 1.0f; col[1] = 0.55f; col[2] = 0.1f; s = 5; break;
            case rr::ObjKind::Beetle: case rr::ObjKind::Hunter: col[0] = 0.15f; col[1] = 0.15f; col[2] = 0.2f; s = 4; break;
            default: col[0] = 1.0f; col[1] = 0.85f; col[2] = 0.1f; s = 1.6; break;
        }
        const Vec3 p = o.p + Vec3{0, s * m, 0};
        const double h = s * m;
        Vec3 q[8];
        for (int k = 0; k < 8; ++k) q[k] = p + Vec3{(k & 1 ? h : -h), (k & 2 ? h : -h), (k & 4 ? h : -h)};
        const int f[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
        for (auto& fc : f) { triangle(im, c, q[fc[0]], q[fc[1]], q[fc[2]], col); triangle(im, c, q[fc[0]], q[fc[2]], q[fc[3]], col); }
        if (o.kind == rr::ObjKind::Spring || o.kind == rr::ObjKind::DashRamp) {
            rr::ObjectParams op;
            const bool ramp = o.kind == rr::ObjKind::DashRamp;
            const Vec3 from{o.p.x, o.p.y + (ramp ? 0 : op.springLift * m), o.p.z};
            auto fc = rr::checkFlight(pm, from, o.launchU, o.station, 200, op, true, ramp ? op.rampHoldFrames : 0, ramp ? op.rampDrag : -1);
            for (size_t k = 0; k + 1 < fc.path.size(); k += 2) line(im, c, fc.path[k], fc.path[k + 1], col, 2);
        }
    }
}

inline bool writePpm(const Image& im, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", im.w, im.h);
    std::vector<unsigned char> row((size_t)im.w * 3);
    for (int y = 0; y < im.h; ++y) {
        for (int x = 0; x < im.w * 3; ++x) row[x] = (unsigned char)std::lround(std::clamp(im.rgb[(size_t)y * im.w * 3 + x], 0.0f, 1.0f) * 255);
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    return true;
}

// Two views: a standing user's eye (1.7 m) at the room's corner beyond the course, looking at its middle; and
// a side elevation (orthographic, along the room's longer axis) with a 0.2 m height scale.
inline bool renderPreview(const rr::GenResult& r, const rr::PlayMesh& pm, const rr::CourseObjects& co, const std::string& path) {
    Image im;
    const int W = 1400, H = 760;
    im.init(W, H);
    const rr::PlayGroup grind = rr::grindRailGroup(co, pm.mpu);
    double minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
    for (auto& p : r.room.floor) { minx = std::min(minx, p.x); maxx = std::max(maxx, p.x); miny = std::min(miny, p.y); maxy = std::max(maxy, p.y); }
    const double cx = (minx + maxx) / 2, cy = (miny + maxy) / 2, span = std::max(maxx - minx, maxy - miny);
    Cam a;
    a.ox = 0; a.oy = 0; a.vw = 860; a.vh = H;
    // a standing user's view from beyond a corner, fitted to the floor and the course (and the 1.6 m reach marks)
    a.lookAt(Vec3{minx - 0.45 * span, 1.0 + 0.55 * span, -(miny - 0.55 * span)}, Vec3{cx, 0.6, -cy});
    {
        std::vector<Vec3> pts;
        for (auto& p : r.room.floor) { pts.push_back(Vec3{p.x, 0, -p.y}); pts.push_back(Vec3{p.x, 1.6, -p.y}); }
        for (auto& s : pm.st) pts.push_back(s.p);
        double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
        a.f = 1;
        for (auto& p : pts) {
            double sx, sy, d;
            if (!a.project(p, sx, sy, d)) continue;
            sx -= a.vw * 0.5; sy -= a.vh * 0.5;
            x0 = std::min(x0, sx); x1 = std::max(x1, sx); y0 = std::min(y0, sy); y1 = std::max(y1, sy);
        }
        a.f = std::min(a.vw * 0.92 / std::max(1e-6, x1 - x0), a.vh * 0.92 / std::max(1e-6, y1 - y0));
        a.px = -a.f * (x0 + x1) / 2;
        a.py = -a.f * (y0 + y1) / 2;
    }
    drawScene(im, a, r, pm, co, grind);
    Cam b;
    b.ortho = true;
    b.ox = 860; b.oy = 0; b.vw = W - 860; b.vh = H;
    b.orthoScale = std::min((W - 860 - 60) / span, (H - 80) / 2.0);
    const bool alongX = (maxx - minx) >= (maxy - miny);
    b.eye = Vec3{cx, 0.9, -cy};
    if (alongX) { b.fwd = Vec3{0, 0, -1}; b.right = Vec3{1, 0, 0}; } else { b.fwd = Vec3{1, 0, 0}; b.right = Vec3{0, 0, 1}; }
    b.up = Vec3{0, 1, 0};
    b.eye = b.eye - b.fwd * 20.0;
    drawScene(im, b, r, pm, co, grind);
    // height scale on the side view: ticks every 0.2 m to 1.6 m
    const float tick[3] = {0.3f, 0.3f, 0.35f};
    for (int k = 0; k <= 8; ++k) {
        double y = 0.2 * k, sx, sy, d;
        Vec3 p = b.eye + b.fwd * 20.0 - b.right * (span * 0.5 + 0.05) + Vec3{0, y - 0.9, 0};
        b.project(p, sx, sy, d);
        for (int x = (int)sx - 10; x < (int)sx; ++x)
            for (int t = -1; t <= 0; ++t)
                if (x >= 0 && x < W && sy + t >= 0 && sy + t < H)
                    for (int q = 0; q < 3; ++q) im.rgb[3 * ((size_t)(sy + t) * W + x) + q] = tick[q];
        char buf[16];
        std::snprintf(buf, sizeof buf, "%.1f", y);
        text(im, (int)sx - 40, (int)sy - 7, buf, 2);
    }
    for (int y = 0; y < H; ++y) for (int q = 0; q < 3; ++q) im.rgb[3 * ((size_t)y * W + 860) + q] = 0.4f;
    return writePpm(im, path);
}

}  // namespace rrpreview
