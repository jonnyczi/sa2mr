// Tunables. Lengths along the track are in SA2 game units ("u"); room geometry in meters.
#pragma once
#include <cmath>
#include <cstdint>
#include <string>

namespace rr {

constexpr const char* kGeneratorVersion = "roomrun-0.2.0";   // 0.2: coils (stacked lobes), 0.2-1.6 m, jump-high stacking

// Sonic-lite physics, in *unscaled* SA2-ish units per frame @60 fps.
struct Physics {
    double runAccel = 0.05;     // throttle acceleration below runMax
    double runMax = 5.0;        // speed reachable by holding forward on flat ground
    double topSpeed = 16.0;     // hard cap (slopes, dash panels)
    double dragAbove = 0.02;    // decay when above runMax on flat ground
    double coastDecel = 0.03;   // stick released
    double brakeDecel = 0.15;   // stick back
    double gravity = 0.08;      // u/f^2
    double jumpV = 1.66;        // initial vertical jump speed
    double slopeFactor = 1.0;   // fraction of gravity applied along slopes
    double grip = 0.30;         // max lateral (centripetal) accel before drifting
    double steerAccel = 0.10;   // lateral accel from full stick
    double latDamp = 0.80;      // lateral velocity damping per frame
    double wallScrape = 0.985;  // speed factor per frame of wall contact
    double sonicRadius = 3.0;   // collision half width
};

// Physics after applying the global speed scale k (time dilation: v*k, a*k^2).
// Time dilation keeps every arc shape identical, so module geometry certified at
// k=1 stays valid; only lap times change.
struct ScaledPhysics {
    Physics p;
    double k;
    ScaledPhysics(const Physics& base, double scale) : p(base), k(scale) {
        double k2 = k * k;
        p.runAccel *= k2; p.runMax *= k; p.topSpeed *= k; p.dragAbove *= k2;
        p.coastDecel *= k2; p.brakeDecel *= k2; p.gravity *= k2; p.jumpV *= k;
        p.grip *= k2; p.steerAccel *= k2;
        p.latDamp = std::pow(base.latDamp, k);  // per-frame damping under dilation
        p.wallScrape = std::pow(base.wallScrape, k);
    }
    double v(double unscaled) const { return unscaled * k; }
};

struct GenParams {
    // Scale: Sonic is sonicHeightM tall and ~10 units tall in SA2.
    double sonicHeightM = 0.10;
    double sonicHeightU = 10.0;
    double speedScale = 0.6;
    double metersPerUnit() const { return sonicHeightM / sonicHeightU; }

    double trackWidthU = 24.0;   // walkway width
    double stationU = 4.0;       // arc-length sampling of the skeleton
    double wallMarginM = 0.05;   // track edge to room wall
    double boxMarginM = 0.04;    // track edge to furniture side
    double tallBoxM = 1.30;      // boxes taller than this are obstacles, not landable
    // Track height window (m): decks from 0.2 m (a coil's lowest tier) to 1.6 m (within a standing user's reach);
    // plain loops and the fallback keep to prefMinH and up.
    double minH = 0.20, prefMinH = 0.40, maxH = 1.60;
    // Stacked decks (a deck over another, in plan): deck to deck at least layerGapU, enough for Sonic and his full jump
    // under the upper deck's underside. Measured in the game (host/xr/README "Room Run: no snapping onto the bridge",
    // the Frame's figure-8): a jump peaks 35 units above the ground, his collision top 10 units above his position
    // (he peaked at +94.6 under an underside at +104.7), and the play mesh's underside is 2 units below its deck:
    // 35 + 10 + 2 = 47, plus 1 of margin.
    double jumpApexU = 35.0, sonicTopU = 10.0, deckUndersideU = 2.0;
    double layerGapU = 48.0;     // >= jumpApexU + sonicTopU + deckUndersideU (the tests check it)
    double stackMarginU = 4.0;   // the coil profiles aim this much above layerGapU (modules and smoothing then have slack)
    double gridResM = 0.025;     // distance field resolution
    double railHalfWidthU = 7.0; // a rail strip's half width (as PlayMeshParams::railHalfWidthU): its walkable slopes

    int beamWidth = 10;
    int maxCandidates = 5;
    bool vertical = true;        // roller profiles, coils and towers (generateCourse turns them off for a library without
                                 // constant-slope ramps and rails to follow them: Chao Race)
    int coilTrials = 28;         // random lobe chains tried a room (coilCandidates); the best few are assembled
    int coilKeep = 2;            // coil candidates kept for assembly
    // walkable slopes the generator keeps to (the play mesh's limits, on the inside edge of a bend: playmesh.hpp)
    double walkRise = 0.45, walkFall = 0.8;
    // the solved profiles (coils, towers, roller loops) plan within these shares of the walkable slopes: climbs
    // gentler (a weak player, or the bad bot, holding a half stick still gets up a long climb), descents nearly all
    double coilRiseSafety = 0.82, coilFallSafety = 0.95;
    int botRuns = 32;            // perturbed bot runs per validation
    int maxRepairRounds = 6;
    double maxLapSeconds = 90.0;
    bool debug = false;          // print per-candidate diagnostics to stderr
    std::string preferSkeleton;  // optional: bias selection toward perimeter/figure8/helix/circle
    double railShareMax = 0.35;  // rail stations beyond this share of the course cost railExcessCost a 10 stations
    double railExcessCost = 0.0;   // off: in small rooms the bends need the rails (a cost there traded a figure-8 for a 1.7 m circle)
    Physics physics;
};

}  // namespace rr
