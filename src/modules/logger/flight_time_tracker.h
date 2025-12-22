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
 * AvesAID: Flight time tracker - accumulates total flight time and persists
 * to SYS_TOT_FLT_TIME parameter. Publishes total hours via debug_key_value
 * for MAVLink NAMED_VALUE_FLOAT stream (key: TOT_FLT_H).
 */

#pragma once

#include <parameters/param.h>
#include <drivers/drv_hrt.h>
#include <uORB/Publication.hpp>
#include <uORB/Subscription.hpp>
#include <uORB/topics/debug_key_value.h>
#include <uORB/topics/vehicle_land_detected.h>
#include <uORB/topics/vehicle_status.h>

namespace px4
{
namespace logger
{

class FlightTimeTracker
{
public:
	FlightTimeTracker();
	~FlightTimeTracker() = default;

	void init();
	void update();

private:
	void commit();

	uORB::Subscription _vehicle_land_detected_sub{ORB_ID(vehicle_land_detected)};
	uORB::Subscription _vehicle_status_sub{ORB_ID(vehicle_status)};
	uORB::Publication<debug_key_value_s> _debug_pub{ORB_ID(debug_key_value)};

	vehicle_land_detected_s _vehicle_land_detected{};
	vehicle_status_s _vehicle_status{};

	param_t _param_handle{PARAM_INVALID};
	uint32_t _session_s{0};
	int32_t _total_committed_s{0};
	hrt_abstime _last_update{0};
	hrt_abstime _last_debug_pub{0};
	bool _prev_landed{true};
	bool _initialized{false};
};

} // namespace logger
} // namespace px4
