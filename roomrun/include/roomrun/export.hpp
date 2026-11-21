// Course serialisation: JSON (engine import), SVG (top-down + side profile), OBJ (Blender).
#pragma once
#include <string>

#include "roomrun/generator.hpp"

namespace rr {

Json courseToJson(const GenResult& r, const ModuleLibrary& lib, const GenParams& gp, uint64_t userSeed);
std::string courseToSvg(const GenResult& r, const ModuleLibrary& lib, const GenParams& gp);
std::string courseToObj(const GenResult& r, const ModuleLibrary& lib, const GenParams& gp);

}  // namespace rr
