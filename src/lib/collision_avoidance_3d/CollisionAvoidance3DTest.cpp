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

// to run: make tests TESTFILTER=CollisionAvoidance3D

#include <gtest/gtest.h>
#include <string.h>

#include "CollisionAvoidance3D.hpp"

#include <parameters/param.h>
#include <uORB/uORB.h>
#include <uORB/topics/mavlink_tunnel.h>
#include <uORB/topics/vehicle_control_mode.h>

using namespace matrix;
using State = CollisionAvoidance3D::State;

static hrt_abstime g_mock_time = 1000000;
static orb_advert_t g_tunnel_pub = nullptr;

// -- test harness -----------------------------------------------------------

class TestCA3D : public CollisionAvoidance3D
{
public:
	TestCA3D() : CollisionAvoidance3D(nullptr) {}
protected:
	hrt_abstime getTime() override { return g_mock_time; }
};

class CollisionAvoidance3DTest : public ::testing::Test
{
public:
	void SetUp() override
	{
		param_control_autosave(false);
		param_reset_all();
		g_mock_time = 1000000;

		// enabled defaults for the majority of tests
		setI("CA3D_EN", 1);
		setF("CA3D_D_STOP", 1.1f);
		setF("CA3D_D_SLOW", 2.0f);
		setF("CA3D_HYST", 0.1f);
		setI("CA3D_STOP_EXIT", 3);
		setF("CA3D_SLOW_SCALE", 0.35f);
		// existing regression tests pin the legacy stair; kinematic tests opt in
		setF("CA3D_BRK_ACC", 0.f);
		setF("CA3D_D_HYST", 0.3f);
		setF("CA3D_BLK_XY", 1.1f);
		setF("CA3D_BLK_Z", 1.1f);
		setI("CA3D_YAW_LOCK", 0);
		setF("CA3D_TIMEOUT", 0.5f);
		setI("CA3D_FS_MODE", 1);
		setI("CA3D_MODE_MASK", 7); // all modes gated on for tests
	}

	static void setI(const char *n, int32_t v) { param_set(param_find(n), &v); }
	static void setF(const char *n, float v) { param_set(param_find(n), &v); }
};

// -- helpers ----------------------------------------------------------------

static ca3d_field_t clearField(uint8_t seq = 0)
{
	ca3d_field_t f{};
	f.magic = CA3D_MAGIC;
	f.version = CA3D_VERSION;
	f.seq = seq;
	f.flags = CA3D_FLAG_FIELD_VALID | CA3D_FLAG_GROUND_PLANE_VALID;
	f.has_cm = 150;
	f.max_range_cm = 400;
	f.up_cm = CA3D_INVALID_CM;
	f.down_cm = CA3D_INVALID_CM;

	// v2 baseline: el_deg at the ring-band centers, so a clearField frame reconstructs
	// identically to the legacy ring-center path. Tests that need a true elevation set
	// el_deg[e][a] explicitly alongside ring_cm.
	static const int8_t el_center[CA3D_NUM_EL] = { -45, 0, 45 };

	for (int e = 0; e < CA3D_NUM_EL; ++e) {
		for (int a = 0; a < CA3D_NUM_AZ; ++a) {
			f.ring_cm[e][a] = CA3D_INVALID_CM;
			f.el_deg[e][a] = el_center[e];
		}
	}

	return f;
}

static void publishField(const ca3d_field_t &f, uint8_t plen = sizeof(ca3d_field_t))
{
	mavlink_tunnel_s t{};
	t.timestamp = g_mock_time;
	t.payload_type = CA3D_TUNNEL_PAYLOAD_TYPE;
	t.payload_length = plen;
	memcpy(t.payload, &f, sizeof(f));

	if (g_tunnel_pub == nullptr) {
		g_tunnel_pub = orb_advertise(ORB_ID(mavlink_tunnel), &t);

	} else {
		orb_publish(ORB_ID(mavlink_tunnel), g_tunnel_pub, &t);
	}
}

static vehicle_control_mode_s posctl()
{
	vehicle_control_mode_s cm{};
	cm.flag_armed = true;
	cm.flag_control_manual_enabled = true;
	cm.flag_control_position_enabled = true;
	cm.flag_multicopter_position_control_enabled = true;
	return cm;
}

