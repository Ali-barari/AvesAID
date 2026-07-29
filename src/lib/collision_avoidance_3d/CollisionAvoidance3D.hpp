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
 * @file CollisionAvoidance3D.hpp
 *
 * Iron Sphere native 3D collision avoidance (velocity-override) library.
 *
 * The ROS companion streams a compact 3D obstacle distance field (ca3d_field_t)
 * over a stock MAVLink TUNNEL message. PX4 decodes it from the uORB mavlink_tunnel
 * topic, reconstructs the 6-direction semantics with the exact dominant-axis rules
 * from the original ROS supervisor, runs a SAFE/SLOW/STOP state machine with
 * hysteresis + stop-exit confirmations, and constrains the velocity/yaw-rate
 * setpoint just before it enters the multicopter position controller. This covers
 * POSCTL / OFFBOARD / AUTO from a single call site.
 *
 * Safety invariants (see §4 of the design):
 *   - only ever CONSTRAINS motion: |out| <= |in| per axis, never flips sign,
 *     never injects motion, never touches attitude/rates other than optional
 *     yaw-rate zeroing, passes NaN components through untouched;
 *   - never modifies anything when disarmed or CA3D_EN=0.
 */

#pragma once

#include <float.h>

#include <drivers/drv_hrt.h>
#include <mathlib/mathlib.h>
#include <matrix/matrix/math.hpp>
#include <px4_platform_common/module_params.h>
#include <systemlib/mavlink_log.h>
#include <uORB/Publication.hpp>
#include <uORB/Subscription.hpp>
#include <uORB/topics/ca3d_status.h>
#include <uORB/topics/mavlink_tunnel.h>
#include <uORB/topics/vehicle_control_mode.h>

#include "ca3d_tunnel_payload.h"

using namespace time_literals;

class CollisionAvoidance3D : public ModuleParams
{
public:
	CollisionAvoidance3D(ModuleParams *parent);
	~CollisionAvoidance3D() override = default;

	enum class State : uint8_t { SAFE = 0, SLOW = 1, STOP = 2 };

	/** Master enable (CA3D_EN). When false the library never modifies setpoints. */
	bool isActive() { return _param_ca3d_en.get() != 0; }

	/**
	 * Constrain a local-NED velocity setpoint (and yaw-rate) in place.
	 * @param vel_sp_ned   velocity setpoint in local NED [m/s] (NaN components pass through)
	 * @param yaw_rate_sp  yaw-rate setpoint [rad/s] (only ever zeroed, only in STOP w/ CA3D_YAW_LOCK)
	 * @param q_att        attitude quaternion (FRD body -> NED)
	 * @param control_mode used for per-mode gating (CA3D_MODE_MASK)
	 * @param landed       true if the land detector reports landed (skip while on ground)
	 */
	void modifySetpoint3D(matrix::Vector3f &vel_sp_ned, float &yaw_rate_sp,
			      const matrix::Quatf &q_att,
			      const vehicle_control_mode_s &control_mode,
			      bool landed);

	// Per-direction slots, aligned with the 3 body-FRD axes used by _applyFilter:
	//   0 FWD(+X)  1 BACK(-X)  2 RIGHT(+Y)  3 LEFT(-Y)  4 DOWN(+Z)  5 UP(-Z)
	// slot = axis*2 + (moving_negative ? 1 : 0). One bit per slot in the latch mask.
	static constexpr int CA3D_NUM_SLOTS = 6;

	// --- pure kinematic-cap primitives (unit-testable, no state) ---------------
	/** Allowed closing speed at range d for braking decel acc, reaching 0 at d_stop.
	 *  acc <= 0 disables the cap (returns +inf). */
	static float vAllow(float d, float acc, float d_stop);
	/** Reduce only the component of v along unit bearing b to at most v_allow;
	 *  the tangential part is preserved. Generalized (oblique) form of the cap;
	 *  the runtime path specializes this per axis (NaN-safe). */
	static void  capClosing(matrix::Vector3f &v, const matrix::Vector3f &b_unit, float v_allow);

