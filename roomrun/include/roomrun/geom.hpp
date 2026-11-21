// 2D/3D vector math and polygon utilities. All room geometry is in meters.
#pragma once
#include <cmath>
#include <vector>

namespace rr {

constexpr double kPi = 3.14159265358979323846;

struct Vec2 {
    double x = 0, y = 0;
    Vec2() = default;
    Vec2(double x_, double y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    Vec2 operator/(double s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    double dot(Vec2 o) const { return x * o.x + y * o.y; }
    double cross(Vec2 o) const { return x * o.y - y * o.x; }
    double len() const { return std::sqrt(x * x + y * y); }
    double len2() const { return x * x + y * y; }
    Vec2 norm() const { double l = len(); return l > 1e-12 ? Vec2{x / l, y / l} : Vec2{1, 0}; }
    Vec2 perp() const { return {-y, x}; }  // left normal
};

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    double len() const { return std::sqrt(x * x + y * y + z * z); }
};

using Polygon = std::vector<Vec2>;

double signedArea(const Polygon& p);                  // >0 for CCW
bool segmentsIntersect(Vec2 a, Vec2 b, Vec2 c, Vec2 d);  // proper or touching
bool isSimplePolygon(const Polygon& p);               // no self-intersections, >=3 verts, non-degenerate
bool pointInPolygon(const Polygon& p, Vec2 q);
double distPointSegment(Vec2 p, Vec2 a, Vec2 b);
double distToPolygonBoundary(const Polygon& p, Vec2 q);
double signedDistPolygon(const Polygon& p, Vec2 q);   // >0 inside
double polylineLength(const std::vector<Vec2>& pts, bool closed);

// Resample a closed polyline to N points with uniform arc-length spacing ~step.
std::vector<Vec2> resampleClosed(const std::vector<Vec2>& pts, double step);

// Oriented rectangle (furniture footprint).
struct ORect {
    Vec2 c;      // center
    Vec2 half;   // half extents
    double yaw;  // radians
    Vec2 toLocal(Vec2 p) const {
        Vec2 d = p - c;
        double cs = std::cos(yaw), sn = std::sin(yaw);
        return {d.x * cs + d.y * sn, -d.x * sn + d.y * cs};
    }
    double dist(Vec2 p) const;  // 0 inside
    bool contains(Vec2 p, double margin = 0) const;
    std::vector<Vec2> corners() const;
};

}  // namespace rr