static vehicle_control_mode_s autoMode()
{
	vehicle_control_mode_s cm{};
	cm.flag_armed = true;
	cm.flag_control_auto_enabled = true;
	return cm;
}

// -- tests ------------------------------------------------------------------

TEST_F(CollisionAvoidance3DTest, instantiation) { CollisionAvoidance3D ca(nullptr); }

TEST_F(CollisionAvoidance3DTest, disabledIsBitExactPassthrough)
{
	setI("CA3D_EN", 0);
	TestCA3D ca;
	Vector3f v(2.f, 1.f, -1.f);
	float yr = 0.5f;
	Quatf q;
	ca.modifySetpoint3D(v, yr, q, posctl(), false);
	EXPECT_FLOAT_EQ(v(0), 2.f);
	EXPECT_FLOAT_EQ(v(1), 1.f);
	EXPECT_FLOAT_EQ(v(2), -1.f);
	EXPECT_FLOAT_EQ(yr, 0.5f);
	EXPECT_FALSE(ca.intervening());
}

TEST_F(CollisionAvoidance3DTest, landedIsPassthrough)
{
	TestCA3D ca;
	Vector3f v(2.f, 0.f, 0.f);
	float yr = 0.f;
	Quatf q;
	ca.modifySetpoint3D(v, yr, q, posctl(), /*landed*/ true);
	EXPECT_FLOAT_EQ(v(0), 2.f);
	EXPECT_FALSE(ca.intervening());
}

TEST_F(CollisionAvoidance3DTest, staleFieldFailsafeStops)
{
	TestCA3D ca; // enabled, but no field ever published
	Vector3f v(2.f, -1.f, 0.5f);
	float yr = 0.f;
	Quatf q;
	ca.modifySetpoint3D(v, yr, q, posctl(), false);
	EXPECT_FLOAT_EQ(v(0), 0.f);
	EXPECT_FLOAT_EQ(v(1), 0.f);
	EXPECT_FLOAT_EQ(v(2), 0.f);
	EXPECT_TRUE(ca.intervening());
	EXPECT_EQ(ca.state(), State::STOP);
}

TEST_F(CollisionAvoidance3DTest, staleFieldPassthroughMode)
{
	setI("CA3D_FS_MODE", 0);
	TestCA3D ca;
	Vector3f v(2.f, -1.f, 0.5f);
	float yr = 0.f;
	Quatf q;
	ca.modifySetpoint3D(v, yr, q, posctl(), false);
	EXPECT_FLOAT_EQ(v(0), 2.f); // passthrough + warn
	EXPECT_FALSE(ca.intervening());
}

TEST_F(CollisionAvoidance3DTest, badMagicRejectedThenFailsafe)
{
	ca3d_field_t f = clearField(1);
	f.ring_cm[1][0] = 90;
	f.magic = 0x00; // corrupt
	publishField(f);
	TestCA3D ca;
	Vector3f v(2.f, 0.f, 0.f);
	float yr = 0.f;
	Quatf q;
	ca.modifySetpoint3D(v, yr, q, posctl(), false);
	EXPECT_FLOAT_EQ(v(0), 0.f); // no valid field -> failsafe STOP
}

TEST_F(CollisionAvoidance3DTest, wallFrontStopZeroesForwardOnly)
{
	TestCA3D ca;
	ca3d_field_t f = clearField(1);
	f.ring_cm[1][0] = 90; // front (az 0), horizontal ring, 0.9 m -> STOP
	publishField(f);

	Vector3f v(2.f, 1.f, -0.5f);
	float yr = 0.f;
	Quatf q;
	ca.modifySetpoint3D(v, yr, q, posctl(), false);

	EXPECT_EQ(ca.state(), State::STOP);
	EXPECT_TRUE(ca.dirMask() & CA3D_DIR_FWD);
	EXPECT_FLOAT_EQ(v(0), 0.f);   // forward blocked -> zeroed
	EXPECT_FLOAT_EQ(v(1), 1.f);   // lateral free (wall-following preserved)
	EXPECT_FLOAT_EQ(v(2), -0.5f); // vertical free
}

