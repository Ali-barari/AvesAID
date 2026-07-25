# collision_avoidance_3d — Iron Sphere native 3D collision avoidance

Full-3D reactive velocity-override safety layer for PX4 (fork of `v1.14.0`,
branch `avestec/ca3d-v1.14.0`). It is the FCU-side half of Iron Sphere: the ROS
companion (`uav_collision_avoidance`) does the geometry (SLAM cloud → filtering →
RANSAC ground split → 3D distance field) and streams a compact field to the FCU
over a **stock MAVLink TUNNEL** message; this library owns the **authority**
(zones, state, per-axis override, failsafe, params, logging).

Unlike the built-in 2D `collision_prevention` (which only runs in manual position
mode), this hooks a single call site in `mc_pos_control` that every velocity-
controlled mode passes through, so it covers **POSCTL / OFFBOARD / AUTO**.

## Data flow

```
mavlink TUNNEL (id 385, payload_type 32800)
   → mavlink_receiver (STOCK) → uORB mavlink_tunnel
      → CollisionAvoidance3D::_readField()   decode + validate (magic/version/len/seq)
      → _classify()                          cells → pseudo-points → 6-dir mask + global min_dist
                                             + per-direction nearest range (_dir_dist[6])
      → _updateState()                       SAFE/SLOW/STOP, hysteresis + stop-exit
      → _updateLatch()                       per-direction stop-line latch (CA3D_BRK_ACC>0)
      → modifySetpoint3D()                   NED→FRD, kinematic cap + latch (or legacy stair), FRD→NED
   ↑ called from MulticopterPositionControl::Run(), immediately before
     _control.setInputSetpoint(_setpoint)   (MulticopterPositionControl.cpp)
   → uORB ca3d_status (logged in the default SD profile)
```

The wire contract is `ca3d_tunnel_payload.h`, vendored **byte-identical** from the
ROS repo (`uav_collision_avoidance/include/uav_collision_avoidance/ca3d_tunnel_payload.h`).
Any change must be applied to both copies and kept `diff`-clean. Field: 84 bytes,
3×12 azimuth/elevation ring cells + up/down caps, ranges in cm, body FRD.

## Direction / frame conventions

- Body **FRD** (x fwd, y right, z **down**); ROS `base_link` is FLU, the encoder
  flips y and z. Azimuth 0 = +X forward, increasing toward +Y (right).
- Direction bits (`CA3D_DIR_*`, identical numbering to the ROS decision layer):
  bit0 fwd, 1 back, 2 left, 3 right, 4 up, 5 down.
- The velocity setpoint is local **NED**; the attitude quaternion
  (`vehicle_attitude.q`, FRD→NED) rotates it into body FRD and back.

## Safety invariants

- Only ever **constrains** motion: `|out| ≤ |in|` per axis, never flips sign,
  never injects motion, never touches attitude/rates other than optional yaw-rate
  zeroing (STOP + `CA3D_YAW_LOCK`), and **passes NaN components through untouched**
  (so pure position setpoints are preserved).
- No modification when `CA3D_EN=0`, disarmed, landed, or the current mode isn't in
  `CA3D_MODE_MASK` — bit-exact passthrough.
- Stale/absent field while active + airborne → `CA3D_FS_MODE` (default: STOP), with
  a throttled `mavlink_log`.

## Kinematic braking + stop-line latch (`CA3D_BRK_ACC > 0`)

The velocity command in SAFE/SLOW is a **continuous, speed-dependent cap** instead of
the fixed `CA3D_SLOW_SCALE` stair. Per direction with nearest obstacle range `d`:

```
v_allow(d) = sqrt( 2 · CA3D_BRK_ACC · max(d − CA3D_D_STOP, 0) )
```

