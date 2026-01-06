# Electronics Design Files

This directory contains the electronic design files for the Skyfan Zigbee Controller hardware. The design supports both fan-only and fan+light configurations through firmware build options.

## Directory Structure

### Design Files
- **SkyFanController.sch** - Schematic file with circuit diagrams
- **SkyFanController.brd** - PCB layout file with component placement and routing
- **SkyfanController.f3z** - Fusion 360 Electronics archive (complete project backup)

### Manufacturing Files
- **gerber/** - Complete manufacturing file set for PCB production
  - **GerberFiles/** - Gerber layer files (copper, soldermask, silkscreen, profile)
  - **DrillFiles/** - Excellon drill files for holes and vias
  - **Assembly/** - Pick and place files for automated assembly
  - **SkyfanController.zip** - All manufacturing files packaged for PCB fab houses

## Design Tools

The design files are in **Autodesk Fusion 360 Electronics** format. The `.sch` and `.brd` files use Eagle format (compatible with standalone Eagle) while the `.f3z` file is a Fusion 360 archive containing the complete project.

## Hardware Specifications

- **MCU**: ESP32-C6 with Zigbee 3.0 support  
- **Power**: 3.3V operation provided by fan
- **Connectivity**: Hardware UART communication with Tuya MCU (115200 baud)
- **Debug**: USB-C serial debug output (configurable baud rate)
- **Control**: Factory reset button on BOOT pin
- **Indicators**: Status LED for network state and command feedback
- **Configuration**: Supports fan-only or fan+light firmware builds

## PCB Specifications

- **Board thickness**: 1.6mm standard
- **Copper weight**: 1oz (35µm) 
- **Minimum via size**: 0.2mm
- **Minimum track width**: 0.1mm
- **Layer count**: 2-layer PCB (top/bottom copper)

## Manufacturing

All manufacturing files are organised in the `gerber/` subdirectory:
- Complete Gerber layer set for fabrication
- Excellon drill files (.xln format)
- Pick and place files for automated assembly
- Gerber job file with layer stack specifications