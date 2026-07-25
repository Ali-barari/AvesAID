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

#include "CollisionAvoidance3D.hpp"

#include <string.h>

using namespace matrix;

// direction slot (0 FWD,1 BACK,2 RIGHT,3 LEFT,4 DOWN,5 UP) -> canonical CA3D_DIR_* bit,
// so _latch_mask shares the same bit numbering as _dir_mask / dirMask().
static constexpr uint8_t CA3D_SLOT_BIT[6] = {
	CA3D_DIR_FWD, CA3D_DIR_BACK, CA3D_DIR_RIGHT, CA3D_DIR_LEFT, CA3D_DIR_DOWN, CA3D_DIR_UP
};

CollisionAvoidance3D::CollisionAvoidance3D(ModuleParams *parent) : ModuleParams(parent) {}

hrt_abstime CollisionAvoidance3D::getTime() { return hrt_absolute_time(); }

float CollisionAvoidance3D::vAllow(float d, float acc, float d_stop)
{
	if (!(acc > 0.f)) {
		return INFINITY; // curve disabled -> no cap
	}

	const float x = d - d_stop;

	if (x <= 0.f) {
		return 0.f; // at/inside the stop line -> no closing motion permitted
	}

	return sqrtf(2.f * acc * x);
}

void CollisionAvoidance3D::capClosing(matrix::Vector3f &v, const matrix::Vector3f &b_unit, float v_allow)
{
	const float v_close = v.dot(b_unit);

	if (v_close > v_allow) {
		v -= b_unit * (v_close - v_allow); // remove only the excess along b; tangential preserved
	}
}

bool CollisionAvoidance3D::_readField()
{
	mavlink_tunnel_s tunnel;
	bool got_valid = false;

	// drain the queue; keep the newest valid CA3D field
	while (_mavlink_tunnel_sub.update(&tunnel)) {
		if (tunnel.payload_type != CA3D_TUNNEL_PAYLOAD_TYPE) {
			continue;
		}

		if (tunnel.payload_length < CA3D_FIELD_V1_BYTES) {
			continue; // too small for even a legacy frame
		}

		// Version-aware decode. v1 (84 B) -> legacy ring-center reconstruction;
		// v2 (120 B) -> true per-cell elevation. Unknown version -> reject the
		// frame (it does not refresh _last_field_time, so it counts toward the
		// CA3D_TIMEOUT failsafe, exactly like a dropout).
		const uint8_t ver = tunnel.payload[1]; // version byte (offset 1, stable in v1/v2)
		size_t need;

		if (ver == CA3D_VERSION) {
			need = sizeof(ca3d_field_t);          // v2

		} else if (ver == CA3D_VERSION_LEGACY) {
			need = CA3D_FIELD_V1_BYTES;           // v1

		} else {
			continue;                             // unknown version -> reject
		}

		if (tunnel.payload_length < need) {
			continue; // truncated for its declared version
		}

		ca3d_field_t field;
		memset(&field, 0, sizeof(field)); // v1: el_deg[] stays 0 (unused on the legacy path)
		memcpy(&field, tunnel.payload, need);

		if (field.magic != CA3D_MAGIC) {
			continue;
		}

		if (!(field.flags & CA3D_FLAG_FIELD_VALID)) {
			continue;
		}

		// track dropped fields via the wrapping seq counter
		if (_have_field) {
			const uint8_t expected = (uint8_t)(_last_seq + 1);

			if (field.seq != expected) {
				_seq_lost += (uint16_t)((uint8_t)(field.seq - expected));
			}
		}

		_last_seq = field.seq;
		_field = field;
		_last_field_time = getTime();
		_have_field = true;
		got_valid = true;
	}

	return got_valid;
}