TEST_F(CollisionAvoidance3DTest, wallFrontAllowsBackingOff)
{
	TestCA3D ca;
	ca3d_field_t f = clearField(1);
	f.ring_cm[1][0] = 90;
	publishField(f);

	Vector3f v(-2.f, 0.f, 0.f); // moving backward, away from the wall
	float yr = 0.f;
	Quatf q;
	ca.modifySetpoint3D(v, yr, q, posctl(), false);
	EXPECT_FLOAT_EQ(v(0), -2.f); // BACK not blocked
	EXPECT_FALSE(ca.intervening());
}

TEST_F(CollisionAvoidance3DTest, ceilingBlocksClimbAllowsDescent)
{
	TestCA3D ca;
	ca3d_field_t f = clearField(1);
	f.up_cm = 90; // ceiling 0.9 m above
	publishField(f);

	Quatf q;
	float yr = 0.f;

	Vector3f climb(0.f, 0.f, -1.f); // -Z_FRD = up
	ca.modifySetpoint3D(climb, yr, q, posctl(), false);
	EXPECT_TRUE(ca.dirMask() & CA3D_DIR_UP);
	EXPECT_FLOAT_EQ(climb(2), 0.f); // climb blocked

	Vector3f descend(0.f, 0.f, 1.f); // +Z_FRD = down
	ca.modifySetpoint3D(descend, yr, q, posctl(), false);
	EXPECT_FLOAT_EQ(descend(2), 1.f); // descent free
}

TEST_F(CollisionAvoidance3DTest, nanComponentsPassThrough)
{
	TestCA3D ca;
	ca3d_field_t f = clearField(1);
	f.ring_cm[1][0] = 90; // STOP front
	publishField(f);

	Vector3f v(NAN, NAN, NAN);
	float yr = NAN;
	Quatf q;
	ca.modifySetpoint3D(v, yr, q, posctl(), false);
	EXPECT_FALSE(PX4_ISFINITE(v(0)));
	EXPECT_FALSE(PX4_ISFINITE(v(1)));
	EXPECT_FALSE(PX4_ISFINITE(v(2)));
	EXPECT_FALSE(ca.intervening());
}

TEST_F(CollisionAvoidance3DTest, stopExitRequiresConsecutiveClears)
{
	TestCA3D ca;
	Quatf q;
	float yr = 0.f;

	ca3d_field_t wall = clearField(1);
	wall.ring_cm[1][0] = 90;
	publishField(wall);
	Vector3f v(2.f, 0.f, 0.f);
	ca.modifySetpoint3D(v, yr, q, posctl(), false);
	EXPECT_EQ(ca.state(), State::STOP);

	// STOP_EXIT = 3: needs three consecutive clear fields to release
	for (uint8_t i = 2; i <= 3; ++i) {
		Vector3f vv(2.f, 0.f, 0.f);
		publishField(clearField(i));
		ca.modifySetpoint3D(vv, yr, q, posctl(), false);
		EXPECT_EQ(ca.state(), State::STOP) << "still STOP after " << (int)(i - 1) << " clears";
	}

	Vector3f vv(2.f, 0.f, 0.f);
	publishField(clearField(4));
	ca.modifySetpoint3D(vv, yr, q, posctl(), false);
	EXPECT_EQ(ca.state(), State::SAFE); // third clear releases
	EXPECT_FLOAT_EQ(vv(0), 2.f);
}

TEST_F(CollisionAvoidance3DTest, rotationNedToFrdAtYaw90)
{
	TestCA3D ca;
	ca3d_field_t f = clearField(1);
	f.ring_cm[1][0] = 90; // wall in body +X (front)
	publishField(f);

	// nose east (yaw = +90 deg): body +X maps to NED +Y
	Quatf q(0.70710678f, 0.f, 0.f, 0.70710678f);
	Vector3f v(0.f, 2.f, 0.f); // NED east == body forward
	float yr = 0.f;
	ca.modifySetpoint3D(v, yr, q, posctl(), false);

	// the body-forward command must be cancelled -> ~0 in NED
	EXPECT_NEAR(v(0), 0.f, 1e-4f);
	EXPECT_NEAR(v(1), 0.f, 1e-4f);
	EXPECT_TRUE(ca.dirMask() & CA3D_DIR_FWD);
}

TEST_F(CollisionAvoidance3DTest, yawLockZeroesYawInStop)
{
	setI("CA3D_YAW_LOCK", 1);
	TestCA3D ca;
	ca3d_field_t f = clearField(1);
	f.ring_cm[1][0] = 90; // STOP
	publishField(f);

	Vector3f v(2.f, 0.f, 0.f);
	float yr = 0.8f;
	Quatf q;
	ca.modifySetpoint3D(v, yr, q, posctl(), false);
	EXPECT_FLOAT_EQ(yr, 0.f);
}

