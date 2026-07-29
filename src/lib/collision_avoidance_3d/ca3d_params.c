/****************************************************************************
 *
 *   Copyright (c) 2024 Avestec. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file ca3d_params.c
 *
 * Parameters for the Iron Sphere 3D collision avoidance library
 * (collision_avoidance_3d). The ROS companion streams a 3D obstacle distance
 * field over MAVLink TUNNEL; these params own the zones/state/override authority
 * on the FCU. Values default to the field-tested ROS thresholds.
 */

/**
 * Enable 3D collision avoidance
 *
 * Master enable for the native 3D collision avoidance velocity-override layer.
 * When 0 the library never modifies setpoints (bit-exact passthrough).
 *
 * @boolean
 * @group Collision Avoidance 3D
 */
PARAM_DEFINE_INT32(CA3D_EN, 0);

/**
 * Stop distance
 *
 * Obstacle range at or below which the blocked axis is fully stopped (STOP zone).
 *
 * @min 0.2
 * @max 10
 * @decimal 2
 * @unit m
 * @group Collision Avoidance 3D
 */
PARAM_DEFINE_FLOAT(CA3D_D_STOP, 1.1f);

/**
 * Slow distance
 *
 * Obstacle range at or below which the blocked axis is scaled down (SLOW zone).
 * Must be greater than CA3D_D_STOP.
 *
 * @min 0.3
 * @max 20
 * @decimal 2
 * @unit m
 * @group Collision Avoidance 3D
 */
PARAM_DEFINE_FLOAT(CA3D_D_SLOW, 2.0f);

/**
 * State hysteresis
 *
 * Distance margin added when leaving a more restrictive state to avoid chattering
 * around the zone boundaries.
 *
 * @min 0
 * @max 2
 * @decimal 2
 * @unit m
 * @group Collision Avoidance 3D
 */
PARAM_DEFINE_FLOAT(CA3D_HYST, 0.1f);

/**
 * Stop-exit confirmations
 *
 * Number of consecutive clear (non-STOP) fields required before leaving the STOP
 * state. Rejects single-frame dropouts.
 *
 * @min 1
 * @max 20
 * @group Collision Avoidance 3D
 */
PARAM_DEFINE_INT32(CA3D_STOP_EXIT, 3);

/**
 * Slow-down scale (legacy)
 *
 * Fraction of the commanded speed kept on a blocked axis while in the SLOW zone.
 *
 * LEGACY: inactive when the kinematic braking curve is enabled (CA3D_BRK_ACC > 0).
 * Kept registered for backward compatibility and for CA3D_BRK_ACC = 0 mode.
 *
 * @min 0.0
 * @max 1.0
 * @decimal 2
 * @group Collision Avoidance 3D
 */
PARAM_DEFINE_FLOAT(CA3D_SLOW_SCALE, 0.35f);

/**
 * Kinematic braking deceleration
 *
 * Deceleration used by the kinematic velocity cap. Per occupied direction with
 * obstacle range d, the closing speed is limited to v_allow = sqrt(2*a*(d - D_STOP)),
 * reaching 0 exactly at CA3D_D_STOP. This replaces the fixed CA3D_SLOW_SCALE stair
 * with a continuous, speed-dependent cap that starts easing at d = D_STOP + v^2/(2a).
 *
 * Set to 0 to disable the curve AND the stop-line latch, restoring the exact legacy
 * SLOW-scale / STOP-exit behavior (rollout switch).
 *
 * Default 2.0 ~= 0.7 x MPC_ACC_HOR (cruise 3.0 m/s^2 in this fork; MPC_ACC_HOR_MAX
 * = 5.0 is the hard ceiling). The conservative cruise-based de-rating keeps the cap
 * slope well below the achievable decel so it stays trackable despite the velocity-
 * loop/attitude lag and the 20 Hz field rate. The same value is applied to the
 * vertical axis (up_cm/down_cm): MPC_ACC_UP_MAX = 4.0 (ceiling case OK), MPC_ACC_DOWN
 * _MAX = 3.0, both >= 2.0 so a single decel is trackable on every axis; down
 * obstacles are the encoder-excluded ground in practice.
 *
 * REVIEW (SITL, px4_sitl Gazebo iris, OFFBOARD velocity rig): the cap correctly
 * limits the setpoint to v_allow, but the vehicle tracks it with a growing velocity
 * lag (the sqrt curve's slope -> infinity at D_STOP), so it reaches the stop line
 * with residual speed and overshoots by ~0.6-1.2 m at 3 m/s across a in [0.8, 3.5]
 * (non-monotonic, run-to-run noise ~0.4 m). This is a controller-lag effect, not a
 * cap error. Per the C1 design ("report the measured numbers first; do NOT add
 * feedforward/jerk/predictive/margin"), the numbers are reported in tests/ and this
 * default is left at the source-derived value pending review of whether hardware
 * needs a lower a or a controller-side change. See tests/REPORT.md (C1 section).
 *
 * @min 0
 * @max 10
 * @decimal 2
 * @unit m/s^2
 * @group Collision Avoidance 3D
 */
