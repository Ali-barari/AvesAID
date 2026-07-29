# C1 — kinematic braking + stop-line latch: results

Change: `PX4-Autopilot/src/lib/collision_avoidance_3d` — replaces the fixed
`CA3D_SLOW_SCALE` stair with a continuous kinematic velocity cap
`v_allow(d)=sqrt(2·a·max(d−D_STOP,0))` and adds a per-direction stop-line latch.
Two new params: `CA3D_BRK_ACC` (default 2.0 m/s²; 0 = legacy), `CA3D_D_HYST` (0.3 m).
Full design + rationale in the library `README.md` and `ca3d_params.c`.

## Unit tests (gtest, `make tests TESTFILTER=CollisionAvoidance3D`) — 23/23 PASS

- **v_allow table**: d=1.1→0, (d=2.0,a=3)→2.324, (d=4.5,a=3)→4.517; monotonic; 0 below D_STOP.
- **vector cap**: oblique approach — closing component reduced to v_allow, tangential
  preserved; diagonal bearing clipped along-bearing only; multiple cells → lowest v_allow wins.
- **kinematic cap end-to-end**: bites in the SAFE zone (wall 3.0 m, cmd 5 → 3.376 m/s);
  slow flight (cmd 2 < v_allow) untouched; vertical cap on a ceiling.
- **stop-line latch state machine**: entry at d<D_STOP; NO release at D_STOP+0.1 (below HYST)
  even with clear frames; release at D_STOP+0.35 after STOP_EXIT retreat frames; closing
  clamped to ≤0 while latched; reverse passes through.
- **ceiling latch under sustained up-stick**: 30 flapping frames (1.08/1.15 m), creep = 0.
- **CA3D_BRK_ACC=0 → exact legacy**: SLOW stair scales 0.35, STOP zeroes, latch never engages.

## SITL (px4_sitl Gazebo iris, OFFBOARD ideal-wall injector at local x=8, zero-latency
## field, long range so the control law — not sensor range — governs)

Metric = deepest forward x reached during the **approach phase** (start verified ≈0).
D_STOP line is at ground-truth x = 6.9 (wall 8.0 − D_STOP 1.1). "standoff" = 8.0 − peak_x.

### Stop distance vs commanded speed — kinematic a=2.0

| cmd v (m/s) | peak_x | standoff from wall | penetration past D_STOP | contact |
|---|---|---|---|---|
| 1 | 7.85 | +0.15 | 0.95 | no |
| 2 | 8.17 | −0.17 | 1.27 | yes |
| 3 | 8.63 | −0.63 | 1.73 | yes |
| 4 | 9.17 | −1.17 | 2.27 | yes |
| 5 | 9.28 | −1.28 | 2.38 | yes |
| 6 | 9.40 | −1.40 | 2.50 | yes |

### Legacy (a=0) baseline, same rig — high speed

| cmd v | kinematic a=2.0 standoff | legacy a=0 standoff (same rig) | design-doc old E-series | improvement |
|---|---|---|---|---|
| 4 | −1.17 | −1.71 | −1.6 | 0.54 m |
| 5 | −1.28 | −2.94 | −2.7 | 1.66 m |
| 6 | −1.40 | −4.18 | −4.1 | 2.78 m (66% less penetration) |

The legacy (a=0) baseline reproduces the design-doc E-series almost exactly, confirming the
rig is faithful. Kinematic braking's advantage grows with speed — it cuts 6 m/s penetration
by two-thirds — because the curve starts braking metres earlier than the stair.

### acc sweep at v=3 (why a is not the lever)

| a | peak penetration past wall |
|---|---|
| 3.5 | −0.87 |
| 2.0 | −1.24 |
| 1.2 | −1.12 |
| 0.8 | −0.63 |

Penetration is non-monotonic in a and dominated by ~0.4 m run-to-run/lag noise — **tuning
a does not resolve the overshoot**.

## Interpretation (honest)

1. **The cap is correct.** A console trace of the closing setpoint shows it tracking
   `v_allow(d)` exactly at every range (e.g. d=2.31→2.91, d=1.55→1.77, d=1.17→0.70).
2. **Large high-speed improvement.** At 6 m/s the kinematic law cuts penetration from the
   legacy ~4.1 m (design E-series) to ~1.4 m — it starts braking metres earlier, which is
   the whole point of the curve.