TEST_F(CollisionAvoidance3DTest, modeMaskGatesOutAuto)
{
	setI("CA3D_MODE_MASK", 3); // POSCTL + OFFBOARD only, AUTO off
	TestCA3D ca;
	ca3d_field_t f = clearField(1);
	f.ring_cm[1][0] = 90;
	publishField(f);

	Vector3f v(2.f, 0.f, 0.f);
	float yr = 0.f;
	Quatf q;
	ca.modifySetpoint3D(v, yr, q, autoMode(), false);
	EXPECT_FLOAT_EQ(v(0), 2.f); // AUTO not gated -> passthrough
	EXPECT_FALSE(ca.intervening());
}

TEST_F(CollisionAvoidance3DTest, slowZoneGlobalFallbackScales)
{
	TestCA3D ca;
	ca3d_field_t f = clearField(1);
	f.ring_cm[1][0] = 150; // 1.5 m front: SLOW zone, beyond BLK_XY(1.1) -> empty mask
	publishField(f);

	Vector3f v(2.f, 2.f, 0.f);
	float yr = 0.f;
	Quatf q;
	ca.modifySetpoint3D(v, yr, q, posctl(), false);
	EXPECT_EQ(ca.state(), State::SLOW);
	EXPECT_EQ(ca.dirMask(), 0);          // not within blocking distance
	EXPECT_FLOAT_EQ(v(0), 2.f * 0.35f);  // global fallback scales all finite axes
	EXPECT_FLOAT_EQ(v(1), 2.f * 0.35f);
}

// ===========================================================================
// C1 — kinematic braking cap + stop-line latch
// ===========================================================================

// -- test 1: v_allow table (pure function) ----------------------------------
TEST_F(CollisionAvoidance3DTest, kinematicVAllowTable)
{
	const float d_stop = 1.1f;

	// exact stop line and below -> zero
	EXPECT_FLOAT_EQ(CollisionAvoidance3D::vAllow(1.1f, 3.f, d_stop), 0.f);
	EXPECT_FLOAT_EQ(CollisionAvoidance3D::vAllow(0.5f, 3.f, d_stop), 0.f);

	// spot values from the design
	EXPECT_NEAR(CollisionAvoidance3D::vAllow(2.0f, 3.f, d_stop), 2.32379f, 1e-4f);
	EXPECT_NEAR(CollisionAvoidance3D::vAllow(4.5f, 3.f, d_stop), 4.51664f, 1e-4f);

	// strictly monotonic increasing in d above the stop line
	float prev = -1.f;

	for (float d = 1.1f; d <= 8.f; d += 0.25f) {
		const float va = CollisionAvoidance3D::vAllow(d, 3.f, d_stop);
		EXPECT_GE(va, prev);
		prev = va;
	}
}

// -- test 2: vector cap — oblique approach ----------------------------------
TEST_F(CollisionAvoidance3DTest, kinematicVectorCapOblique)
{
	// wall normal along +X (b = forward). Velocity is oblique: forward + right.
	const Vector3f b(1.f, 0.f, 0.f);

	Vector3f v(3.f, 2.f, 0.f);
	CollisionAvoidance3D::capClosing(v, b, 1.f); // v_allow = 1 along +X
	EXPECT_NEAR(v(0), 1.f, 1e-5f);  // closing component reduced to v_allow
	EXPECT_NEAR(v(1), 2.f, 1e-5f);  // tangential (right) preserved
	EXPECT_NEAR(v(2), 0.f, 1e-5f);

	// already-compliant closing speed is untouched
	Vector3f v2(0.5f, 2.f, 0.f);
	CollisionAvoidance3D::capClosing(v2, b, 1.f);
	EXPECT_NEAR(v2(0), 0.5f, 1e-5f);
	EXPECT_NEAR(v2(1), 2.f, 1e-5f);

	// diagonal bearing: only the along-bearing component is clipped
	Vector3f bd(0.70710678f, 0.70710678f, 0.f);
	Vector3f v3(2.f, 0.f, 0.f);              // v_close = 1.4142 along bd
	CollisionAvoidance3D::capClosing(v3, bd, 0.5f);
	EXPECT_NEAR(v3.dot(bd), 0.5f, 1e-5f);    // projection onto bd == v_allow

	// multiple cells along the same bearing -> most restrictive (lowest) wins
	Vector3f v4(4.f, 0.f, 0.f);
	CollisionAvoidance3D::capClosing(v4, b, 3.f);  // first cell
	CollisionAvoidance3D::capClosing(v4, b, 1.5f); // nearer cell
	CollisionAvoidance3D::capClosing(v4, b, 2.f);  // farther cell, no effect
	EXPECT_NEAR(v4(0), 1.5f, 1e-5f);
}

