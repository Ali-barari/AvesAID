# AvesAID: Custom message for flight status
uint64 timestamp		# time since system start (microseconds)

# ARM enum - Current arm/payload attached
uint8 ARM_UNKNOWN = 0
uint8 SPARM = 1
uint8 DELTA_ARM = 2
uint8 SURFACE_PREP = 3
uint8 LONG_ARM = 4
uint8 GIMBAL = 5
uint8 DEPLOYMENT = 6

uint8 arm_type		# Current arm/payload type (ARM enum)

# VEHICLE enum - Current vehicle type
uint8 VEHICLE_UNKNOWN = 0
uint8 SN = 1
uint8 SB = 2
uint8 UT_CL = 3
uint8 EMAT_CL = 4
uint8 PA_CL = 5

uint8 vehicle_type	# Current vehicle type (VEHICLE enum)

bool flag_mode_attachment_enabled
bool flag_mode_partial_attachment_enabled
bool flag_magnet_enabled
bool flag_height_source_baro_enabled
bool flag_height_source_slam_enabled
bool flag_payload_enabled
