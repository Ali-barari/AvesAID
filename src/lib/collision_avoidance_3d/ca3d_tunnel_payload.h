/****************************************************************************
 * CA3D tunnel payload — shared data contract (Iron Sphere collision avoidance)
 *
 * SINGLE SOURCE OF TRUTH. This file is vendored BYTE-IDENTICAL into two repos:
 *   - PX4-Autopilot/src/lib/collision_avoidance_3d/ca3d_tunnel_payload.h  (decode)
 *   - uav_collision_avoidance/include/uav_collision_avoidance/ca3d_tunnel_payload.h (encode)
 * Any change must be applied to BOTH and kept `diff`-clean.
 *
 * The ROS companion ENCODES a 3D obstacle distance field into this struct and ships it to
 * the FCU inside a stock MAVLink TUNNEL (id 385) message. PX4 DECODES it and reconstructs
 * the 6-direction semantics natively. No MAVLink dialect changes are required: payload_type
 * values > 32767 are the MAVLink-designated "local experiment" range.
 *
 * Frame convention (spelled out identically in both repos' docs):
 *   Body FRD  — x forward, y right, z DOWN.
 *   Azimuth   = atan2(y_frd, x_frd), 0 on +X (forward), increasing toward +Y (right).
 *   Elevation = atan2(-z_frd, hypot(x,y)), POSITIVE = UP.
 *   ROS base_link is FLU; the encoder flips y and z into FRD before binning.
 ****************************************************************************/
#pragma once
#include <stdint.h>
#include <stddef.h>   /* offsetof */

#define CA3D_TUNNEL_PAYLOAD_TYPE 32800u  /* >32767 = MAVLink "local experiment" range */
#define CA3D_MAGIC   0xA5u
#define CA3D_VERSION 2u          /* current wire version. v2 appends el_deg[3][12] (C2). */
#define CA3D_VERSION_LEGACY 1u   /* v1: no el_deg; decode via hardcoded ring-center elevation */

#define CA3D_NUM_AZ 12            /* 30° azimuth bins, index 0 centered on body +X,
                                     increasing toward body-right (FRD +Y) */
#define CA3D_NUM_EL 3             /* elevation rings: index 0 = -45° (down-tilted),
                                     1 = 0° (horizontal), 2 = +45° (up-tilted);
                                     elevation positive = UP */
#define CA3D_INVALID_CM   0xFFFFu /* no return within max_range in this cell */
#define CA3D_HAS_INVALID  INT16_MIN

typedef struct __attribute__((packed))
{
	uint8_t  magic;         /* CA3D_MAGIC */
	uint8_t  version;       /* CA3D_VERSION */
	uint8_t  seq;           /* wrapping counter, for drop detection */
	uint8_t  flags;         /* bit0 field_valid, bit1 ground_plane_valid, rest 0 */
	int16_t  has_cm;        /* height above RANSAC landing surface, cm; CA3D_HAS_INVALID */
	uint16_t max_range_cm;  /* encoder query radius */
	uint16_t down_cm;       /* straight down, ground-surface inliers EXCLUDED */
	uint16_t up_cm;         /* straight up */
	uint16_t ring_cm[CA3D_NUM_EL][CA3D_NUM_AZ];  /* 3 x 12, min range per cell, cm */
	/* --- v2 (C2) APPENDED below; all offsets above are unchanged. Decoders that
	 * only read v1 fields stay correct. Present only when version >= CA3D_VERSION
	 * and payload_length >= sizeof(ca3d_field_t); a v1 frame is 84 bytes and omits
	 * this array. --- */
	int8_t   el_deg[CA3D_NUM_EL][CA3D_NUM_AZ];   /* v2: elevation (deg, + = UP, FRD-
	                                                consistent) of each cell's min-range
	                                                point, clamped [-90,+90]; DON'T-CARE
	                                                when ring_cm == CA3D_INVALID_CM */
} ca3d_field_t;             /* v1: 84 bytes; v2: 84 + 36 = 120 bytes */

#define CA3D_FIELD_V1_BYTES 84u                 /* on-wire size of a legacy v1 frame */

/* flags bits */
#define CA3D_FLAG_FIELD_VALID        (1u << 0)
#define CA3D_FLAG_GROUND_PLANE_VALID (1u << 1)

/* Reconstruction direction bits — IDENTICAL numbering to the ROS decision layer
 * (BLOCK_POS_X..BLOCK_NEG_Z). Defined here once so PX4 and the encoder agree.
 * NOTE the FRD vs FLU sign flip: "up" (bit4) is +Z in ROS FLU = -Z in body FRD;
 * "down" (bit5) is -Z FLU = +Z FRD. The bit MEANINGS (fwd/back/left/right/up/down)
 * are frame-independent; only the axis math flips. */
#define CA3D_DIR_FWD   (1u << 0)  /* +X forward  */
#define CA3D_DIR_BACK  (1u << 1)  /* -X backward */
#define CA3D_DIR_LEFT  (1u << 2)  /* +Y_FLU left  (= -Y_FRD) */
#define CA3D_DIR_RIGHT (1u << 3)  /* -Y_FLU right (= +Y_FRD) */
#define CA3D_DIR_UP    (1u << 4)  /* +Z_FLU up    (= -Z_FRD) */
#define CA3D_DIR_DOWN  (1u << 5)  /* -Z_FLU down  (= +Z_FRD) */

/* Static layout guarantees. Portable across the C11 (encoder standalone) and C++
 * (PX4 lib, ROS node) toolchains — both build with -Werror, and the C11 keyword
 * _Static_assert warns in C++ mode, so select the right form. The STRUCT and all
 * constants above remain exactly as the §3 data contract specifies. */
#if defined(__cplusplus)
static_assert(sizeof(ca3d_field_t) == 120, "layout (v2: 84 + el_deg[3][12])");
static_assert(offsetof(ca3d_field_t, ring_cm) == 12, "v1 prefix offsets must be stable");
static_assert(offsetof(ca3d_field_t, el_deg) == CA3D_FIELD_V1_BYTES, "el_deg appended after v1 layout");
static_assert(sizeof(ca3d_field_t) <= 128, "must fit MAVLink TUNNEL payload");
#else
_Static_assert(sizeof(ca3d_field_t) == 120, "layout (v2: 84 + el_deg[3][12])");
_Static_assert(offsetof(ca3d_field_t, ring_cm) == 12, "v1 prefix offsets must be stable");
_Static_assert(offsetof(ca3d_field_t, el_deg) == CA3D_FIELD_V1_BYTES, "el_deg appended after v1 layout");
_Static_assert(sizeof(ca3d_field_t) <= 128, "must fit MAVLink TUNNEL payload");
#endif
