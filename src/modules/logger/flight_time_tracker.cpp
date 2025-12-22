/****************************************************************************
 *
 *   Copyright (c) 2024 AvesAID Development Team. All rights reserved.
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
 * 3. Neither the name AvesAID nor the names of its contributors may be
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
 * AvesAID: Flight time tracker implementation
 */

#include "flight_time_tracker.h"
#include <string.h>

namespace px4
{
namespace logger
{

FlightTimeTracker::FlightTimeTracker()
{
}

void FlightTimeTracker::init()
{
	if (_initialized) {
		return;
	}

	// AvesAID: Find and load the SYS_TOT_FLT_TIME parameter
	_param_handle = param_find("SYS_TOT_FLT_TIME");

	if (_param_handle != PARAM_INVALID) {
		int32_t tot_s{0};
		(void) param_get(_param_handle, &tot_s);
		_total_committed_s = tot_s > 0 ? tot_s : 0;
	}

	_initialized = true;
}

void FlightTimeTracker::update()
{
	if (!_initialized) {
		init();
	}

	// AvesAID: Update subscriptions
	_vehicle_land_detected_sub.update(&_vehicle_land_detected);
	_vehicle_status_sub.update(&_vehicle_status);

	const hrt_abstime now = hrt_absolute_time();
	const bool armed = (_vehicle_status.arming_state == vehicle_status_s::ARMING_STATE_ARMED);
	const bool landed = _vehicle_land_detected.landed;

	// AvesAID: Track state transitions
	const bool was_landed = _prev_landed;
	_prev_landed = landed;

	// AvesAID: Initialize timestamp on first armed run
	if (armed && _last_update == 0) {
		_last_update = now;
	}

	// AvesAID: Accumulate flight time when armed and in-air
	if (armed && _last_update > 0) {
		const float dt_s = (now - _last_update) * 1e-6f;
		_last_update = now;

		if (!landed && dt_s > 0.f && dt_s < 1.0f) { // AvesAID: sanity check dt
			_session_s += (uint32_t)dt_s;

			// AvesAID: Periodic commit every 60s
			if (_session_s >= 60) {
				commit();
			}
		}

		// AvesAID: Commit on landing transition
		if (!was_landed && landed) {
			commit();
		}
	}

	// AvesAID: Reset timestamp when disarmed
	if (!armed) {
		_last_update = 0;
	}

	// AvesAID: Publish debug value at 1 Hz for MAVLink NAMED_VALUE_FLOAT
	if ((now - _last_debug_pub) >= 1000000) { // 1 second in usec
		_last_debug_pub = now;
		const int32_t total_s = _total_committed_s + (int32_t)_session_s;
		const float total_hours = (float)total_s / 3600.0f;

		debug_key_value_s dbg{};
		dbg.timestamp = now;
		const char key[] = "TOT_FLT_H";
		memcpy(dbg.key, key, sizeof(dbg.key));
		dbg.key[sizeof(dbg.key) - 1] = '\0';
		dbg.value = total_hours;
		_debug_pub.publish(dbg);
	}
}

void FlightTimeTracker::commit()
{
	if (_param_handle == PARAM_INVALID || _session_s == 0) {
		return;
	}

	// AvesAID: Monotonic commit - only increase from our last committed state
	_total_committed_s += (int32_t)_session_s;
	(void) param_set(_param_handle, &_total_committed_s);
	(void) param_save_default(true);
	_session_s = 0;
}

} // namespace logger
} // namespace px4
