# C2 — true per-cell elevation (wire format v2): results

Coordinated two-repo change. Branch `c2-true-elevation` in both:
- Encoder: `avestec/ca3d-perception-only` (`ros_ws/src/uav_collision_avoidance`)
- Decoder/FMU: `avestec/ca3d-v1.14.0` (`PX4-Autopilot`)

## The change

Wire v1 discarded each cell's true elevation and the FMU reconstructed bearings from
hardcoded ring-band centers (±45/0). A return at, e.g., −20° (mid band) was rebuilt at
0° → a phantom forward obstacle at drone height; a ceiling return near the +45 band edge
leaked onto the horizontal axes (D4). The error is angular, so it grows with range — a
prerequisite to raising `max_range_m`.

v2 appends `int8 el_deg[3][12]` (deg, +=up, FRD-consistent) after the existing 84-byte
layout → **120 bytes** (TUNNEL max 128). All v1 offsets are unchanged; `el_deg` is the
elevation of each cell's **min-range point** (sampled at the same point/frame the range is
taken from). The FMU reconstructs `b = (cos e cos a, cos e sin a, −sin e)` from the true
per-cell elevation; the C1 dominant-axis classifier then puts steep terrain returns on
DOWN and ceiling returns on UP instead of leaking to horizontal at the 45° band edges.

Version policy (no lockstep): FMU accepts **both** — v1 (84 B) → legacy ring-center
(bit-exact old behavior), v2 (120 B) → true elevation, unknown version → frame rejected
(counts toward the CA3D_TIMEOUT failsafe). Encoder param `wire_version` (default 2, set 1
to roll back to 84-byte frames). Either side updates first, any order.

`CA3D_VERSION 1→2`, `CA3D_VERSION_LEGACY=1` added. **No `max_range_m` change** (sequenced
after C2). No existing field moved, no param defaults changed, no constraint logic changed
beyond the true bearing.

## Unit tests — all green

- **FMU** (`make tests TESTFILTER=CollisionAvoidance3D`): **30/30** (23 C1 + 7 C2).
  C2 cases: v1 ceiling leaks to FWD (legacy, bit-exact); v2 ceiling → UP not FWD (D4 fix);
  sign — below-forward → DOWN (+z), above → UP (−z) (catches the classic flip); unknown
  version → failsafe; boundary el = ±90/0 + sentinel don't-care; flat wall matches C1.
- **Encoder repo** (`catkin build … run_tests`): **25/25** — roundtrip now asserts
  `sizeof==120`, `offsetof(el_deg)==84`, el_deg round-trips, and a "v1 is the exact prefix
  of v2" test (truncate to 84 → decode v1 → el_deg absent/zero).
- **Python mirror** (`ca3d_field.py`, faithful port): v2 terrain(−60)→DOWN, ceiling(+60)→UP,
  no FWD; v1 (+45 ring-center)→FWD leak — matches the FMU byte-for-byte in intent.

## Wire soak (real `/mavros/tunnel/in`)

| encoder | payload on wire | decoded version | el_deg carried | seq |
|---|---|---|---|---|
| v2 | **120 bytes** | 2 | `el_deg[1][0]=−55` ✓ | continuous (0 gaps) |
| v1 | **84 bytes** | 1 | ring-center fallback (0) | continuous (0 gaps) |

120-byte v2 frames traverse the stock TUNNEL intact (no truncation); v1 rollback emits
84-byte frames. Continuous flow during the behavior flights below with no failsafe STOP =
frames accepted end-to-end on both paths (functional mixed-version soak).

## End-to-end behavior (FMU decode via vehicle motion, forward return at x=5, 1.5 m/s)

Forward OFFBOARD cruise; injected return in the forward-az cell at the tagged elevation.
Metric = deepest forward x during the approach (approach covers ~18 m if unconstrained).

| case | wire | el_deg | peak_x | outcome |
|---|---|---|---|---|
| flat wall (legacy) | v1 | 0 | **4.82** | forward STOPS at the wall (≈5, with the C1 lag overshoot) |
| flat wall | v2 | 0 | **4.82** | STOPS — **identical to v1 → wall regression clean (test 7)** |
| terrain below-forward | v2 | −55 | **16.80** | forward **FREE**, flies the full approach — return classified DOWN, **phantom forward STOP eliminated (test 5)** |
| ceiling above-forward | v2 | +55 | **16.82** | forward **FREE** — return classified UP, horizontal unconstrained, **D4 leak gone (test 6)** |

Legacy (v1) discards elevation, so a flat wall, a terrain residual, and a ceiling return all
collapse to the same forward phantom and stop the vehicle (v1_flat stops at 4.82; unit test
`c2LegacyV1CeilingLeaksToForward` confirms a v1 ceiling cell → FWD). v2 separates them by true
bearing: flat wall still stops (byte-identical to v1), terrain becomes a descent-only
constraint, ceiling an up-only constraint — forward flight over both is unblocked. The 4.82
stop distance (both flat cases) is the C1 velocity-loop lag, unchanged by C2 and out of scope
here. Continuous frame flow across all four ~30 s flights with no failsafe STOP =
mixed-version soak clean on both the v2 and v1 paths.

**Test 9 (vis):** RViz `ca3d_field_markers` updated to draw v2 cells at their true elevation
(thin patch) — a below-forward terrain return renders low, not at the horizon; ceiling returns
render high (version-guarded; v1 still draws bands). Verified by construction + the marker
gtest suite; not captured as an image in this headless run.

## Decoder inventory (every consumer of ca3d_field_t)

| decoder | repo | C2 handling |
|---|---|---|
| `CollisionAvoidance3D.cpp` (_readField/_classify) | FMU | UPDATED — v1/v2/unknown, true-el bearing |
| `obstacle_field_node.cpp` | encoder | UPDATED — emits el_deg, `wire_version` param, version-aware pack len |
| `ca3d_tunnel_payload.h` (both, byte-identical) | both | UPDATED — v2 struct, static_asserts |
| `ca3d_field.py` (Python mirror) | rig | UPDATED — v1/v2 decode + classify + encode |
| `ca3d_inject.py` | rig | UPDATED — emits el, `wire_version`, payload_length=len |
| `ca3d_field_markers.hpp` (RViz) | encoder | UPDATED — v2 draws thin patch at true el (ground low), version-guarded |
| `ca3d_payload_roundtrip_test.cpp` | encoder | UPDATED — 120 B, el_deg, v1-prefix |
| `capture_field.py` | rig | inherits `Field` (version-aware); reads full 128-B payload, version byte drives format — no edit needed |
| `test_field_markers.cpp` | encoder | unaffected (uses ring-1 el≈0 and down_cap; guard transparent) — no edit |
| `ca3d_test_driver.py` | encoder | does NOT decode struct bytes (ROS-topic driver) — version-agnostic, no edit |
| Three.js field vis | — | **NOT PRESENT** in the workspace (searched THREE/webgl/BufferGeometry) — nothing to update |

## Out of scope (noted, per task)

- **Azimuth quantization** (30° bins, ±15° max error) — unchanged, accepted for M1.
- No change to ring band boundaries; no new constraint logic beyond the bearing fix.
- **up_cm/down_cm × steep ring cells**: both can feed the Z slots; `_dir_dist[slot]` is a
  min and the cap applies once per axis-direction → single constraint, **no double-subtraction**
  (confirmed by inspection).

## Roadmap

C2 unblocks the range raise: with the angular error corrected, `max_range_m` → 8 can follow
(uint16-cm headroom confirmed in C1) and the speed envelope be re-swept. `CA3D_BRK_TAU`
remains proposal-only.
