#!/usr/bin/env bash
# Regenerate example courses (run inside the build container from roomrun/: ./run.sh ./examples/make_examples.sh).
# Each gets its course JSON, SVG (plan + side profile), OBJ, and a 3D preview (examples/png/NAME_3d.ppm: a corner
# view and a side view of the play mesh with its objects). The container has no image tools: convert the PPMs on
# the host, e.g. `for f in examples/png/*.ppm; do magick "$f" "${f%.ppm}.png" && rm "$f"; done`.
set -e
gen() {  # name room extra-args...
  local name=$1 room=$2; shift 2
  ./build/roomrun-gen --room examples/rooms/$room.json --seed 7 "$@" \
    --out examples/$name.course.json --svg examples/$name.svg --obj examples/$name.obj --ppm examples/png/${name}_3d.ppm \
    | grep -E "^skeleton|^shape" | sed "s/^/$name: /"
}
gen living_room living_room
gen bedroom bedroom
gen l_kitchen l_kitchen
gen hallway_helix hallway --prefer helix
gen attic attic
gen studio_figure8 studio --prefer figure8
gen tiny_office tiny_office
gen living_room_15cm living_room --sonic-cm 15
gen living_room_chao_race living_room --modules data/modules_chao.json
# the VR player's play areas (Sonic 12 cm): the Frame's 1 x 1 m one made 2 x 2 m, a 2 x 2 m corner spot, 3 x 2.5 m
gen frame_1x1 frame_1x1 --sonic-cm 12 --seed 1
gen play_2x2_corner play_2x2_corner --sonic-cm 12
gen play_3x2_5 play_3x2_5 --sonic-cm 12
gen play_3x2_5_coil play_3x2_5 --sonic-cm 12 --prefer coil