	// introspection (unit tests / logging)
	State   state() const { return _state; }
	uint8_t dirMask() const { return _dir_mask; }
	float   minDist() const { return _min_dist; }
	bool    intervening() const { return _intervening; }
	uint8_t latchMask() const { return _latch_mask; }
	float   dirDist(int slot) const { return (slot >= 0 && slot < CA3D_NUM_SLOTS) ? _dir_dist[slot] : NAN; }

protected:
	// mockable clock (mirrors CollisionPrevention so tests can drive staleness)
	virtual hrt_abstime getTime();

	// core steps, split out so the test can exercise them deterministically
	bool _readField();                                     ///< drain mavlink_tunnel -> _field, track seq
	void _classify(uint8_t &dir_mask, float &min_dist,
		       float dir_dist[CA3D_NUM_SLOTS]) const; ///< dominant-axis reconstruction + per-slot ranges
	void _updateState(float min_dist);                     ///< hysteresis + stop-exit (global zone machine)
	void _updateLatch();                                   ///< per-direction stop-line latch (needs _dir_dist)
	void _applyFilter(matrix::Vector3f &vel_frd, float &yaw_rate, uint8_t dir_mask);
	void _applyFilterLegacy(matrix::Vector3f &vel_frd, float &yaw_rate, uint8_t dir_mask);

private:
	bool _modeGated(const vehicle_control_mode_s &cm) const;
	void _publishStatus(bool active, uint32_t field_age_ms, bool field_valid);

	// decoded field state
	ca3d_field_t _field {};
	bool         _have_field{false};   ///< a valid field has ever been received
	hrt_abstime  _last_field_time{0};
	uint8_t      _last_seq{0};
	uint16_t     _seq_lost{0};

	// state machine
	State   _state{State::SAFE};
	uint8_t _dir_mask{0};
	float   _min_dist{NAN};
	int32_t _stop_exit_count{0};
	bool    _intervening{false};

	// kinematic cap + stop-line latch (active only when CA3D_BRK_ACC > 0)
	float   _dir_dist[CA3D_NUM_SLOTS] {NAN, NAN, NAN, NAN, NAN, NAN}; ///< nearest range per slot, no blk gate
	uint8_t _latch_mask{0};                                          ///< one bit per latched slot
	int32_t _latch_clear[CA3D_NUM_SLOTS] {0, 0, 0, 0, 0, 0};         ///< consecutive retreat-clear frames per slot

	hrt_abstime _last_fs_warn{0};
	static constexpr uint64_t FS_WARN_INTERVAL_US{1_s};

	orb_advert_t _mavlink_log_pub{nullptr};

	uORB::Subscription _mavlink_tunnel_sub{ORB_ID(mavlink_tunnel)};
	uORB::Publication<ca3d_status_s> _ca3d_status_pub{ORB_ID(ca3d_status)};

	DEFINE_PARAMETERS(
		(ParamInt<px4::params::CA3D_EN>)          _param_ca3d_en,
		(ParamFloat<px4::params::CA3D_D_STOP>)    _param_ca3d_d_stop,
		(ParamFloat<px4::params::CA3D_D_SLOW>)    _param_ca3d_d_slow,
		(ParamFloat<px4::params::CA3D_HYST>)      _param_ca3d_hyst,
		(ParamInt<px4::params::CA3D_STOP_EXIT>)   _param_ca3d_stop_exit,
		(ParamFloat<px4::params::CA3D_SLOW_SCALE>) _param_ca3d_slow_scale,
		(ParamFloat<px4::params::CA3D_BRK_ACC>)   _param_ca3d_brk_acc,
		(ParamFloat<px4::params::CA3D_D_HYST>)    _param_ca3d_d_hyst,
		(ParamFloat<px4::params::CA3D_BLK_XY>)    _param_ca3d_blk_xy,
		(ParamFloat<px4::params::CA3D_BLK_Z>)     _param_ca3d_blk_z,
		(ParamInt<px4::params::CA3D_YAW_LOCK>)    _param_ca3d_yaw_lock,
		(ParamFloat<px4::params::CA3D_TIMEOUT>)   _param_ca3d_timeout,
		(ParamInt<px4::params::CA3D_FS_MODE>)     _param_ca3d_fs_mode,
		(ParamInt<px4::params::CA3D_MODE_MASK>)   _param_ca3d_mode_mask
	)
};
