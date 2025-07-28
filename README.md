# AvesAID - Autopilot for Inspection Drones

AvesAID is a specialized autopilot system designed specifically for complex inspection missions and scenarios. Built on the robust PX4 foundation, AvesAID extends the capabilities with advanced payload management, real-time adaptation, and inspection-focused flight control features.

This repository contains the AvesAID flight control solution, with customized modules located in the [src/modules](src/modules) directory. AvesAID includes specialized middleware and drivers optimized for inspection drone operations.

AvesAID is highly portable, OS-independent and supports Linux, NuttX and MacOS out of the box.

## Key Features

* **Advanced Payload Management**: Real-time payload monitoring and rotor position adaptation
* **Inspection-Optimized Flight Control**: Specialized flight modes for complex inspection scenarios
* **Dynamic Effectiveness Matrix**: Real-time actuator effectiveness adaptation based on payload status
* **Custom MAVLink Commands**: Extended command set for inspection-specific operations
* **Configurable Transitions**: Smooth, parameterized transitions for payload deployment and retraction
* **PX4 Convention Versioning**: Git tag format v<PX4-version>-<AvesAID-version> (e.g., v1.15.4-1.0.0)

## Supported Inspection Scenarios

* **Infrastructure Inspection**: Bridges, towers, buildings, pipelines
* **Surface Preparation**: Advanced surface treatment with attachable tools
* **Non-Destructive Testing**: Ultrasonic, magnetic, and visual inspection systems
* **Precision Positioning**: High-accuracy positioning for detailed inspection work
* **Multi-Payload Operations**: Support for various inspection payloads and sensors

## Supported Inspection Payloads

AvesAID supports a wide range of inspection equipment:

| Payload Type | Applications |
|---|---|
| **Cameras & Imaging** | Visual inspection, thermal imaging, multispectral analysis |
| **Ultrasonic Testing** | Non-destructive material testing, thickness measurement |
| **Magnetic Testing** | Ferromagnetic material inspection, crack detection |
| **Surface Preparation** | Cleaning, coating removal, surface treatment |
| **Contact Sensors** | Direct surface measurement and testing |

## Hardware Compatibility

AvesAID is compatible with standard PX4 flight controllers with enhanced support for inspection operations.

### Recommended Flight Controllers

**Primary Targets** (Fully tested and optimized):
* **Pixhawk 6C (FMUv6C)** - Recommended for most inspection operations
* **Pixhawk 6X (FMUv6X)** - High-performance option for complex payloads

**Supported Controllers**:
* CUAV Pixahwk V6X (FMUv6X)
* Holybro Pixhawk 6X/6C (FMUv6X/FMUv6C) (Tested)
* Holybro Pix32 v6 (FMUv6C)
* CubePilot Cube Orange/Orange+ (FMUv5)
* ARK Electronics ARKV6X

### Inspection-Specific Hardware Requirements

**Minimum Specifications**:
* ARM Cortex-M7 processor (for real-time payload processing)
* 32MB+ flash memory (for enhanced logging)
* Multiple UART ports (for payload communication)
* CAN bus support (for advanced sensor integration)
* High-resolution ADC (for precise sensor readings)

**Recommended Add-ons**:
* **Companion Computers**: Intel NUC, NVIDIA Jetson for advanced processing
* **Communication**: Long-range telemetry modules, cellular connectivity
* **Sensors**: High-precision IMU, RTK GPS, 3D LiDAR for positioning and mapping
* **Power Systems**: Hot-swappable batteries, power distribution boards

## AvesAID Roadmap

### Current Version Features
- ✅ Advanced flight mode adjustment for inspections
- ✅ Real-time data streaming and analysis
- ✅ Custom MAVLink command support for inspection operations
- ✅ Configurable payload transitions
- ✅ Robust failsafe algorithm

### Planned Enhancements
- 🔄 Autonomous inspection mission execution
- 🔄 Multi-robot swarm inspection capabilities
- 🔄 Enhanced safety systems for payload operations
- 🔄 Advanced sensor fusion for inspection accuracy

## License and Acknowledgments

AvesAID is based on PX4 Autopilot and maintains compatibility with the PX4 ecosystem while adding specialized inspection capabilities.

* **License**: BSD 3-clause (following PX4 licensing)
* **Based on**: PX4 Autopilot v1.15.4+
* **Specialized for**: Professional inspection drone operations

### Acknowledgments

AvesAID builds upon the excellent foundation provided by the PX4 Development Team and the broader drone development community. We acknowledge and thank all contributors to the PX4 project for their foundational work that makes AvesAID possible.

**PX4 Foundation**: [PX4 Autopilot](https://github.com/PX4/PX4-Autopilot)
**Hosted under**: Following open-source development principles


**Documentation**: Ali Barari
**Code Development**: Ali Barari