// -- kinematic cap end-to-end: bites early, leaves slow flight alone ---------
TEST_F(CollisionAvoidance3DTest, kinematicCapBitesInSafeZone)
{
	setF("CA3D_BRK_ACC", 3.f);
	TestCA3D ca;

	// wall 3.0 m ahead: beyond D_SLOW(2.0) so the zone machine is SAFE,
	// but the kinematic cap must still limit a fast approach.
	ca3d_field_t f = clearField(1);
	f.ring_cm[1][0] = 300;
	publishField(f);

	Vector3f v(5.f, 0.f, 0.f); // commanded 5 m/s forward
	float yr = 0.f;
	Quatf q;
	ca.modifySetpoint3D(v, yr, q, posctl(), false);

	EXPECT_EQ(ca.state(), State::SAFE);            // zone unchanged
	EXPECT_NEAR(v(0), 3.37639f, 1e-3f);            // capped to v_allow(3.0)
	EXPECT_TRUE(ca.intervening());
}

TEST_F(CollisionAvoidance3DTest, kinematicSlowFlightUntouched)
{
	setF("CA3D_BRK_ACC", 3.f);
	TestCA3D ca;

	ca3d_field_t f = clearField(1);
	f.ring_cm[1][0] = 300; // 3.0 m ahead -> v_allow = 3.376
	publishField(f);

	Vector3f v(2.f, 1.f, 0.f); // slow: 2 m/s forward < v_allow, lateral free
	float yr = 0.f;
	Quatf q;
	ca.modifySetpoint3D(v, yr, q, posctl(), false);

	EXPECT_FLOAT_EQ(v(0), 2.f); // untouched
	EXPECT_FLOAT_EQ(v(1), 1.f);
	EXPECT_FALSE(ca.intervening());
}

TEST_F(CollisionAvoidance3DTest, kinematicVerticalCapOnCeiling)
{
	setF("CA3D_BRK_ACC", 3.f);
	TestCA3D ca;

	ca3d_field_t f = clearField(1);
	f.up_cm = 300; // ceiling 3.0 m above
	publishField(f);

	Vector3f climb(0.f, 0.f, -5.f); // -Z_FRD = up, 5 m/s
	float yr = 0.f;
	Quatf q;
	ca.modifySetpoint3D(climb, yr, q, posctl(), false);
	EXPECT_NEAR(climb(2), -3.37639f, 1e-3f); // capped by v_allow(3.0)
	EXPECT_FALSE(ca.latchMask() & CA3D_DIR_UP); // 3.0 m > D_STOP -> no latch

	// descent is unaffected: no obstacle below (ceiling stays overhead)
	Vector3f descend(0.f, 0.f, 5.f);
	f.seq = 2;
	publishField(f);
	ca.modifySetpoint3D(descend, yr, q, posctl(), false);
	EXPECT_FLOAT_EQ(descend(2), 5.f);
}

