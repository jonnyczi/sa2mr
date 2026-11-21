// The game's own objects on a generated course (Room Run in the game, external/OpenSA2/host/xr/README.md
// "Room Run objects"): ring lines and ring circles, springs where the course has spring modules, dash
// panels on boost sections, dash ramps at gaps, grind-rail paths along the rail strips, and (optional)
// enemies on flat road. The play mesh stays underneath as it is (a spring's flight and a gap keep their
// bridged ramp, a rail its walled strip), so every course stays completable without the objects; the
// objects are the fun on top.
//
// Frame: the play mesh's (metres, X = the plan's x, Y up, Z = -(the plan's y)). Speeds are in real SA2
// units per frame (unscaled: the game runs at speed scale 1, the generator's arcs keep their shape under
// its time dilation). The game's own conventions (SET rotations and scales) are the VR player's business
// (host/xr/xr_roomrun.cpp); this file says what each object should do.
//
// Flights (springs, dash ramps) are planned and then checked against a model of the real game's air
// physics (gravity and a drag a frame, measured in OpenSA2: docs/vr-roomrun-objects.md): the flight must
// stay inside the walkway, under any deck above it, and come down on the course's deck ahead within the
// module (or the next one). A flight that fails is left out: the bridge under it carries Sonic.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "roomrun/playmesh.hpp"

namespace rr {

enum class ObjKind : uint8_t { Ring, RingLine, RingCircle, Spring, DashPanel, DashRamp, Beetle, Hunter };
const char* objKindName(ObjKind k);

struct CourseObject {
    ObjKind kind = ObjKind::Ring;
    Vec3 p;               // its place (metres): a ring's centre, a line's first ring, a circle's centre, the
                          // spring / panel / ramp / enemy on the deck (an enemy's hover height added)
    Vec3 dir;             // unit: the way along the course (horizontal, but a ring line's, which climbs with the deck)
    int station = 0;      // its station on the centreline
    int count = 0;        // rings: in the line or circle
    double spacingU = 0;  // a ring line's spacing (units)
    double radiusU = 0;   // a ring circle's radius (units)
    Vec3 launchU;         // a spring's or a dash ramp's launch velocity (units a frame; course frame, Y up)
    double speedU = 0;    // a dash panel's speed (units a frame)
    int lockFrames = 0;   // a dash panel's no-control frames
    int landStation = -1; // a flight's landing station (the check's)
    double apexU = 0;     // a flight's highest point above its launch (units)
    int flightFrames = 0;
};

struct CourseRail {
    std::vector<Vec3> pts;   // the grind path (metres): the rail strip's centreline, railHeightU above its deck
    int s0 = 0, s1 = 0;      // its stations [s0, s1)
    double riseU = 0;        // its largest climb above its start (units)
};

struct ObjectParams {
    bool rings = true, springs = true, dashPanels = true, dashRamps = true, rails = true;
    double enemiesPerM = 0.0;    // GUN Beetles / Hunters on flat road (0: none, the default)
    int enemyMax = 4;
    double ringHeightU = 6.0;    // a ring's centre above the deck
    double ringSpacingU = 16.0;  // a line's spacing (the game's RING_LINEAR: scale.x + 10)
    int ringLineMax = 6;         // rings in a line (the game clamps 1..8)
    int ringGapStations = 10;    // stations between ring groups
    double straightTolU = 3.0;   // a ring line's straightness: every station within this of its chord
    double ringCircleRadiusU = 10.0;
    int ringCircleCount = 8;
    double railHeightU = 1.5;    // the grind path above the strip's deck
    double railMaxRiseU = 40.0;  // a rail climbing more than this above its start gets no grind path (the
                                 // game's rail slows him uphill and slides him back: v^2 / 2g at 4 u/f is 100)
    // the game's air physics (measured in OpenSA2 on a spring's flight: docs/vr-roomrun-objects.md)
    double gravityU = 0.08;      // units a frame^2
    double airDrag = 0.01;       // the velocity's loss a frame in the air (vertical, measured; horizontal assumed the same)
    double springLift = 5.0;     // a spring puts him this far above itself before the launch
    double springApexU = 16.0;   // the flight's apex above the higher of take-off and landing (the module's "apex")
    double rampKickDeg = 20.0;   // a dash ramp's launch angle (the game's ramp: 3.2 + scale z up, scale x forward)
    int rampHoldFrames = 23;     // the ramp holds his upward speed this long (no gravity), and
    double rampDrag = 0.0;       // no drag (measured in OpenSA2, 2026-11-21: 1.14 up for 23 frames, 3.34 forward throughout)
    double sonicHeightU = 10.0;  // the flight must clear a deck above by this
    double sonicRadiusU = 3.0;   // and stay this far inside the walkway
};

struct ObjectStats {
    int rings = 0, ringLines = 0, ringCircles = 0, springs = 0, dashPanels = 0, dashRamps = 0, enemies = 0, rails = 0;
    int railStations = 0;
    int springsDropped = 0, rampsDropped = 0, railsDropped = 0;   // left out by the checks (the bridge / strip remains)
    std::vector<std::string> notes;                                // why each was left out
};

struct CourseObjects {
    std::vector<CourseObject> objs;
    std::vector<CourseRail> rails;
    ObjectStats stats;
};

// The objects for a course and its play mesh (deterministic).
CourseObjects placeObjects(const GenResult& r, const PlayMesh& pm, const ModuleLibrary& lib, const GenParams& gp,
                           const ObjectParams& op = ObjectParams());

// A flight under the game's air physics from `from` (metres) with velocity v (units a frame), against the
// play mesh: where it comes down. landStation -1: it left the walkway, met a deck above, or never came
// down within maxFrames. sample: the path (metres), one point a frame, if asked.
struct FlightCheck {
    int landStation = -1;
    int frames = 0;
    double apexU = 0;
    std::string why;           // the failure
    std::vector<Vec3> path;
};
FlightCheck checkFlight(const PlayMesh& pm, const Vec3& from, const Vec3& vU, int fromStation, int maxAheadStations,
                        const ObjectParams& op, bool sample = false, int holdFrames = 0, double drag = -1);

// A summary line ("12 rings in 3 lines and 1 circle, 1 spring, ...").
std::string objectSummary(const CourseObjects& o);

}  // namespace rr