The closing component of the setpoint along that direction is limited to `v_allow`
(`vAllow()` / `capClosing()` — pure, unit-tested). It reaches **0 exactly at
`D_STOP`** and starts biting at `d = D_STOP + v²/(2a)`, so slow flight is untouched
while fast flight eases much earlier. `_dir_dist[6]` holds the nearest range per
cardinal direction (no `BLK_*` gate — the cap self-gates by distance); the binding
constraint is automatically the nearest cell in each direction. The vertical axis
uses the same law on `up_cm`/`down_cm`. The SAFE/SLOW/STOP zones, `dir_mask`,
hysteresis, failsafe, and `ca3d_status` are unchanged — only the command changes.

**Stop-line latch** (`_latch_mask`, one bit per direction): on entry to STOP in a
direction (`d < D_STOP`) the closing component in that direction is clamped to `≤ 0`
(reverse/away and unblocked lateral stay free). Release requires **both**
`CA3D_STOP_EXIT` frames **and** the range to exceed `D_STOP + CA3D_D_HYST` — i.e. the
vehicle must physically retreat past the line, not just catch a few marginal frames
at the boundary. This kills the boundary-noise ratchet. The stale-field failsafe
latches all directions and recovers via the same retreat + `STOP_EXIT` path.

`CA3D_BRK_ACC` is derated from the controller's achievable deceleration
(`MPC_ACC_HOR`=3.0, `MPC_ACC_HOR_MAX`=5.0 in this fork) so the cap stays trackable
despite velocity-loop/attitude lag and the 20 Hz field rate — see `ca3d_params.c`
and the SITL sweep in `tests/` for the tuning rationale. `CA3D_BRK_ACC = 0` restores
byte-identical legacy behavior.

## Parameters (`@group Collision Avoidance 3D`)

| Param | Default | Meaning |
|---|---|---|
| `CA3D_EN` | 0 | master enable |
| `CA3D_D_STOP` | 1.1 m | STOP zone distance |
| `CA3D_D_SLOW` | 2.0 m | SLOW zone distance |
| `CA3D_HYST` | 0.1 m | state hysteresis |
| `CA3D_STOP_EXIT` | 3 | consecutive clear fields to leave STOP (also the latch retreat count) |
| `CA3D_SLOW_SCALE` | 0.35 | **legacy** speed kept on a blocked axis in SLOW (inactive when `CA3D_BRK_ACC>0`) |
| `CA3D_BRK_ACC` | 2.0 m/s² | kinematic braking decel (≈0.7·MPC_ACC_HOR); **0 = legacy stair + old release** (rollout switch) |
| `CA3D_D_HYST` | 0.3 m | retreat margin beyond `D_STOP` a latched stop-line needs to release |
| `CA3D_BLK_XY` | 1.1 m | max range for a cell to block an X/Y axis |
| `CA3D_BLK_Z` | 1.1 m | max range for a cell to block the Z axis |
| `CA3D_YAW_LOCK` | 0 | 1 = zero yaw rate in STOP |
| `CA3D_TIMEOUT` | 0.5 s | field staleness threshold |
| `CA3D_FS_MODE` | 1 | 0 = passthrough+warn, 1 = STOP |
| `CA3D_MODE_MASK` | 3 | bit0 POSCTL, bit1 OFFBOARD, bit2 AUTO |

`min_points_per_dir` from the old ROS supervisor moved to the encoder
(`min_points_per_cell`); `landing_suppress_neg_z` is obsolete — the ground is
excluded at the source by RANSAC, so the floor never appears as an obstacle.

## Build, test, flash

```sh
make px4_sitl_default                       # SITL
make px4_fmu-v6c_default                    # hardware target (parameterize as needed)
make tests TESTFILTER=CollisionAvoidance3D  # unit tests
make check_format                           # style
```

Docker: pin `ARG PX4_VERSION` to the fork tag `avestec/ca3d-v1.14.0`.

## Interaction with stock CP

Leave the 2D `collision_prevention` off (`CP_DIST < 0`) when using CA3D — don't run
both. CA3D does not modify pure position setpoints (velocity-setpoint modification
only); AUTO position missions pass through unless they command velocity. 30°
azimuth resolution; single-field atomicity (no cross-frame fusion). These are v1
limits, documented for v2.
