// How a course uses its space (the batch's "shape" line and the tests): heights, stacked tiers,
// over/under crossings, the smallest deck-to-deck gap where decks overlap, lap length per floor area.
// Measured on the play mesh (the game's decks), so it holds for any generator version.
#pragma once
#include "roomrun/playmesh.hpp"

namespace rr {

struct ShapeMetrics {
    double lapM = 0;            // lap length (m)
    double floorM2 = 0;         // the room's (play area's) floor area
    double lapPerM2 = 0;        // lap length per floor area (m / m^2)
    double minDeckM = 0, maxDeckM = 0, meanDeckM = 0;   // deck heights above the floor
    int tiers = 1;              // most decks stacked over one spot (1 = nothing stacked)
    int crossings = 0;          // over/under places: runs of a deck passing over another
    double stackedShare = 0;    // share of stations with a deck above or below
    double minStackGapU = 0;    // smallest height between decks that overlap in plan (units; 0 if none)
    double minStackGapM = 0;
};

// overlapM: walkway edges closer than this count as overlapping (the decks' own half widths are added)
ShapeMetrics shapeMetrics(const GenResult& r, const PlayMesh& pm, double overlapM = 0.0);

}  // namespace rr
