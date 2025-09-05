#!/usr/bin/env bash -eu

# Build with fuzzing enabled using default SITL configuration
PX4_FUZZ=1 make px4_sitl
cp build/px4_sitl_default/bin/px4 $OUT/px4