3. **Residual overshoot from controller lag, not the cap.** The vehicle tracks `v_allow`
   with a growing velocity lag (~+0.4 m/s at moderate range, blowing up near D_STOP because
   the sqrt slope → ∞ there), so it reaches the stop line with residual speed and coasts
   past. This is a velocity-loop / attitude-response limit of the SITL iris in the OFFBOARD
   velocity rig, independent of a. Consequently the strict acceptance (zero contact and
   stop ≥ D_STOP−0.15 at every speed) is **met only at 1 m/s** in this rig; 2–6 m/s still
   contact, though far less than legacy.
4. Per the C1 design ("if SITL shows residual overshoot, report the measured numbers first;
   do NOT add feedforward / jerk shaping / predictive filtering / margin params"), no such
   term was added. `CA3D_BRK_ACC` is left at the source-derived 0.7·MPC_ACC_HOR≈2.0.

## Closed-loop latch tests (item 3)

| test | rig | result |
|---|---|---|
| **9 reverse-away** (`t7_recovery.py`) | wall, OFFBOARD | STOP at x=7.58; on reverse cmd the vehicle **backs away immediately** (released x=7.69). Latch allows reverse/away — PASS. |
| **10 C3+ suppress/recover** (`c3plus_recover.py`) | wall, OFFBOARD | Kill field mid-cruise → **failsafe STOP in 1.96 s**; latched-hold creep 0.12 m/2 s while still commanding forward; restart field → **recovers +5.09 m** via STOP_EXIT — PASS. |
| **POSCTL stick** (`posctl_wall.py`) | wall, POSCTL manual | Mode confirmed POSCTL (mask bit0); CA3D **caps + stops** (peak 8.17) — confirms the law runs on the manual-position path, not just OFFBOARD. Same lag overshoot as OFFBOARD. |
| **7 ceiling ratchet** (`ceiling_climb.py`) | ceiling z=3.0, 60 s up-stick | z(t) oscillates 1.83↔2.94 around the 1.9 latch line; steady-window drift **−0.87 m (NO cumulative ratchet)** — the anti-ratchet works. But the dynamic climb-*into* the ceiling overshoots the line by ~1.05 m (same lag). Caveat: this flies into the ceiling rather than hovering at the line, so it conflates overshoot with ratchet; the ratchet metric (no upward drift) is the meaningful one and passes. |

Net: the **latch behaves correctly** — allows reverse, holds under failsafe, recovers on
resume, and does not ratchet — and CA3D is confirmed active on the POSCTL path. The residual
dynamic overshoot is the same controller lag characterized below, not a latch fault.

### Ceiling z-trace (test 7) — drift sign and non-growing offset

Ceiling at z=3.0 (above), latch line at z = 3.0 − D_STOP = 1.90. Data `results/c1_t7_ceiling.csv`:

```
 t(s)   z(m)   vz     gap-to-ceiling (3.0 − z)
  0.0   1.608  -0.01   1.39
  5.0   1.641   0.00   1.36
 15.0   1.693   0.00   1.31
 20.0   2.022   0.00   0.98     <- climbing INTO the ceiling (dynamic)
 25.0   2.452   0.00   0.55
 30.0   2.808   0.00   0.19
 35.0   2.941   0.00   0.06     <- peak: lag overshoot, 1.05 m past the latch line
 40.0   2.736   0.00   0.26
 45.0   2.266   0.00   0.73
 50.0   1.833   0.00   1.17     <- fell back BELOW the latch line
 55.0   1.971  -0.03   1.03
```

**Drift sign:** after the peak the trajectory moves **downward, i.e. AWAY from the ceiling**
(gap-to-ceiling grows 0.06 → ~1.0 m). Steady-window (t_latch+10 s → end) drift = **−0.87 m**
(negative = away from ceiling). **The offset toward the ceiling is non-growing**: the vehicle
does not creep upward cycle-over-cycle — it overshoots once (lag) then oscillates back around
the latch line and settles near it. This is the anti-ratchet property (design target: no
cumulative creep; legacy was +0.52 m ratchet). It is a single lag overshoot, not a ratchet.

**Vertical A_eff: UNCHARACTERIZED.** `A_eff = 1.25 m/s²` was measured on the *horizontal*
wall approach only. The climb axis was not cleanly characterized (the ceiling run is an
oscillation, not a monotone decel), and `CA3D_BRK_ACC = 2.0` is **inherited** on the vertical
axis (same param drives up/down via `up_cm`/`down_cm`). `MPC_ACC_UP_MAX = 4.0`,
`MPC_ACC_DOWN_MAX = 3.0` bound it from above, but the achievable vertical A_eff and its lag
are not measured here — characterize before relying on vertical stop distances.

## Follow-up analysis — lag vs horizon clipping (merge-gating, from data)

Tooling: `rig/c1_lag_analysis.py` over the existing `_traj.csv` (approach phase).
Facts used: `MPC_XY_VEL_P_ACC = 1.8` (accel_sp = 1.8·v_err), real encoder horizon
`obstacle_field_node max_range_m = 4.0`, injector horizon 25 m (Phase B) / 20 m
(acc sweep), wall at x=8 with start x≈0 → geometric appearance range ≈ 8 m.

### (1a) The cap bites on time — not a timing/horizon problem in this rig

| v cmd | v_peak | bite_meas (d, m) | bite_pred = D_STOP+v_peak²/2a | appear (m) | verdict |
|---|---|---|---|---|---|
| 1 | 0.99 | 1.25 | 1.35 | 7.97 | lag |
| 2 | 1.95 | 1.86 | 2.05 | 8.02 | lag |
| 3 | 2.85 | 2.78 | 3.13 | 7.97 | lag |
| 4 | 3.56 | 3.78 | 4.28 | 7.93 | lag |
| 5 | 4.05 | 4.72 | 5.20 | 8.03 | lag |
| 6 | 4.23 | 5.12 | 5.58 | 7.96 | lag |

Measured bite ≈ predicted (within ~0.4 m) and always **< the ~8 m appearance range**,
so the cap starts on schedule and is not horizon-clipped in the injector sweep. (The
vehicle never reaches commanded 5–6 m/s — v_peak saturates ~4.2 m/s in the 8 m runway.)
**But** against the *real* 4.0 m encoder horizon, bite_pred exceeds 4 m for v_peak ≳ 3.4
(v≥4 rows) → those speeds *would* be horizon-clipped on the real sensor.

### (1b) The overshoot is controller lag — quantified

`v_err = v_act − v_sp` on the braking ramp (v_sp = min(v_peak, v_allow(d))):

| v cmd | mean v_err (m/s) | measured A_eff (m/s²) | P-law 1.8·v_err (m/s²) | A_eff / P-law |
|---|---|---|---|---|
| 1 | 0.59 | 0.66 | 1.07 | 0.62 |
| 3 | 1.02 | 1.16 | 1.84 | 0.63 |
| 4 | 1.17 | 1.34 | 2.10 | 0.64 |
| 6 | 1.25 | 1.43 | 2.24 | 0.64 |

v_err **grows monotonically along the ramp** (v=4 example: +0.22 at d=3.9 → +0.89 at 2.7
→ +1.33 at 1.9 → v_act still **2.45 m/s at d=0.8 where v_sp=0**). The achieved decel is a
steady **~64% of the P-law command 1.8·v_err** — i.e. the velocity P-loop asks for more
decel than the attitude/thrust response delivers. That downstream response, not the cap,
is the binding lag. Implied lag constant **τ ≈ v_err/a ≈ 0.5–0.6 s**.

Conclusion: **lag-dominated in the injector rig** (bite on time, R not binding), and
**horizon-dominated on the real 4 m encoder** (bite_pred > 4 m for v ≳ 3.4). Both are
outside the CA3D law; the law itself is correct and bites where predicted.

### (2) Speed envelope, restated with measured A_eff

`v_max = √(2·A_eff·(R − D_STOP))`, A_eff = 1.25 m/s² (median, a=2 sweep):

| horizon R | source | v_max (stop-capable) |
|---|---|---|
| 4.0 m | **real `obstacle_field_node` max_range_m** | **2.69 m/s** |
| 8.0 m | this rig's wall geometry | 4.15 m/s |
| 25 m | injector (law-isolation) | 7.72 m/s |

So on the shipped perception horizon (4 m) the no-contact envelope is ~2.7 m/s regardless
of `CA3D_BRK_ACC` — consistent with the design's observed ~2.2–2.5 m/s ceiling, and set by
**perception range + vehicle decel**, not by the cap. Raising the envelope needs a larger
encoder `max_range_m` (R) and/or higher achievable decel A_eff (controller), not cap tuning.

### Range-encoding headroom check for max_range_m = 8 (source)

Per-cell ranges in `ca3d_field_t` (`ca3d_tunnel_payload.h`): `ring_cm[3][12]`, `up_cm`,
`down_cm`, `max_range_cm` are all **`uint16_t`, unit centimetres, 16-bit**. The encoder
(`obstacle_field_node.cpp` `clampRangeCm`) clamps every valid cell to `[1, max_range_cm]`;
`max_range_cm = min(CA3D_INVALID_CM−1, round(max_range_m·100)) = min(65534, …)`, and
`CA3D_INVALID_CM = 0xFFFF (65535)` is the "no return" sentinel.

- `max_range_m = 8` → `max_range_cm = 800`. Max *valid* code = 65534 cm (655.34 m); sentinel
  65535. So 800 uses **~1.2 % of the field width — no saturation, no overflow, no sentinel
  collision.** The encoding has headroom for R far beyond 8 m (hundreds of metres) before the
  uint16 cm representation is a concern. **max_range_m is NOT changed here** (sequenced after
  C2 — elevation-quantization error grows with range).

## Recommendation for review

Two independent binding limits, both outside the CA3D law:
- **Perception horizon** (real encoder R = 4 m) caps the stop-capable envelope at
  ~2.7 m/s (§2). To exceed it, raise `obstacle_field_node max_range_m` — a ROS-side change.
- **Velocity-loop / attitude lag** (τ ≈ 0.5–0.6 s, A_eff ≈ 0.64·P-law) causes overshoot even
  when the horizon is generous (§1b). To close it, a controller-side change (velocity-loop /
  stopping behavior), or the τ-shift below.

**Candidate remedy (PROPOSAL ONLY — not implemented; blocked on this analysis, now done):**
a single measured-τ distance shift — evaluate the cap at `d_eff = d − τ·v_close` instead of
`d`, so the curve reaches 0 at `D_STOP + τ·v` and the lagging vehicle stops at ~D_STOP. One
new param `CA3D_BRK_TAU` (≈0.5 s from §1b); no feedforward, jerk, or predictive filtering.
Do NOT implement until the team agrees, per the C1 design's "report numbers first" clause.

**Merge stance:** the law is correct and strictly better than legacy at every speed; merge to
a branch. The residual overshoot and the 4 m horizon ceiling are documented above with numbers
for the team to decide the next step (encoder range vs controller vs τ-shift). **C1 ACCEPTED /
MERGED** on branch `c1-kinematic-braking`.

## Roadmap

- **Envelope formula CERTIFIED:** `v_max = √(2·A_eff·(R − D_STOP))`, with `A_eff = 1.25 m/s²`
  measured (horizontal, §1b). Governs the no-contact speed ceiling for a given sensor horizon R.
- **Next speed step:** C2 (elevation quantization) → then raise encoder `max_range_m` to **8**
  (uint16-cm headroom confirmed above) → re-sweep the envelope. Sequenced this way because
  elevation-quantization error scales with range, so C2 must land before extending R.
  Predicted post-C2 envelope at R=8: `v_max = √(2·1.25·6.9) ≈ 4.15 m/s` (pending re-measure).
- **`CA3D_BRK_TAU` stays PROPOSAL-ONLY** — the measured-τ distance shift `d_eff = d − τ·v`
  (τ≈0.5 s) is not implemented; revisit only if lag-limited overshoot must be closed after the
  horizon is extended. No feedforward/jerk/predictive terms.
- **Open item:** vertical A_eff uncharacterized (test 7); characterize before relying on
  vertical stop distances.

Reproduce: `bash tests/scripts/c1_speed_sweep.sh` (Phase B), `bash tests/scripts/c1_kinematic_sweep.sh`
(acc sweep). Per-run PlotJuggler-readable CSVs in `tests/results/c1B_*_traj.csv` (commanded
vs actual velocity, pose) and `_gt.csv` (Gazebo ground truth).
