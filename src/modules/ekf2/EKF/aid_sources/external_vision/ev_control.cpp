/****************************************************************************
 *
 *   Copyright (c) 2022 PX4 Development Team. All rights reserved.
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
 * @file ev_control.cpp
 * Control functions for ekf external vision control
 */

#ifndef MODULE_NAME
#define MODULE_NAME "ev_control"
#endif
#include <px4_platform_common/events.h>

#include "ekf.h"

void Ekf::controlExternalVisionFusion(const imuSample &imu_sample)
{
	_ev_pos_b_est.predict(_dt_ekf_avg);
	_ev_hgt_b_est.predict(_dt_ekf_avg);

	// Check for new external vision data
	extVisionSample ev_sample;

	// AvesAID: Track EV fusion state for START/STOP logging
	static bool ev_was_active = false;

	if (_ext_vision_buffer && _ext_vision_buffer->pop_first_older_than(imu_sample.time_us, &ev_sample)) {

		bool ev_reset = (ev_sample.reset_counter != _ev_sample_prev.reset_counter);

		// determine if we should use the horizontal position observations
		bool quality_sufficient = (_params.ev_quality_minimum <= 0) || (ev_sample.quality >= _params.ev_quality_minimum);

		const bool starting_conditions_passing = quality_sufficient
				&& ((ev_sample.time_us - _ev_sample_prev.time_us) < EV_MAX_INTERVAL)
				&& ((_params.ev_quality_minimum <= 0)
				    || (_ev_sample_prev.quality >= _params.ev_quality_minimum)) // previous quality sufficient
				&& ((_params.ev_quality_minimum <= 0)
				    || (_ext_vision_buffer->get_newest().quality >= _params.ev_quality_minimum)) // newest quality sufficient
				&& isNewestSampleRecent(_time_last_ext_vision_buffer_push, EV_MAX_INTERVAL);

		// AvesAID: Log when SLAM fusion starts
		if (starting_conditions_passing && !ev_was_active) {
			mavlink_log_info(&_mavlink_log_pub, "AvesAID: External Vision Fusion Started");
			events::send(events::ID("ekf2_ev_data_started"), events::Log::Info,
				"AvesAID: External Vision Fusion Started");
			ev_was_active = true;
		}

		updateEvAttitudeErrorFilter(ev_sample, ev_reset);
		controlEvYawFusion(imu_sample, ev_sample, starting_conditions_passing, ev_reset, quality_sufficient, _aid_src_ev_yaw);
		controlEvVelFusion(imu_sample, ev_sample, starting_conditions_passing, ev_reset, quality_sufficient, _aid_src_ev_vel);
		controlEvPosFusion(imu_sample, ev_sample, starting_conditions_passing, ev_reset, quality_sufficient, _aid_src_ev_pos);
		controlEvHeightFusion(imu_sample, ev_sample, starting_conditions_passing, ev_reset, quality_sufficient,
				      _aid_src_ev_hgt);

		if (quality_sufficient) {
			_ev_sample_prev = ev_sample;
		}

	} else if ((_control_status.flags.ev_pos || _control_status.flags.ev_vel || _control_status.flags.ev_yaw
		    || _control_status.flags.ev_hgt)
		   && isTimedOut(_ev_sample_prev.time_us, 5 * EV_MAX_INTERVAL)) {  // 5 * 200ms = 1000ms timeout to match SLAM health check

		// Turn off EV fusion mode if no data has been received
		stopEvPosFusion();
		stopEvVelFusion();
		stopEvYawFusion();
		stopEvHgtFusion();

		_ev_q_error_initialized = false;
		ev_was_active = false;  // AvesAID: Reset for next External Vision session

		// AvesAID: Force immediate failsafe when EV (SLAM) feed stops
		//
		// WORKFLOW:
		// 1. These timestamps are checked in ekf_helper.cpp::updateHorizontalDeadReckoningstatus()
		// 2. Setting them to 0 makes isRecent() checks fail immediately
		// 3. This sets inertial_dead_reckoning = true (line 858 ekf_helper.cpp)
		// 4. Also triggers _horizontal_deadreckon_time_exceeded = true (line 859 ekf_helper.cpp)
		// 5. Dead reckoning flags are published to Commander via vehicle_local_position.dead_reckoning
		// 6. estimatorCheck.cpp sets failsafe_flags.local_position_invalid = true
		// 7. failsafe.cpp::modeCanRun(POSCTL) returns false (line 682 framework.cpp)
		// 8. Failsafe triggered: POSITION mode → ALTITUDE mode (line 594-598 failsafe.cpp)
		//
		// Result: Immediate mode switch instead of waiting 5 seconds for deadreckoning timeout
		_time_last_hor_pos_fuse = 0;        // Invalidate horizontal position fusion timestamp
		_time_last_hor_vel_fuse = 0;        // Invalidate horizontal velocity fusion timestamp
		_time_last_horizontal_aiding = 0;   // Trigger immediate deadreckon timeout (skips 5sec valid_timeout_max)

		// AvesAID: Log SLAM stopped (both mavlink for old QGC and events for new)
		mavlink_log_warning(&_mavlink_log_pub, "AvesAID: External Vision data stopped");
		// AvesAID: Send event for new event logging system
		events::send(events::ID("ekf2_ev_data_stopped"), events::Log::Warning,
			"AvesAID: External Vision data stopped");
	}
}

void Ekf::updateEvAttitudeErrorFilter(extVisionSample &ev_sample, bool ev_reset)
{
	const Quatf q_error((_state.quat_nominal * ev_sample.quat.inversed()).normalized());

	if (!q_error.isAllFinite()) {
		return;
	}

	if (!_ev_q_error_initialized || ev_reset) {
		_ev_q_error_filt.reset(q_error);
		_ev_q_error_initialized = true;

	} else {
		_ev_q_error_filt.update(q_error);
	}
}
