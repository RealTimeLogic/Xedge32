# Xedge32 Agent Guide

Xedge32 embeds the Barracuda App Server (BAS) and Xedge on ESP32, ESP32-S3,
and ESP32-P4. Use the shared BAS skills for native and Lua code; this guide
adds repository setup, board selection, and firmware build instructions.

## Start here

Read [README.md](README.md) and [the BAS agent guide](main/BAS/AGENTS.md).
Before editing, run `git status --short`, `git branch --show-current`, and
`git rev-parse HEAD`. Check status inside any submodule you will modify;
the parent repository can hide dirty submodule files. Preserve existing work.

| Work | Guidance |
| --- | --- |
| Native C/C++, startup, ownership, porting | [BAS/BWS native skill](main/BAS/skills/build-bas-bws-native-apps/SKILL.md) |
| Lua/LSP applications and runtime integration | [Xedge/Mako Lua skill](main/BAS/skills/build-xedge-mako-lua-apps/SKILL.md) |
| Lua/C bindings | [Binding guide](main/BAS/skills/build-xedge-mako-lua-apps/references/upstream/Lua-Binding-Skill.md), together with the native skill |
| GPIO and peripherals | [Hardware binding guide](main/BAS/skills/build-xedge-mako-lua-apps/references/hardware-bindings.md), together with the binding guide |

Read these files directly from the checkout; no global skill installation is
required. Keep shared guidance in BAS rather than duplicating it here. If an
agent tool needs installed skills, copy the complete `build-bas-bws-native-apps`
and `build-xedge-mako-lua-apps` folders from `main/BAS/skills` to that tool's
skill directory, including their references and scripts. Repository copies
remain the reference for this checkout.

## Set up the checkout

These Git commands work in Windows CMD or a Linux shell:

```text
git clone --recurse-submodules https://github.com/RealTimeLogic/Xedge32.git
cd Xedge32
git submodule update --init --recursive
```

For an existing checkout, run the last command after updating Xedge32, with
local submodule changes preserved first. It installs the recorded revisions;
do not use `--remote` for routine setup. Submodule upgrades must be deliberate.