void CollisionAvoidance3D::_classify(uint8_t &dir_mask, float &min_dist, float dir_dist[CA3D_NUM_SLOTS]) const
{
	dir_mask = 0;
	float md = NAN;

	// per-slot nearest range (no blk gate) — feeds the kinematic cap + latch.
	// slots: 0 FWD 1 BACK 2 RIGHT 3 LEFT 4 DOWN 5 UP
	for (int i = 0; i < CA3D_NUM_SLOTS; ++i) {
		dir_dist[i] = NAN;
	}

	auto note_slot = [&dir_dist](int slot, float d) {
		dir_dist[slot] = PX4_ISFINITE(dir_dist[slot]) ? math::min(dir_dist[slot], d) : d;
	};

	const float blk_xy = _param_ca3d_blk_xy.get();
	const float blk_z  = _param_ca3d_blk_z.get();

	// elevation source: v2 carries each cell's TRUE elevation (el_deg); v1 (or any
	// legacy frame) falls back to the hardcoded ring-center bands -> bit-exact old
	// behavior. Reconstructing the true bearing lets the dominant-axis test place
	// steep terrain returns on DOWN and ceiling returns on UP instead of leaking to
	// the horizontal axes at the 45 deg band edges (fixes rough-terrain phantoms + D4).
	const bool use_true_el = (_field.version >= CA3D_VERSION);
	static const float el_center_deg[CA3D_NUM_EL] = { -45.f, 0.f, 45.f };

	for (int el = 0; el < CA3D_NUM_EL; ++el) {
		for (int az = 0; az < CA3D_NUM_AZ; ++az) {
			const uint16_t cm = _field.ring_cm[el][az];

			if (cm == CA3D_INVALID_CM || cm == 0) {
				continue;
			}

			const float dist = cm * 0.01f;
			md = PX4_ISFINITE(md) ? math::min(md, dist) : dist;

			// per-cell elevation (deg, + = up): true value (v2) or ring center (v1)
			const float e = math::radians(use_true_el ? (float)_field.el_deg[el][az]
							     : el_center_deg[el]);

			// pseudo-point in body FRD (x fwd, y right, z DOWN); elevation positive = UP
			const float a = math::radians((float)az * 30.f);
			const float px = dist * cosf(e) * cosf(a);
			const float py = dist * cosf(e) * sinf(a);
			const float pz = dist * (-sinf(e));

			const float horiz = math::max(fabsf(px), fabsf(py));

			if (fabsf(pz) > horiz) {
				// vertical-dominant cell
				note_slot((pz > 0.f) ? 4 /*DOWN*/ : 5 /*UP*/, dist);

				if (fabsf(pz) <= blk_z) {
					dir_mask |= (pz > 0.f) ? CA3D_DIR_DOWN : CA3D_DIR_UP;
				}

			} else {
				// horizontal-dominant cell — "a wall is a wall at any height"
				if (fabsf(px) >= fabsf(py)) {
					note_slot((px > 0.f) ? 0 /*FWD*/ : 1 /*BACK*/, dist);

				} else {
					note_slot((py > 0.f) ? 2 /*RIGHT*/ : 3 /*LEFT*/, dist);
				}

				if (horiz <= blk_xy) {
					if (fabsf(px) >= fabsf(py)) {
						dir_mask |= (px > 0.f) ? CA3D_DIR_FWD : CA3D_DIR_BACK;

					} else {
						dir_mask |= (py > 0.f) ? CA3D_DIR_RIGHT : CA3D_DIR_LEFT;
					}
				}
			}
		}
	}

	// up cap (straight up)
	if (_field.up_cm != CA3D_INVALID_CM && _field.up_cm != 0) {
		const float dist = _field.up_cm * 0.01f;
		md = PX4_ISFINITE(md) ? math::min(md, dist) : dist;
		note_slot(5 /*UP*/, dist);

		if (dist <= blk_z) {
			dir_mask |= CA3D_DIR_UP;
		}
	}

	// down cap (straight down; ground-surface inliers already excluded by the encoder)
	if (_field.down_cm != CA3D_INVALID_CM && _field.down_cm != 0) {
		const float dist = _field.down_cm * 0.01f;
		md = PX4_ISFINITE(md) ? math::min(md, dist) : dist;
		note_slot(4 /*DOWN*/, dist);

		if (dist <= blk_z) {
			dir_mask |= CA3D_DIR_DOWN;
		}
	}

	min_dist = md;
}

