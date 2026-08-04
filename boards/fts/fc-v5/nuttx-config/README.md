# NuttX config for yourcompany_yourfc

This directory is a PX4 FMUv5-class starting point generated from the uploaded ArduPilot `hwdef.dat`.

Recommended integration path:

1. Copy the current PX4 FMUv5 NuttX config as a baseline:
   ```sh
   cp -a boards/px4/fmu-v5/nuttx-config/* boards/yourcompany/yourfc/nuttx-config/
   ```
2. Apply the deltas in `defconfig.delta` and `include/board_pins_from_hwdef.h`.
3. Run:
   ```sh
   make yourcompany_yourfc_default menuconfig
   make yourcompany_yourfc_default boardconfig
   make yourcompany_yourfc_default
   ```

Why baseline-copy is required: PX4's NuttX defconfig changes frequently between releases. A complete generated defconfig from an arbitrary release is more likely to break than a delta applied to your checked-out PX4 branch.
