// Room input: traced floor polygon, furniture boxes, player exclusion cylinder (meters).
#pragma once
#include <string>
#include <vector>

#include "roomrun/geom.hpp"
#include "roomrun/json.hpp"
#include "roomrun/rng.hpp"

namespace rr {

struct Box {
    ORect rect;
    double height = 0.5;
    std::string label;
};

struct Room {
    Polygon floor;           // CCW after normalize()
    std::vector<Box> boxes;
    Vec2 player;             // generate position (where the user stands)
    double playerRadius = 0.7;
};

Room roomFromJson(const Json& j);
Json roomToJson(const Room& r);
bool validateRoom(const Room& r, std::string* err);
void normalizeRoom(Room& r);                // make floor CCW
Room quantizeRoom(const Room& r, double q = 0.05);  // snap to grid (seed stability)
uint64_t roomHash(const Room& quantized);

// Random room generator used by the batch harness and tests.
enum class RoomType { Rectangle, LShape, Hallway, Irregular };
Room randomRoom(Rng& rng, RoomType type);
const char* roomTypeName(RoomType t);

}  // namespace rr