Use a Linux shell, including WSL1 on Windows, for the resource/build commands
below. Install Git, zip, curl, and ESP-IDF as described in the
[README](README.md#compiling-the-c-code-for-experts), then activate the SDK
environment in that shell. Check `idf.py --version` and `echo "$IDF_PATH"`.
[main/idf_component.yml](main/idf_component.yml) currently requires
ESP-IDF `>=6.1,<6.2`; preserve its component pins unless upgrading them is part
of the task. Do not hand-edit generated managed-component integrity files.

## Select the board profile

These are the supplied development defaults, not universal settings for every
board using the same chip. All profiles enable PSRAM. Check actual memory,
chip revision, console connection, and peripheral wiring before flashing.

| Target | Target defaults | Supplied profile |
| --- | --- | --- |
| `esp32` | [sdkconfig.defaults.esp32](sdkconfig.defaults.esp32) | 4 MB flash, PSRAM, UART console; Ethernet disabled |
| `esp32s3` | [sdkconfig.defaults.esp32s3](sdkconfig.defaults.esp32s3) | 8 MB flash, octal PSRAM, native USB Serial/JTAG console; SPI W5500 support |
| `esp32p4` | [sdkconfig.defaults.esp32p4](sdkconfig.defaults.esp32p4) | 16 MB flash, PSRAM, pre-v3 chip profile, native USB Serial/JTAG console; Function EV Board Ethernet/ESP-Hosted and OV5647 camera profile |

The common defaults are in [sdkconfig.defaults](sdkconfig.defaults).
The root [CMakeLists.txt](CMakeLists.txt) also selects
[sdkconfig.defaults.camera](sdkconfig.defaults.camera) for ESP32/S3 when
`IDF_TARGET` is set. Keep that environment variable set as shown below.
Defaults seed new configurations; changing a defaults file does not replace
values already saved in an existing `sdkconfig`.

P4 has no integrated Wi-Fi radio. Its ESP-Hosted profile needs a compatible
ESP32-C6 companion and firmware; Ethernet does not establish that Wi-Fi works.
The IP101 example and wiring caveats are in
[GettingStarted.rst](doc/source/GettingStarted.rst). Check console selection
in `menuconfig`: a USB-to-UART adapter cannot provide input to a native USB
Serial/JTAG console. ROM output on UART followed by silence can be a console
switch, not a failed boot.

## Generate resources and build

From the Xedge32 root, generate the embedded resources before the first build
and after changing embedded Lua/UI resources:

```bash
# Interactive: select OPC UA resources to match the firmware configuration.
bash ./BuildESP32ResourceFile.sh
```

This creates `main/BAS/examples/xedge/obj/XedgeZip.c`. If it is missing, rebuild
the resources; do not create an empty replacement to satisfy CMake. The script
also asks which certificate bundle to include.

Use a separate build directory and configuration for each target. In Bash:

```bash
# Choose esp32, esp32s3, or esp32p4. Keep these paths for later actions.
target=esp32s3
export IDF_TARGET="$target"
build_dir="$PWD/build/$target"
config_file="$build_dir/sdkconfig"

# Optional board-specific configuration before building:
idf.py -B "$build_dir" -D SDKCONFIG="$config_file" menuconfig
idf.py -B "$build_dir" -D SDKCONFIG="$config_file" build
idf.py -B "$build_dir" -D SDKCONFIG="$config_file" size
```

Outputs include `xedge.bin`, `xedge.elf`, bootloader, and partition-table files
under the selected build directory. Do not reuse another target's build or
configuration. To validate changed defaults, use a fresh build/configuration
pair rather than deleting someone's configured build.

For the final three-target compile, after generating resources, run serially
because managed components and the dependency lock are shared by the checkout:

```bash
# Existing per-target configurations are retained; these are incremental builds.
for target in esp32 esp32s3 esp32p4; do
    IDF_TARGET="$target" idf.py -B "$PWD/build/$target" \
        -D SDKCONFIG="$PWD/build/$target/sdkconfig" build size || break
done
```

## Flash and validate

Use the same target, build directory, and configuration as the intended image.
Confirm the connected board and serial port. In WSL1, Windows COMn typically
maps to `/dev/ttySn`; Linux device names depend on the serial interface.
Close other monitors before flashing.

For an application update with a compatible existing partition layout, using
the variables from the single-target example above:

```bash
# Replace PORT with the actual board port. Preserve existing partitions/settings.
idf.py -B "$build_dir" -D SDKCONFIG="$config_file" -p PORT -b 230400 app-flash
idf.py -B "$build_dir" -D SDKCONFIG="$config_file" -p PORT monitor
```

A first installation or partition-layout change needs the matching bootloader,
partition table, and data images. Review the generated flash arguments before
using full `flash`: [main/CMakeLists.txt](main/CMakeLists.txt) includes NVS
(saved settings) and storage images in that operation. Do not reuse flash
offsets from another target. See [GettingStarted.rst](doc/source/GettingStarted.rst)
and [the partition guide](partitions/README.md).

Keep changes minimal and preserve compact argument parsing and single-character
switches. Follow BAS formatting, Lua ownership, and dispatcher-mutex rules.
Check [PatchRmt.cmake](main/PatchRmt.cmake) and
[PatchMdns.cmake](main/PatchMdns.cmake) when upgrading dependencies; their
source-fragment checks can intentionally stop configuration for review.

After shared native changes, compile all three targets before declaring the
work complete, unless the user specifies a narrower stage. Report image sizes
and distinguish compilation, flashing, console, network, peripheral, and
charger-only tests. Internal loopback does not prove external hardware timing.
Documentation-only changes need link/content checks, not firmware builds.
