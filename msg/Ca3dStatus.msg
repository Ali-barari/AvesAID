# Iron Sphere 3D collision avoidance status (published by the collision_avoidance_3d lib)
uint64 timestamp		# time since system start (microseconds)

uint8 STATE_SAFE = 0
uint8 STATE_SLOW = 1
uint8 STATE_STOP = 2
uint8 state			# current avoidance state (SAFE/SLOW/STOP)

uint8 dir_mask			# blocked directions, bit0 +X fwd, 1 -X back, 2 +Y_FLU left,
				# 3 -Y_FLU right, 4 +Z_FLU up, 5 -Z_FLU down (see ca3d_tunnel_payload.h)

float32 min_dist		# global min range over valid cells [m], NaN if no valid cell
uint32 field_age_ms		# age of the last valid field [ms]
int16 has_cm			# height above RANSAC landing surface [cm], CA3D_HAS_INVALID if unknown
uint16 seq_lost			# cumulative dropped fields (seq gaps)
uint8 field_seq			# last decoded field seq

bool active			# CA3D enabled, gated mode, armed & airborne this cycle
bool intervening		# a velocity/yaw component was actually constrained this cycle
bool field_valid		# a fresh, valid field is available (not stale/failsafe)
