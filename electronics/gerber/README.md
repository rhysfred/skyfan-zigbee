# Gerber Manufacturing Files

This directory contains the processed manufacturing files exported from Fusion 360 Electronics for PCB fabrication.

## Directory Structure

### GerberFiles/
- **copper_bottom.gbr** - Bottom copper layer
- **copper_top.gbr** - Top copper layer  
- **silkscreen_bottom.gbr** - Bottom silkscreen layer
- **silkscreen_top.gbr** - Top silkscreen layer
- **soldermask_bottom.gbr** - Bottom soldermask layer
- **soldermask_top.gbr** - Top soldermask layer
- **solderpaste_bottom.gbr** - Bottom solderpaste stencil
- **solderpaste_top.gbr** - Top solderpaste stencil
- **profile.gbr** - Board outline/profile
- **gerber_job.gbrjob** - Gerber job file with layer stack information

### DrillFiles/
- **drill_1_16.xln** - Excellon drill file for via and component holes

### Assembly/
- **PnP_Skyfan Controller v4_front.txt** - Pick and place file for front components
- **Skyfan Controller v4.txt** - Component placement coordinates

## Manufacturing Notes

- Board thickness: 1.6mm standard
- Copper weight: 1oz (35µm) standard
- Minimum via size: 0.2mm
- Minimum track width: 0.1mm