// -- test 3: stop-line latch state machine ----------------------------------
TEST_F(CollisionAvoidance3DTest, latchStateMachine)
{
	setF("CA3D_BRK_ACC", 3.f); // enables cap + latch
	setF("CA3D_D_HYST", 0.3f);
	TestCA3D ca;
	Quatf q;
	float yr = 0.f;
	uint8_t seq = 1;

	// entry: obstacle inside D_STOP latches FWD
	ca3d_field_t f = clearField(seq++);
	f.ring_cm[1][0] = 90; // 0.9 m < D_STOP
	publishField(f);
	Vector3f v(2.f, 0.f, 0.f);
	ca.modifySetpoint3D(v, yr, q, posctl(), false);
	EXPECT_TRUE(ca.latchMask() & CA3D_DIR_FWD);
	EXPECT_FLOAT_EQ(v(0), 0.f);

	// reverse component passes through while latched
	Vector3f back(-2.f, 0.f, 0.f);
	f = clearField(seq++); f.ring_cm[1][0] = 90; publishField(f);
	ca.modifySetpoint3D(back, yr, q, posctl(), false);
	EXPECT_FLOAT_EQ(back(0), -2.f);
	EXPECT_TRUE(ca.latchMask() & CA3D_DIR_FWD);

	// marginal frames just past D_STOP but below HYST -> NO release, closing clamped
	for (int i = 0; i < 6; ++i) {
		f = clearField(seq++); f.ring_cm[1][0] = 120; /* 1.2 m, < 1.1+0.3 */ publishField(f);
		Vector3f fwd(2.f, 0.f, 0.f);
		ca.modifySetpoint3D(fwd, yr, q, posctl(), false);
		EXPECT_TRUE(ca.latchMask() & CA3D_DIR_FWD) << "held at marginal frame " << i;
		EXPECT_FLOAT_EQ(fwd(0), 0.f);
	}

	// retreat past D_STOP + HYST for STOP_EXIT frames -> release
	for (int i = 0; i < 3; ++i) {
		f = clearField(seq++); f.ring_cm[1][0] = 145; /* 1.45 m >= 1.4 */ publishField(f);
		Vector3f fwd(1.f, 0.f, 0.f);
		ca.modifySetpoint3D(fwd, yr, q, posctl(), false);
	}

	EXPECT_FALSE(ca.latchMask() & CA3D_DIR_FWD); // released after 3 retreat frames

	// forward now passes (1.0 < v_allow(1.45) = 1.449)
	f = clearField(seq++); f.ring_cm[1][0] = 145; publishField(f);
	Vector3f fwd(1.f, 0.f, 0.f);
	ca.modifySetpoint3D(fwd, yr, q, posctl(), false);
	EXPECT_FLOAT_EQ(fwd(0), 1.f);
}

// -- ceiling latch holds under sustained up-stick (test 7 unit analog) -------
TEST_F(CollisionAvoidance3DTest, latchCeilingHoldsUnderSustainedStick)
{
	setF("CA3D_BRK_ACC", 3.f);
	TestCA3D ca;
	Quatf q;
	float yr = 0.f;
	uint8_t seq = 1;

	ca3d_field_t f = clearField(seq++);
	f.up_cm = 100; // 1.0 m < D_STOP -> latch UP
	publishField(f);
	Vector3f up(0.f, 0.f, -1.f);
	ca.modifySetpoint3D(up, yr, q, posctl(), false);
	EXPECT_TRUE(ca.latchMask() & CA3D_DIR_UP);

	// sustained up-stick with the ceiling flapping around the boundary:
	// up must stay clamped to 0 (no ratchet).
	for (int i = 0; i < 30; ++i) {
		f = clearField(seq++);
		f.up_cm = (i % 2) ? 115 : 108; // 1.08/1.15 m, both below release 1.4
		publishField(f);
		Vector3f climb(0.f, 0.f, -1.f);
		ca.modifySetpoint3D(climb, yr, q, posctl(), false);
		EXPECT_FLOAT_EQ(climb(2), 0.f) << "creep at frame " << i;
		EXPECT_TRUE(ca.latchMask() & CA3D_DIR_UP);
	}
}

// -- test 4: CA3D_BRK_ACC = 0 -> exact legacy stair/latch behavior -----------
TEST_F(CollisionAvoidance3DTest, legacyModeExactStair)
{
	setF("CA3D_BRK_ACC", 0.f); // legacy
	TestCA3D ca;
	Quatf q;
	float yr = 0.f;

	// SLOW zone: forward wall at 1.5 m, dir_mask localizes FWD? beyond BLK -> mask 0
	ca3d_field_t f = clearField(1);
	f.ring_cm[1][0] = 150;
	publishField(f);
	Vector3f v(2.f, 2.f, 0.f);
	ca.modifySetpoint3D(v, yr, q, posctl(), false);
	EXPECT_EQ(ca.state(), State::SLOW);
	EXPECT_FLOAT_EQ(v(0), 2.f * 0.35f); // legacy stair, global fallback
	EXPECT_FLOAT_EQ(v(1), 2.f * 0.35f);
	EXPECT_EQ(ca.latchMask(), 0);       // latch never engages in legacy mode

	// STOP zone: wall at 0.9 m -> forward zeroed, lateral free, no latch
	f = clearField(2);
	f.ring_cm[1][0] = 90;
	publishField(f);
	Vector3f v2(2.f, 1.f, 0.f);
	ca.modifySetpoint3D(v2, yr, q, posctl(), false);
	EXPECT_EQ(ca.state(), State::STOP);
	EXPECT_FLOAT_EQ(v2(0), 0.f);
	EXPECT_FLOAT_EQ(v2(1), 1.f);
	EXPECT_EQ(ca.latchMask(), 0);
}