void CollisionAvoidance3D::_updateState(float min_dist)
{
	const float d_stop = _param_ca3d_d_stop.get();
	const float d_slow = _param_ca3d_d_slow.get();
	const float hyst   = _param_ca3d_hyst.get();
	const int32_t stop_exit = math::max((int32_t)1, _param_ca3d_stop_exit.get());

	// no valid cell => no obstacle
	const bool has_obstacle = PX4_ISFINITE(min_dist);

	switch (_state) {
	case State::SAFE:
		if (has_obstacle && min_dist < d_stop) {
			_state = State::STOP;

		} else if (has_obstacle && min_dist < d_slow) {
			_state = State::SLOW;
		}

		_stop_exit_count = 0;
		break;

	case State::SLOW:
		if (has_obstacle && min_dist < d_stop) {
			_state = State::STOP;

		} else if (!has_obstacle || min_dist >= d_slow + hyst) {
			_state = State::SAFE;
		}

		_stop_exit_count = 0;
		break;

	case State::STOP:

		// require CA3D_STOP_EXIT consecutive clear fields before releasing STOP
		if (!has_obstacle || min_dist >= d_stop + hyst) {
			_stop_exit_count++;

			if (_stop_exit_count >= stop_exit) {
				_state = (!has_obstacle || min_dist >= d_slow + hyst) ? State::SAFE : State::SLOW;
				_stop_exit_count = 0;
			}

		} else {
			_stop_exit_count = 0;
		}

		break;
	}
}

void CollisionAvoidance3D::_updateLatch()
{
	// Per-direction stop-line latch. Only active with the kinematic curve.
	if (!(_param_ca3d_brk_acc.get() > 0.f)) {
		_latch_mask = 0;

		for (int i = 0; i < CA3D_NUM_SLOTS; ++i) {
			_latch_clear[i] = 0;
		}

		return;
	}

	const float d_stop = _param_ca3d_d_stop.get();
	const float d_hyst = _param_ca3d_d_hyst.get();
	const int32_t stop_exit = math::max((int32_t)1, _param_ca3d_stop_exit.get());
	const float release_d = d_stop + d_hyst;

	for (int slot = 0; slot < CA3D_NUM_SLOTS; ++slot) {
		const float d = _dir_dist[slot];
		const uint8_t bit = CA3D_SLOT_BIT[slot];

		// entry: an obstacle inside the stop line in this direction latches it
		if (PX4_ISFINITE(d) && d < d_stop) {
			_latch_mask |= bit;
			_latch_clear[slot] = 0;
			continue;
		}

		if (_latch_mask & bit) {
			// release only after physically retreating past D_STOP + D_HYST
			// (or the obstacle vanishing) for STOP_EXIT consecutive frames.
			const bool retreated = !PX4_ISFINITE(d) || d >= release_d;

			if (retreated) {
				if (++_latch_clear[slot] >= stop_exit) {
					_latch_mask &= (uint8_t)~bit;
					_latch_clear[slot] = 0;
				}

			} else {
				_latch_clear[slot] = 0; // marginal frame at the boundary: no progress
			}
		}
	}
}

