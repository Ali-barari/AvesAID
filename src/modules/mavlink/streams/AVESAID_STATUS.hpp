/****************************************************************************
 *
 *   Copyright (c) 2020 PX4 Development Team. All rights reserved.
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

#ifndef AVESAID_STATUS_HPP
#define AVESAID_STATUS_HPP

#include <uORB/topics/avesaid_status.h>

class MavlinkStreamAvesaidStatus : public MavlinkStream
{
public:
    static MavlinkStream *new_instance(Mavlink *mavlink) { return new MavlinkStreamAvesaidStatus(mavlink); }

    static constexpr const char *get_name_static() { return "AVESAID_STATUS"; }
    static constexpr uint16_t get_id_static() { return MAVLINK_MSG_ID_AVESAID_STATUS; }

    const char *get_name() const override { return get_name_static(); }
    uint16_t get_id() override { return get_id_static(); }

    unsigned get_size() override
    {
        return _avesaid_status_sub.advertised() ? MAVLINK_MSG_ID_AVESAID_STATUS_LEN + MAVLINK_NUM_NON_PAYLOAD_BYTES : 0;
	// return MAVLINK_MSG_ID_AVESAID_STATUS_LEN + MAVLINK_NUM_NON_PAYLOAD_BYTES;
    }
private:
    explicit MavlinkStreamAvesaidStatus(Mavlink *mavlink) : MavlinkStream(mavlink) {}

    uORB::Subscription _avesaid_status_sub{ORB_ID(avesaid_status)};

    bool send() override
    {
        avesaid_status_s avesaid_status{};

        // Copy the latest data from the ORB topic
        if (_avesaid_status_sub.update(&avesaid_status)) {
            // Populate the MAVLink message
            mavlink_avesaid_status_t msg{};

            msg.timestamp = avesaid_status.timestamp;
            msg.flag_mode_attachment_enabled = avesaid_status.flag_mode_attachment_enabled;
            msg.flag_mode_partial_attachment_enabled = avesaid_status.flag_mode_partial_attachment_enabled;
            msg.flag_magnet_enabled = avesaid_status.flag_magnet_enabled;
            msg.flag_height_source_baro_enabled = avesaid_status.flag_height_source_baro_enabled;
            msg.flag_height_source_slam_enabled = avesaid_status.flag_height_source_slam_enabled;

            // Send the message
            mavlink_msg_avesaid_status_send_struct(_mavlink->get_channel(), &msg);

            return true;
        }

        return false;
    }
};

#endif // AVESAID_STATUS_HPP
