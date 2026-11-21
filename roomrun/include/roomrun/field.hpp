// Grid distance fields: free-space clearance, morphological opening (corner rounding),
// marching-squares contours (polygon inset) and a distance-field medial axis.
#pragma once
#include <cstdint>
#include <vector>

#include "roomrun/geom.hpp"
#include "roomrun/params.hpp"
#include "roomrun/room.hpp"

namespace rr {

struct Grid {
    double x0 = 0, y0 = 0, res = 0.025;
    int nx = 0, ny = 0;
    std::vector<float> v;
    void init(double x0_, double y0_, double res_, int nx_, int ny_, float fill = 0) {
        x0 = x0_; y0 = y0_; res = res_; nx = nx_; ny = ny_; v.assign((size_t)nx * ny, fill);
    }
    float& at(int i, int j) { return v[(size_t)j * nx + i]; }
    float at(int i, int j) const { return v[(size_t)j * nx + i]; }
    Vec2 pos(int i, int j) const { return {x0 + i * res, y0 + j * res}; }
    double sample(Vec2 p) const;  // bilinear, -1 outside
};

struct MedialPoint { Vec2 p; double r; };

struct FreeSpace {
    Grid clearance;                   // meters to nearest obstacle (walls-margin, player cylinder, tall boxes)
    std::vector<uint8_t> ridge;       // medial-axis mask over clearance grid
    std::vector<MedialPoint> maxima;  // medial vertices: big inscribed disks, sorted by r desc
};

FreeSpace buildFreeSpace(const Room& room, const GenParams& gp);

// Morphological opening of {F >= c} by radius r; returns field G with region {G >= 0}
// whose boundary has convex-corner radius >= r.
Grid openedField(const Grid& F, double c, double r);

// Closed contour loops of {g > level} (marching squares, saddles resolved by center value).
std::vector<std::vector<Vec2>> contourLoops(const Grid& g, double level);

// Exact squared Euclidean distance transform (Felzenszwalb) of a binary mask (1 = feature).
std::vector<float> edt(const std::vector<uint8_t>& mask, int nx, int ny, double res);

}  // namespace rr