// ===========================================================================
// C2 — true per-cell elevation (wire v2)
// ===========================================================================

// -- version matrix: v1 frame -> legacy ring-center (D4 leak), bit-exact -----
TEST_F(CollisionAvoidance3DTest, c2LegacyV1CeilingLeaksToForward)
{
	TestCA3D ca;
	ca3d_field_t f = clearField(1);
	f.version = CA3D_VERSION_LEGACY;   // v1: el_deg ignored, ring-center used
	f.ring_cm[2][0] = 90;              // up-tilt band (center +45), az 0, 0.9 m
	publishField(f, CA3D_FIELD_V1_BYTES);   // 84-byte legacy frame

	Vector3f v(2.f, 0.f, -1.f);        // forward + climb
	float yr = 0.f; Quatf q;
	ca.modifySetpoint3D(v, yr, q, posctl(), false);

	// legacy reconstructs +45 -> horizontal-dominant -> FWD leak (the D4 defect)
	EXPECT_TRUE(ca.dirMask() & CA3D_DIR_FWD);
	EXPECT_FLOAT_EQ(v(0), 0.f);        // forward blocked (legacy behavior preserved)
}

// -- v2 true elevation: a ceiling return constrains UP, not forward (D4 fix) --
TEST_F(CollisionAvoidance3DTest, c2TrueElevationCeilingDoesNotBlockForward)
{
	TestCA3D ca;
	ca3d_field_t f = clearField(1);    // v2
	f.ring_cm[2][0] = 90;              // same cell/range as the legacy test
	f.el_deg[2][0] = 60;               // TRUE elevation +60 (steeper than the 45 band edge)
	publishField(f);                   // 120-byte v2 frame

	Vector3f v(2.f, 0.f, -1.f);        // forward + climb (-Z = up)
	float yr = 0.f; Quatf q;
	ca.modifySetpoint3D(v, yr, q, posctl(), false);

	EXPECT_TRUE(ca.dirMask() & CA3D_DIR_UP);      // classified UP (vertical-dominant)
	EXPECT_FALSE(ca.dirMask() & CA3D_DIR_FWD);    // horizontal axes free -> D4 gone
	EXPECT_FLOAT_EQ(v(0), 2.f);                   // forward passes through
	EXPECT_FLOAT_EQ(v(2), 0.f);                   // climb into the ceiling blocked
}

// -- sign/frame: a return BELOW forward -> +z (down) FRD -> constrains DESCENT
TEST_F(CollisionAvoidance3DTest, c2SignBelowForwardConstrainsDescentNotForward)
{
	TestCA3D ca;
	ca3d_field_t f = clearField(1);
	f.ring_cm[1][0] = 90;              // az 0, 0.9 m
	f.el_deg[1][0] = -60;             // TRUE elevation -60 = below-forward (steep terrain)
	publishField(f);

	Vector3f v(2.f, 0.f, 1.f);         // forward + descent (+Z = down)
	float yr = 0.f; Quatf q;
	ca.modifySetpoint3D(v, yr, q, posctl(), false);

	EXPECT_TRUE(ca.dirMask() & CA3D_DIR_DOWN);    // below-forward -> DOWN (down = +z FRD)
	EXPECT_FALSE(ca.dirMask() & CA3D_DIR_UP);     // sign not flipped
	EXPECT_FALSE(ca.dirMask() & CA3D_DIR_FWD);    // forward relieved
	EXPECT_FLOAT_EQ(v(0), 2.f);                   // forward passes (fly level over it)
	EXPECT_FLOAT_EQ(v(2), 0.f);                   // descent into terrain blocked
}

