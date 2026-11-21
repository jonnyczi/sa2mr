// Closed skeleton splines fitted to the room: inset perimeter, figure-8, helix, circle.
#pragma once
#include <string>
#include <vector>

#include "roomrun/field.hpp"
#include "roomrun/geom.hpp"
#include "roomrun/params.hpp"
#include "roomrun/rng.hpp"
#include "roomrun/room.hpp"

namespace rr {

struct Skeleton {
    std::string kind;                        // perimeter | figure8 | helix | circle
    std::vector<Vec2> pts;                   // stations, uniform spacing stepM, closed loop
    double stepM = 0.04;
    std::vector<double> curv;                // signed curvature per station (1/unit)
    std::vector<double> target;              // optional preset target heights (m)
    std::vector<std::vector<int>> partners;  // stations overlapping in plan view (stacked layers)
    bool aroundPlayer = false;
    double prelim = 0;
    int size() const { return (int)pts.size(); }
    double lengthM() const { return pts.size() * stepM; }
};

// Track-centerline clearance requirement (m) from obstacles.
double centerlineClearance(const GenParams& gp);

std::vector<Skeleton> skeletonCandidates(const Room& room, const FreeSpace& fs, const GenParams& gp, Rng& rng);
// Simplest safe loop (no self-overlap): longest perimeter contour, else a circle in the biggest free disk.
bool fallbackSkeleton(const Room& room, const FreeSpace& fs, const GenParams& gp, Skeleton& out);

// Resample/smooth/orient a dense closed polyline into stations; false if it violates clearance.
bool finalizeSkeleton(Skeleton& sk, const std::vector<Vec2>& dense, const FreeSpace& fs, const GenParams& gp,
                      Rng* rng, bool smooth);

}  // namespace rr
