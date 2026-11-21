#!/usr/bin/env bash
# Regenerate example courses (run inside the build container from roomrun/: ./run.sh ./examples/make_examples.sh).
set -e
gen() {  # name room extra-args...
  local name=$1 room=$2; shift 2
  ./build/roomrun-gen --room examples/rooms/$room.json --seed 7 "$@" \
    --out examples/$name.course.json --svg examples/$name.svg --obj examples/$name.obj | head -1 | sed "s/^/$name: /"
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