void CollisionAvoidance3D::_applyFilter(matrix::Vector3f &vel_frd, float &yaw_rate, uint8_t dir_mask)
{
	_intervening = false;

	// Legacy stair (CA3D_BRK_ACC = 0): exact original behavior.
	if (!(_param_ca3d_brk_acc.get() > 0.f)) {
		_applyFilterLegacy(vel_frd, yaw_rate, dir_mask);
		return;
	}

	// --- kinematic velocity cap + stop-line latch --------------------------
	// Runs every cycle (not gated by SAFE): the cap self-gates by distance and
	// the latch must persist even while the global zone reads SAFE/SLOW.
	const float acc    = _param_ca3d_brk_acc.get();
	const float d_stop = _param_ca3d_d_stop.get();

	// pos slot = axis*2, neg slot = axis*2+1  (0 FWD 1 BACK 2 RIGHT 3 LEFT 4 DOWN 5 UP)
	for (int i = 0; i < 3; ++i) {
		float &v = vel_frd(i);

		if (!PX4_ISFINITE(v) || fabsf(v) <= FLT_EPSILON) {
			continue;
		}

		const int slot = (v > 0.f) ? (i * 2) : (i * 2 + 1);

		// (1) kinematic cap: limit the closing speed to v_allow(range). Scalar,
		// NaN-safe specialization of capClosing() for an axis-aligned bearing.
		const float d = _dir_dist[slot];

		if (PX4_ISFINITE(d)) {
			const float va = vAllow(d, acc, d_stop);

			if (fabsf(v) > va) {
				v = (v > 0.f) ? va : -va;
				_intervening = true;
			}
		}

		// (2) latch: while a stop-line is latched in the closing direction, the
		// closing component is clamped to <= 0 (away/reverse stays allowed).
		if (_latch_mask & CA3D_SLOT_BIT[slot]) {
			if (fabsf(v) > FLT_EPSILON) {
				v = 0.f;
				_intervening = true;
			}
		}
	}

	// yaw is only ever zeroed, and only in STOP with CA3D_YAW_LOCK (unchanged)
	if (_state == State::STOP && _param_ca3d_yaw_lock.get() != 0 && PX4_ISFINITE(yaw_rate)
	    && fabsf(yaw_rate) > FLT_EPSILON) {
		yaw_rate = 0.f;
		_intervening = true;
	}
}

void CollisionAvoidance3D::_applyFilterLegacy(matrix::Vector3f &vel_frd, float &yaw_rate, uint8_t dir_mask)
{
	_intervening = false;

	if (_state == State::SAFE) {
		return; // passthrough
	}

	const float slow_scale = math::constrain(_param_ca3d_slow_scale.get(), 0.f, 1.f);
	const float factor = (_state == State::STOP) ? 0.f : slow_scale;

	if (dir_mask == 0) {
		// global fallback: state says danger but it can't be localized -> constrain all finite axes
		for (int i = 0; i < 3; ++i) {
			if (PX4_ISFINITE(vel_frd(i)) && fabsf(vel_frd(i)) > 0.f) {
				vel_frd(i) *= factor;
				_intervening = true;
			}
		}

	} else {
		// per-axis: only constrain an axis when moving INTO a blocked direction
		// vel_frd: (0)=forward +X, (1)=right +Y_FRD, (2)=down +Z_FRD
		const uint8_t pos_bit[3] = { CA3D_DIR_FWD,  CA3D_DIR_RIGHT, CA3D_DIR_DOWN };
		const uint8_t neg_bit[3] = { CA3D_DIR_BACK, CA3D_DIR_LEFT,  CA3D_DIR_UP   };

		for (int i = 0; i < 3; ++i) {
			float &v = vel_frd(i);

			if (!PX4_ISFINITE(v) || fabsf(v) <= FLT_EPSILON) {
				continue;
			}

			const uint8_t bit = (v > 0.f) ? pos_bit[i] : neg_bit[i];

			if (dir_mask & bit) {
				v *= factor;
				_intervening = true;
			}
		}
	}

	// yaw is only ever zeroed, and only in STOP with CA3D_YAW_LOCK
	if (_state == State::STOP && _param_ca3d_yaw_lock.get() != 0 && PX4_ISFINITE(yaw_rate)
	    && fabsf(yaw_rate) > FLT_EPSILON) {
		yaw_rate = 0.f;
		_intervening = true;
	}
}

bool CollisionAvoidance3D::_modeGated(const vehicle_control_mode_s &cm) const
{
	const int32_t mask = _param_ca3d_mode_mask.get();

	if ((mask & 1) && cm.flag_control_manual_enabled && cm.flag_control_position_enabled) {
		return true; // POSCTL
	}

	if ((mask & 2) && cm.flag_control_offboard_enabled) {
		return true; // OFFBOARD
	}

	if ((mask & 4) && cm.flag_control_auto_enabled) {
		return true; // AUTO
	}

	return false;
}