PARAM_DEFINE_FLOAT(CA3D_BRK_ACC, 2.0f);

/**
 * Stop-line retreat hysteresis
 *
 * Distance the vehicle must physically retreat BEYOND CA3D_D_STOP before a latched
 * stop-line in that direction releases. Release requires BOTH the CA3D_STOP_EXIT
 * frame count AND the measured range in that direction exceeding D_STOP + this
 * margin. Kills the boundary-noise ratchet (creep through a wall under sustained
 * stick). Only active when CA3D_BRK_ACC > 0.
 *
 * Distinct from CA3D_HYST (state-machine zone hysteresis); this is the stricter
 * retreat margin for the per-direction stop-line latch.
 *
 * @min 0
 * @max 2
 * @decimal 2
 * @unit m
 * @group Collision Avoidance 3D
 */
PARAM_DEFINE_FLOAT(CA3D_D_HYST, 0.3f);

/**
 * Lateral blocking distance
 *
 * A cell counts as blocking a horizontal (X/Y) axis only if its dominant-axis
 * lateral distance is within this range. Mirrors the ROS block_xy_dist.
 *
 * @min 0.2
 * @max 10
 * @decimal 2
 * @unit m
 * @group Collision Avoidance 3D
 */
PARAM_DEFINE_FLOAT(CA3D_BLK_XY, 1.1f);

/**
 * Vertical blocking distance
 *
 * A cell counts as blocking the vertical (Z) axis only if its dominant-axis
 * vertical distance is within this range. Mirrors the ROS block_z_dist.
 *
 * @min 0.2
 * @max 10
 * @decimal 2
 * @unit m
 * @group Collision Avoidance 3D
 */
PARAM_DEFINE_FLOAT(CA3D_BLK_Z, 1.1f);

/**
 * Lock yaw in STOP
 *
 * When enabled, the yaw rate setpoint is zeroed while in the STOP state.
 * Disabled keeps yaw authority (matches ROS allow_yaw_in_stop=true).
 *
 * @boolean
 * @group Collision Avoidance 3D
 */
PARAM_DEFINE_INT32(CA3D_YAW_LOCK, 0);

/**
 * Field timeout
 *
 * Maximum age of the last valid field before it is considered stale and the
 * failsafe behavior (CA3D_FS_MODE) applies.
 *
 * @min 0.1
 * @max 5
 * @decimal 2
 * @unit s
 * @group Collision Avoidance 3D
 */
PARAM_DEFINE_FLOAT(CA3D_TIMEOUT, 0.5f);

/**
 * Failsafe mode on stale/absent field
 *
 * Behavior when active and airborne in a gated mode but the field is stale or
 * never received. 0: passthrough with throttled warning (development only).
 * 1: treat as STOP, zeroing finite velocity components.
 *
 * @value 0 Passthrough + warn
 * @value 1 Stop
 * @group Collision Avoidance 3D
 */
PARAM_DEFINE_INT32(CA3D_FS_MODE, 1);

/**
 * Mode gating bitmask
 *
 * Which flight modes the override is active in. bit0: manual position (POSCTL),
 * bit1: OFFBOARD, bit2: AUTO. Default 3 = POSCTL + OFFBOARD.
 *
 * @min 0
 * @max 7
 * @bit 0 Manual position (POSCTL)
 * @bit 1 Offboard
 * @bit 2 Auto
 * @group Collision Avoidance 3D
 */
PARAM_DEFINE_INT32(CA3D_MODE_MASK, 3);
