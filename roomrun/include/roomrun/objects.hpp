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
    bool overRoad = false;   // a grind path of its own over road (not over a rail strip)
};

struct ObjectParams {
    bool rings = true, springs = true, dashPanels = true, dashRamps = true, rails = true;
    double enemiesPerM = 0.25;   // enemies a metre of course on flat road, away from landings, rails and springs
    int enemyMax = 3;
    int enemyMin = 1;            // at least this many where a safe spot exists (a course of 2.5 m or more)
    double hunterShare = 0.0;    // the share of GUN Hunters (their shots killed toy Sonic: none by default); the rest GUN Beetles
    double beetleHeightU = 20.0; // a Beetle hovers this far above the deck: he runs under it and jumps to attack
    bool ensureVariety = true;   // a course without a spring module gets a hop spring on its road, one without a dash
                                 // module a dash panel (Frame test 2026-11-21: a 2 x 2 m course had rings only)
    double hopApexU = 12.0;      // a hop spring's apex
    int hopMinStations = 6, hopMaxStations = 10;   // its landing, stations ahead
    int climbSprings = 2;        // springs up a climb onto a higher deck ahead (vertical courses), at most this many
    int climbSpringMaxStations = 16;   // their landing, at most this far ahead
    double climbSpringMinRiseM = 0.12; // and at least this much higher
    double smallDashU = 6.0;     // the added dash panel's speed and no-control frames
    int smallDashLock = 24;
    double ringHeightU = 6.0;    // a ring's centre above the deck
    double ringSpacingU = 16.0;  // a line's spacing (the game's RING_LINEAR: scale.x + 10)
    int ringLineMax = 6;         // rings in a line (the game clamps 1..8)
    int ringGapStations = 10;    // stations between ring groups
    double straightTolU = 3.0;   // a ring line's straightness: every station within this of its chord
    double ringCircleRadiusU = 10.0;
    int ringCircleCount = 8;
    double railHeightU = 7.0;    // the grind path above the deck: out of reach running (the game grinds within 4 units,
                                 // 6.5 with a no-control timer), reached by a jump; off it, the strip or road below runs
    double railMaxRiseU = 1e9;   // no limit: the player finds his own way up (no assist; he can always jump off)
    int railJoinStations = 6;    // rail runs this close are one grind path (over the road between)
    double railTargetShare = 0.35;  // grind paths over road too, until this share of the stations is under one
    int roadRailMinStations = 14;   // a road stretch for a grind path of its own (3 stations kept clear at each end)
    double railBarU = 1.6;       // the drawn grind bar's thickness (units)
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
    int hopSprings = 0, addedDash = 0, roadRails = 0, grindStations = 0;   // the variety added; stations under a grind path
    int climbSprings = 0;                                          // springs up a climb onto a higher deck
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

// The grind paths drawn: a bar along each and posts down to the deck (the course's own art; metres, Y up).
PlayGroup grindRailGroup(const CourseObjects& co, double mpu, const ObjectParams& op = ObjectParams());

// A summary line ("12 rings in 3 lines and 1 circle, 1 spring, ...").
std::string objectSummary(const CourseObjects& o);

}  // namespace rr