void CollisionAvoidance3D::modifySetpoint3D(matrix::Vector3f &vel_sp_ned, float &yaw_rate_sp,
		const matrix::Quatf &q_att, const vehicle_control_mode_s &control_mode, bool landed)
{
	_intervening = false;

	// disabled, disarmed, on ground, or mode not gated -> bit-exact passthrough
	if (!isActive() || !control_mode.flag_armed || landed || !_modeGated(control_mode)) {
		_readField(); // keep seq/staleness tracking warm even when passive
		_publishStatus(false, _have_field ? (uint32_t)((getTime() - _last_field_time) / 1000) : UINT32_MAX,
			       false);
		return;
	}

	_readField();

	const hrt_abstime now = getTime();
	const uint64_t timeout_us = (uint64_t)(math::max(0.05f, _param_ca3d_timeout.get()) * 1e6f);
	const bool stale = !_have_field || (now - _last_field_time) > timeout_us;
	const uint32_t age_ms = _have_field ? (uint32_t)((now - _last_field_time) / 1000) : UINT32_MAX;

	if (stale) {
		// failsafe: no fresh field while active and airborne in a gated mode
		if (_param_ca3d_fs_mode.get() == 1) {
			_state = State::STOP;
			_dir_mask = 0;

			// No distance measurement: latch every direction. Recovery is the
			// existing path (fresh fields -> retreat + STOP_EXIT via _updateLatch).
			if (_param_ca3d_brk_acc.get() > 0.f) {
				_latch_mask = 0x3F; // all 6 slots
			}

			for (int i = 0; i < 3; ++i) {
				if (PX4_ISFINITE(vel_sp_ned(i)) && fabsf(vel_sp_ned(i)) > FLT_EPSILON) {
					vel_sp_ned(i) = 0.f;
					_intervening = true;
				}
			}

			if (now - _last_fs_warn > FS_WARN_INTERVAL_US) {
				mavlink_log_critical(&_mavlink_log_pub, "CA3D: field stale, STOP\t");
				_last_fs_warn = now;
			}

		} else {
			// passthrough + warn (development only)
			if (now - _last_fs_warn > FS_WARN_INTERVAL_US) {
				mavlink_log_warning(&_mavlink_log_pub, "CA3D: field stale, passthrough\t");
				_last_fs_warn = now;
			}
		}

		_publishStatus(true, age_ms, false);
		return;
	}

	// NED -> body FRD (q_att rotates body -> NED)
	const Dcmf R_body_to_ned(q_att);
	Vector3f vel_frd = R_body_to_ned.transpose() * vel_sp_ned;

	uint8_t dir_mask = 0;
	float min_dist = NAN;
	_classify(dir_mask, min_dist, _dir_dist);
	_updateState(min_dist);
	_updateLatch();
	_dir_mask = dir_mask;
	_min_dist = min_dist;

	_applyFilter(vel_frd, yaw_rate_sp, dir_mask);

	// back to NED, preserving NaN (untouched) components
	const Vector3f vel_ned_new = R_body_to_ned * vel_frd;

	for (int i = 0; i < 3; ++i) {
		if (PX4_ISFINITE(vel_sp_ned(i))) {
			vel_sp_ned(i) = vel_ned_new(i);
		}
	}

	_publishStatus(true, age_ms, true);
}

void CollisionAvoidance3D::_publishStatus(bool active, uint32_t field_age_ms, bool field_valid)
{
	ca3d_status_s s{};
	s.timestamp = getTime();
	s.state = (uint8_t)_state;
	s.dir_mask = _dir_mask;
	s.min_dist = _min_dist;
	s.field_age_ms = field_age_ms;
	s.has_cm = _have_field ? _field.has_cm : (int16_t)CA3D_HAS_INVALID;
	s.seq_lost = _seq_lost;
	s.field_seq = _last_seq;
	s.active = active;
	s.intervening = _intervening;
	s.field_valid = field_valid;
	_ca3d_status_pub.publish(s);
}