// -- above-forward -> -z (up) : the opposite sign ---------------------------
TEST_F(CollisionAvoidance3DTest, c2SignAboveForwardConstrainsClimb)
{
	TestCA3D ca;
	ca3d_field_t f = clearField(1);
	f.ring_cm[1][0] = 90;
	f.el_deg[1][0] = 60;              // above-forward
	publishField(f);

	Vector3f up(0.f, 0.f, -1.f);       // climb
	float yr = 0.f; Quatf q;
	ca.modifySetpoint3D(up, yr, q, posctl(), false);
	EXPECT_TRUE(ca.dirMask() & CA3D_DIR_UP);
	EXPECT_FALSE(ca.dirMask() & CA3D_DIR_DOWN);
	EXPECT_FLOAT_EQ(up(2), 0.f);
}

// -- unknown version -> rejected -> failsafe (no valid field) ----------------
TEST_F(CollisionAvoidance3DTest, c2UnknownVersionRejectedToFailsafe)
{
	TestCA3D ca;
	ca3d_field_t f = clearField(1);
	f.version = 99;                    // unknown
	f.ring_cm[1][0] = 90;
	publishField(f);

	Vector3f v(2.f, -1.f, 0.5f);
	float yr = 0.f; Quatf q;
	ca.modifySetpoint3D(v, yr, q, posctl(), false);
	EXPECT_EQ(ca.state(), State::STOP);   // no valid field ever -> stale failsafe
	EXPECT_FLOAT_EQ(v(0), 0.f);
	EXPECT_FLOAT_EQ(v(1), 0.f);
}

// -- boundary elevations + sentinel don't-care el ---------------------------
TEST_F(CollisionAvoidance3DTest, c2BoundaryElevations)
{
	// el = +90 straight up (az 0), 0.9 m -> pure UP
	{
		TestCA3D ca;
		ca3d_field_t f = clearField(1);
		f.ring_cm[1][0] = 90; f.el_deg[1][0] = 90;
		publishField(f);
		Vector3f up(0.f, 0.f, -1.f); float yr = 0.f; Quatf q;
		ca.modifySetpoint3D(up, yr, q, posctl(), false);
		EXPECT_TRUE(ca.dirMask() & CA3D_DIR_UP);
		EXPECT_FALSE(ca.dirMask() & CA3D_DIR_FWD);
	}
	// el = 0 -> pure forward
	{
		TestCA3D ca;
		ca3d_field_t f = clearField(1);
		f.ring_cm[1][0] = 90; f.el_deg[1][0] = 0;
		publishField(f);
		Vector3f v(2.f, 0.f, 0.f); float yr = 0.f; Quatf q;
		ca.modifySetpoint3D(v, yr, q, posctl(), false);
		EXPECT_TRUE(ca.dirMask() & CA3D_DIR_FWD);
		EXPECT_FLOAT_EQ(v(0), 0.f);
	}
	// sentinel range cell carries don't-care el -> no obstacle at all
	{
		TestCA3D ca;
		ca3d_field_t f = clearField(1);
		f.ring_cm[1][0] = CA3D_INVALID_CM; f.el_deg[1][0] = 77; // garbage el, invalid range
		publishField(f);
		Vector3f v(2.f, 0.f, 0.f); float yr = 0.f; Quatf q;
		ca.modifySetpoint3D(v, yr, q, posctl(), false);
		EXPECT_EQ(ca.state(), State::SAFE);
		EXPECT_FLOAT_EQ(v(0), 2.f);
	}
}

// -- wall regression: a flat wall (el ~ 0) matches C1 behavior ---------------
TEST_F(CollisionAvoidance3DTest, c2FlatWallMatchesC1)
{
	setF("CA3D_BRK_ACC", 3.f);
	TestCA3D ca;
	ca3d_field_t f = clearField(1);
	f.ring_cm[1][0] = 300; f.el_deg[1][0] = 0; // 3.0 m flat wall ahead
	publishField(f);
	Vector3f v(5.f, 0.f, 0.f); float yr = 0.f; Quatf q;
	ca.modifySetpoint3D(v, yr, q, posctl(), false);
	EXPECT_NEAR(v(0), 3.37639f, 1e-3f); // identical to the C1 kinematicCapBitesInSafeZone value
}
