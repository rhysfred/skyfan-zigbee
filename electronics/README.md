# Electronics Design Files

This directory contains the electronic design files for the Skyfan Zigbee Controller hardware.

## Directory Contents

- **Schematic files** - Circuit diagrams showing electrical connections
- **PCB layout files** - Physical board design and component placement
- **Component libraries** - Custom symbols and footprints
- **3D models** - Component 3D representations for visualization
- **gerber/** - Manufacturing files for PCB production

## Design Tools

The design files are created using **Autodesk Fusion 360 Electronics**.

## File Naming Convention

- `skyfan-zigbee.fsch` - Main schematic file
- `skyfan-zigbee.fbrd` - PCB layout file
- `*.f3d` - Fusion 360 project files
- `*.lbr` - Component libraries
- `*.f3z` - Fusion 360 archive files

## Hardware Specifications

- **MCU**: ESP32-C6 with Zigbee support
- **Power**: 3.3V operation
- **Connectivity**: UART communication with Tuya MCU
- **Debug**: Optional SoftwareSerial debug output
- **Reset**: Factory reset button (BOOT pin)

## Manufacturing

Processed manufacturing files are stored in the `gerber/` subdirectory and include:
- Gerber files for each PCB layer
- Excellon drill files
- Pick and place files
- Bill of materials (BOM)
- Assembly drawings
- Silkscreen overlay files