// Closed skeleton splines fitted to the room: inset perimeter, figure-8, helix, circle, coil.
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
    std::string kind;                        // perimeter | figure8 | helix | circle | coil
    std::vector<Vec2> pts;                   // stations, uniform spacing stepM, closed loop
    double stepM = 0.04;
    std::vector<double> curv;                // signed curvature per station (1/unit)
    std::vector<double> target;              // optional preset target heights (m)
    std::vector<std::vector<int>> partners;  // stations overlapping in plan view (stacked layers)
    bool aroundPlayer = false;
    bool solved = false;                     // target is a solved stacked profile (coils): followed as it is
    bool keepStart = false;                  // finalizeSkeleton keeps the dense path's first point as the start line
    double prelim = 0;
    int size() const { return (int)pts.size(); }
    double lengthM() const { return pts.size() * stepM; }
};

// Track-centerline clearance requirement (m) from obstacles.
double centerlineClearance(const GenParams& gp);

std::vector<Skeleton> skeletonCandidates(const Room& room, const FreeSpace& fs, const GenParams& gp, Rng& rng);
// Simplest safe loop (no self-overlap): longest perimeter contour, else a circle in the biggest free disk.
// The last resort when no candidate makes a good course: a plain loop (the free floor's contour, else a circle) of at
// least minLap metres.
bool fallbackSkeleton(const Room& room, const FreeSpace& fs, const GenParams& gp, Skeleton& out, double minLap = 1.5);

// Coils (coil.cpp): chains of circular lobes in free disks (the user's ring among them), each wound up to two extra
// times and joined by tangent lines (crossing where neighbours turn opposite ways), climbing on some lobes and
// descending on others, so the decks stack. Their heights are solved by solveStackedProfile. Appends the best few.
void coilCandidates(const Room& room, const FreeSpace& fs, const GenParams& gp, Rng& rng, std::vector<Skeleton>& out);

// Towers (tower.cpp): a loop from `bases` (perimeter, figure-8, circle) with one to three spiral towers hung off
// it: one or two full turns down beside the loop, back onto it a jump's height lower, the loop climbing back.
void towerCandidates(const Room& room, const FreeSpace& fs, const GenParams& gp, Rng& rng, const std::vector<Skeleton>& bases,
                     std::vector<Skeleton>& out);

// Heights for a skeleton whose decks overlap in plan (sk.partners): every overlapping pair at least
// layerGapU + stackMarginU apart (the one the guide puts higher stays higher), every slope within the walkable
// limits on the inside of its bends (times coilRiseSafety / coilFallSafety), inside the height window and above furniture, the
// first flatStations level (the start line). A system of difference constraints (Bellman-Ford): kept as near the
// guide as it allows. Returns false if none exists; else sets sk.target and sk.solved.
bool solveStackedProfile(Skeleton& sk, const Room& room, const GenParams& gp, const std::vector<double>& guide, int flatStations);

// A roller-coaster profile for a plain loop (perimeter, figure-8, circle): low and high plateaus (a figure-8's
// crossing on them, one pass high, one low), a climb shaped by each station's walkable slope and a steeper
// descent, as high as the climb and the window allow; solved like a coil's. False (sk unchanged) if it does not fit.
bool rollerProfile(Skeleton& sk, const Room& room, const GenParams& gp, Rng& rng);

// The share of a centreline slope that the inside edge of a bend of curvature k (1/unit) keeps walkable (the
// road's width on gentle bends, a rail strip's on tight ones), as the assembler and the play mesh measure it.
double walkBendFactor(const GenParams& gp, double k);

// Resample/smooth/orient a dense closed polyline into stations; false if it violates clearance.
bool finalizeSkeleton(Skeleton& sk, const std::vector<Vec2>& dense, const FreeSpace& fs, const GenParams& gp,
                      Rng* rng, bool smooth);

}  // namespace rr
