#include "roomrun/geom.hpp"

#include <algorithm>

namespace rr {

double signedArea(const Polygon& p) {
    double a = 0;
    for (size_t i = 0, n = p.size(); i < n; ++i) a += p[i].cross(p[(i + 1) % n]);
    return 0.5 * a;
}

static int orient(Vec2 a, Vec2 b, Vec2 c) {
    double v = (b - a).cross(c - a);
    if (std::fabs(v) < 1e-12) return 0;
    return v > 0 ? 1 : -1;
}
static bool onSeg(Vec2 a, Vec2 b, Vec2 p) {
    return std::min(a.x, b.x) - 1e-12 <= p.x && p.x <= std::max(a.x, b.x) + 1e-12 &&
           std::min(a.y, b.y) - 1e-12 <= p.y && p.y <= std::max(a.y, b.y) + 1e-12;
}

bool segmentsIntersect(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    int o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
    if (o1 != o2 && o3 != o4) return true;
    if (o1 == 0 && onSeg(a, b, c)) return true;
    if (o2 == 0 && onSeg(a, b, d)) return true;
    if (o3 == 0 && onSeg(c, d, a)) return true;
    if (o4 == 0 && onSeg(c, d, b)) return true;
    return false;
}

bool isSimplePolygon(const Polygon& p) {
    size_t n = p.size();
    if (n < 3) return false;
    if (std::fabs(signedArea(p)) < 1e-6) return false;
    for (size_t i = 0; i < n; ++i) {
        if ((p[i] - p[(i + 1) % n]).len() < 1e-9) return false;  // duplicate vertex
        for (size_t j = i + 1; j < n; ++j) {
            // skip adjacent edges
            if (j == i + 1 || (i == 0 && j == n - 1)) continue;
            if (segmentsIntersect(p[i], p[(i + 1) % n], p[j], p[(j + 1) % n])) return false;
        }
    }
    return true;
}

bool pointInPolygon(const Polygon& p, Vec2 q) {
    bool in = false;
    for (size_t i = 0, n = p.size(), j = n - 1; i < n; j = i++) {
        if (((p[i].y > q.y) != (p[j].y > q.y)) &&
            (q.x < (p[j].x - p[i].x) * (q.y - p[i].y) / (p[j].y - p[i].y) + p[i].x))
            in = !in;
    }
    return in;
}

double distPointSegment(Vec2 p, Vec2 a, Vec2 b) {
    Vec2 ab = b - a;
    double l2 = ab.len2();
    double t = l2 > 0 ? std::clamp((p - a).dot(ab) / l2, 0.0, 1.0) : 0.0;
    return (p - (a + ab * t)).len();
}

double distToPolygonBoundary(const Polygon& p, Vec2 q) {
    double d = 1e30;
    for (size_t i = 0, n = p.size(); i < n; ++i) d = std::min(d, distPointSegment(q, p[i], p[(i + 1) % n]));
    return d;
}

double signedDistPolygon(const Polygon& p, Vec2 q) {
    double d = distToPolygonBoundary(p, q);
    return pointInPolygon(p, q) ? d : -d;
}

double polylineLength(const std::vector<Vec2>& pts, bool closed) {
    double L = 0;
    size_t n = pts.size();
    for (size_t i = 0; i + 1 < n; ++i) L += (pts[i + 1] - pts[i]).len();
    if (closed && n > 1) L += (pts[0] - pts[n - 1]).len();
    return L;
}

std::vector<Vec2> resampleClosed(const std::vector<Vec2>& pts, double step) {
    size_t n = pts.size();
    std::vector<Vec2> out;
    if (n < 3) return out;
    double L = polylineLength(pts, true);
    int N = std::max(8, (int)std::round(L / step));
    double ds = L / N;
    out.reserve(N);
    size_t seg = 0;
    double segStart = 0, segLen = (pts[1] - pts[0]).len();
    for (int k = 0; k < N; ++k) {
        double s = k * ds;
        while (segStart + segLen < s && seg + 1 < n) {
            segStart += segLen;
            ++seg;
            segLen = (pts[(seg + 1) % n] - pts[seg]).len();
        }
        double t = segLen > 0 ? (s - segStart) / segLen : 0;
        t = std::clamp(t, 0.0, 1.0);
        out.push_back(pts[seg] + (pts[(seg + 1) % n] - pts[seg]) * t);
    }
    return out;
}

double ORect::dist(Vec2 p) const {
    Vec2 l = toLocal(p);
    double dx = std::max(std::fabs(l.x) - half.x, 0.0);
    double dy = std::max(std::fabs(l.y) - half.y, 0.0);
    return std::sqrt(dx * dx + dy * dy);
}
bool ORect::contains(Vec2 p, double margin) const {
    Vec2 l = toLocal(p);
    return std::fabs(l.x) <= half.x + margin && std::fabs(l.y) <= half.y + margin;
}
std::vector<Vec2> ORect::corners() const {
    double cs = std::cos(yaw), sn = std::sin(yaw);
    std::vector<Vec2> out;
    const double sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
    for (int i = 0; i < 4; ++i) {
        double lx = sx[i] * half.x, ly = sy[i] * half.y;
        out.push_back({c.x + lx * cs - ly * sn, c.y + lx * sn + ly * cs});
    }
    return out;
}

}  // namespace rr
