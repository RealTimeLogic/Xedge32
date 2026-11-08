# Xedge32

Build connected ESP32 products in Lua, right from your browser. Xedge32 brings
on-device application development to ESP32, ESP32-S3, and ESP32-P4, with the web
interfaces, networking, and hardware access you need for sensors, protocol
bridges, and [edge controllers](https://realtimelogic.com/products/edge-controller/).

Install prebuilt firmware and start developing. Add application logic in Lua
without rebuilding the firmware for every change. When your product needs
custom hardware support or native APIs, extend the C firmware. Xedge32 gives
you a working reference platform for Xedge and the Barracuda App Server (BAS),
with a path from your first prototype to an OEM integration.

![Xedge browser IDE](https://realtimelogic.com/images/xedge/v1/Xedge.png)

*Edit and run Lua applications on the device from your browser.*

## Getting Started

1. [Install the prebuilt firmware](https://realtimelogic.com/downloads/bas/ESP32/)
   on a supported board. The download page includes a browser installer.
2. Follow [Your First Xedge32 Project](https://realtimelogic.com/articles/Your-First-Xedge32-Project)
   to create and run a Lua application.
3. Explore the [Xedge32 tutorials](https://realtimelogic.com/xedge32-tutorials/),
   the [ESP32 hardware APIs](https://realtimelogic.com/ba/ESP32/), and the
   [Xedge development environment](https://realtimelogic.com/ba/doc/en/Xedge.html).
4. [Set up trusted HTTPS](#automatic-certificate-management) for access to your device.

You do not need to compile the firmware to get started. Build from source when
you need to change its configuration, native code, or embedded resources.

**Build and flash with an AI agent:** Give your AI coding agent
[AGENTS.md](AGENTS.md) for dependency setup, target selection, builds,
programming, and startup checks.

## Understanding the Xedge Architecture

Xedge32 combines three layers:

- [Barracuda App Server](https://github.com/RealTimeLogic/BAS) provides the
  embedded application server, Lua runtime, security, and networking services.
- [Xedge](https://realtimelogic.com/products/xedge/) adds the Lua application
  environment and browser-based development tools.
- Xedge32 integrates Xedge with ESP32 firmware and Lua APIs for ESP32 hardware.

This structure lets developers prototype in Lua and gives OEMs a foundation
they can [integrate and extend](#xedge32-and-oem-integration) in their own
firmware. For more Xedge build and release options, see the
[standalone Xedge build guide](https://realtimelogic.com/ba/examples/xedge/readme.html).

## Compiling the C Code (for experts)

Use a Linux shell to build the firmware. On Windows, you can use the
[Windows Subsystem for Linux](https://learn.microsoft.com/en-us/windows/wsl/about);
WSL1 is often more convenient when access to a Windows serial port is needed.
Install Git, zip, curl, and [ESP-IDF 6.1](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/get-started/index.html),
then activate the ESP-IDF environment in your shell. This checkout requires
ESP-IDF version 6.1 or later, but earlier than 6.2.

On Debian or Ubuntu, install the additional tools with:

```bash
sudo apt -y update
sudo apt -y install git zip curl
```

### Clone the repository

```bash
git clone https://github.com/RealTimeLogic/xedge32.git xedge
cd xedge
# Install the BAS and resource revisions required by this Xedge32 version.
git submodule update --init --recursive
```

For an existing checkout, pull the Xedge32 update and rerun the submodule
command above before building. If you already have a build directory, run
`idf.py reconfigure` to refresh its resource dependencies (include `-B` if
using a separate build directory).

The build embeds CA certificates and generates the matching Lua and web
resources automatically.

### Select a target and build

From the repository root, set `IDF_TARGET` to your board and select the same
target with `idf.py set-target`. Keep `IDF_TARGET` set in the shell while
configuring and building so Xedge32 loads the matching defaults. This example
selects ESP32-S3:

```bash
export IDF_TARGET=esp32s3
idf.py set-target esp32s3

# For another board, change both lines above to esp32 or esp32p4.
# idf.py set-target esp32
# idf.py set-target esp32p4

# Configure features and board-specific settings, then build.
idf.py menuconfig
idf.py build
```

The supplied defaults are development profiles, so check the board's flash,
PSRAM, console, and peripheral configuration before flashing. ESP32-P4 does
not have integrated Wi-Fi; the supplied ESP-Hosted profile requires a compatible
companion device and firmware. See the [getting started guide](https://realtimelogic.com/ba/ESP32/source/GettingStarted.html)
for board and connection details. Existing `sdkconfig` files retain their saved
values when defaults change.

#### IPv6 and large UDP packets

IPv6 follows `CONFIG_LWIP_IPV6` in `idf.py menuconfig`. The supplied defaults
enable IPv6, address autoconfiguration (SLAAC), and IPv6 reassembly. Existing
`sdkconfig` files keep their saved settings.

**Warning:** with IPv6 reassembly enabled, the tested ESP-IDF 6.1 stack on
ESP32-S3 asserted in `ip6_reass` and rebooted when receiving a fragmented IPv6
UDP payload of **65,488 bytes**; 65,487 bytes passed. The failure occurs in
lwIP before BAS receives the packet. Xedge32 uses the SDK's unmodified IPv6
reassembler and permits builds with reassembly enabled. It does not apply an
automatic lwIP patch; the native-stack issue remains until fixed in the SDK.

#### Optional: Separate builds for each target

To keep configurations and build outputs for multiple targets, use a separate
directory for each target. The following example uses ESP32-S3:

```bash
target=esp32s3
export IDF_TARGET="$target"
build_dir="$PWD/build/$target"
config_file="$build_dir/sdkconfig"

# Configure and build using the selected target's own paths.
idf.py -B "$build_dir" -D SDKCONFIG="$config_file" menuconfig
idf.py -B "$build_dir" -D SDKCONFIG="$config_file" build
```

### Embedded resources

`idf.py build` automatically generates the embedded Lua and web resources.
[Resources.cmake](main/Resources.cmake) calls
[build_resources.py](Build-Resources/build_resources.py) to select the PSA or
softTPM Lua implementation, optional OPC UA resources, and firmware-update
support for the target. The helper checks the archive contents and adds
compile-time checks for mismatched PSA and OPC UA settings.

The generated `XedgeZip.zip` and `XedgeZip.c` are stored under
`esp-idf/main/resources/` in the selected build directory. Each build has its
own resources instead of sharing one generated C file inside the BAS source
tree. This prevents a resource build for one configuration from overwriting
another's. Existing resource inputs and configuration changes trigger rebuilding.

No separate resource-generation step is required before building. To generate
only resources, call the `xedge-resources` build target directly:

```bash
# Generate only resources using the default build directory and configuration.
idf.py xedge-resources

# For the separate build/configuration paths shown above:
idf.py -B "$build_dir" -D SDKCONFIG="$config_file" xedge-resources
```

### Flash the firmware

Replace `PORT` with the board's serial port. For an application update on a
device with a compatible partition layout:

```bash
# Flash the application and open its serial console.
idf.py -p PORT -b 230400 app-flash
idf.py -p PORT monitor
```

For a separate build directory, add `-B "$build_dir" -D SDKCONFIG="$config_file"`
to both commands so they use the intended firmware and configuration.

A first installation or a partition-layout change also needs the matching
bootloader and partition table. Review the [getting started guide](https://realtimelogic.com/ba/ESP32/source/GettingStarted.html)
and [partition guide](partitions/README.md) before flashing. Factory provisioning
can replace configuration, application files, and PSA keys.

## Configuring Xedge32

Use `idf.py menuconfig` to select Xedge32 features and board settings. If you
use separate builds, include the `-B` and `SDKCONFIG` options shown above. Among
the available options:

- **Multicast DNS (mDNS):** Keep `CONFIG_mDNS_ENABLED` enabled for
  [SharkCA's local HTTPS names](#sharkca-and-mdns). It also provides the
  default `xedge32.local` name for development access.
- **[OPC UA](https://realtimelogic.com/products/opc-ua/):** Enable the
  industrial communication feature in menuconfig. The build includes matching
  Lua resources automatically.
- **Camera:** Configure the camera support and hardware to match your board.
- **TPM backend:** Keep the default softTPM, or select PSA Crypto on ESP32-S3
  and ESP32-P4. Read [TPM](#tpm) before changing an existing device's backend.

## Automatic Certificate Management

Start with the [introduction to automatic certificate management](https://realtimelogic.com/services/SharkTrust/) for the public and private trust options. Then follow the [Xedge certificate manual](https://realtimelogic.com/ba/doc/en/Xedge.html#cert) to configure SharkTrustX or SharkCA. The manual covers enrollment, renewal, portal credentials, and browser trust.

For IDE users and access control, follow the [Xedge authentication and configuration guide](https://realtimelogic.com/ba/doc/en/Xedge.html#conf).

### SharkCA and mDNS

Xedge's multicast DNS (mDNS) support is primarily for **SharkCA's local HTTPS names**. SharkCA issues the certificate; mDNS lets a browser find the device by its assigned `.local` name. Xedge manages that registration automatically. Connecting computers must also trust the zone's device CA root; resolving a name alone does not establish HTTPS trust.

Use the [SharkCA user manual](https://github.com/RealTimeLogic/SharkCA/blob/main/doc/user-manual.md) for portal and trust setup. Application-managed names are an optional use of the [mDNS Lua API](https://realtimelogic.com/ba/doc/en/lua/auxlua.html#mdns).

On ESP32, `CONFIG_mDNS_ENABLED` enables the native responder and the `ba.createmdns` API used by SharkCA. The development name `xedge32.local` can coexist with the certificate's name. See the [ESP32 hostname controls](https://realtimelogic.com/ba/ESP32/source/misc.html#esp32-execute-label) to change or disable that extra default name while retaining SharkCA's registration. Use the certificate's assigned hostname for trusted HTTPS.

## TPM

Xedge32 provides the Lua [Trusted Platform Module (TPM) API, `ba.tpm`](https://realtimelogic.com/ba/doc/en/lua/auxlua.html#TPM)
for named keys, signing, certificates, and encryption-key derivation. Applications
use key names instead of handling private-key files. Two backends implement
this API:

| Backend | Targets | Cryptographic implementation |
| --- | --- | --- |
| **softTPM (default)** | ESP32, ESP32-S3, ESP32-P4 | BAS softTPM and native SharkSSL cryptography |
| **PSA Crypto (optional)** | ESP32-S3, ESP32-P4 | ESP-IDF's provider of the Platform Security Architecture (PSA) Crypto API |

SharkSSL remains the TLS implementation in both modes. PSA changes its crypto
provider and the TPM key store; it does not replace SharkSSL with an mbedTLS
TLS server. Neither backend requires a separate TPM chip.

**Our recommendation: softTPM with native SharkSSL.** We believe softTPM is the
more secure choice for Xedge32 products on ESP32 chips than the flash-backed PSA
configuration supplied here. It derives named private keys when needed instead
of storing them as persistent private keys in flash. With a protected product
secret and device-specific input in eFuse, copying application storage alone
is not enough to reproduce a device's identity.

That protection depends on the product secret, firmware integrity, and correct
provisioning. softTPM runs in software, and its eFuse input is software-readable.
Our comparison is with the ESP32 PSA configuration used by Xedge32; a PSA
provider with hardware-isolated keys has a different security boundary.

**Faster HTTPS connections with SharkSSL.** Native SharkSSL delivered more than
twice the fresh HTTPS request rate of PSA on ESP32-S3 and more than five times
on the tested ESP32-P4 profile. That gives device web interfaces and clients
less time to wait when opening secure connections. See the
[HTTPS performance comparison](#https-performance) for measurements and settings.

**PSA support for API experimentation.** The optional backend makes ESP32-S3 and
ESP32-P4 useful platforms for exploring the standard PSA Crypto API, evaluating
provider integration, and testing BAS and SharkSSL with that interface. Choose
it when your project needs to work with PSA. Enabling the API does not by itself
provide hardware-isolated keys or PSA certification.

### softTPM

The [softTPM](https://realtimelogic.com/ba/doc/en/SoftTPM.html) derives named keys
from product-wide secret material and device-specific input. **For deployed
devices, eFuse is the preferred location for the device-specific input:** it
remains on the chip when application storage is erased or replaced and cannot
be lost by deleting a file.

Xedge32 defaults to storing this input in `softpmkey.bin` in application
storage because programming eFuse is permanent. The file-based default lets
you evaluate and develop with softTPM without making an irreversible change
to the chip. Preserve this file when retaining identities and data derived
from it.

**Before production, replace the example product secret.** The checkout includes
example key material in [EncryptionKey.h](https://github.com/RealTimeLogic/BAS/blob/main/examples/xedge/src/EncryptionKey.h).
Use a unique, confidential secret for each product or deployment and protect
it as part of the firmware design. The [BAS build guide](https://github.com/RealTimeLogic/BAS/blob/main/doc/compiling.md#xedge-rtos)
describes selecting a generated header with `XEDGE_SOFTTPM_KEY_HEADER`.
Changing the product secret on an existing device changes derived keys, so plan
the migration of its identities and encrypted data first.

To use eFuse for the device-specific input:

1. Confirm that the eFuse field used in [main/main.c](main/main.c) is available
   for this purpose on your target.
2. Run `idf.py menuconfig`, including your build and configuration paths if
   you use a separate build. Keep **Xedge config > Use PSA Crypto for Xedge**
   disabled; this setting applies to softTPM.
3. Enable **Xedge config > softTPM > Use eFuse**. This sets the Boolean option
   `CONFIG_softTPM_EFUSE_ENABLED=y` (default: disabled).
4. Save, rebuild, and flash the firmware. At startup, Xedge32 generates and
   permanently writes a random value if the field is empty; subsequent starts
   reuse the stored value.

Enabling this option does not copy `softpmkey.bin` into eFuse. On an existing
device, plan migration of identities and encrypted data before changing the
input. Software still reads the eFuse value; this is not hardware-isolated
key storage.

### PSA Crypto

Run `idf.py menuconfig`, including your build and configuration paths if you
use a separate build, and enable **Xedge config > Use PSA Crypto for Xedge**.
The Boolean option `CONFIG_XEDGE_PSA` defaults to disabled. Build normally;
the build selects matching C definitions and Lua TPM resources automatically.
Use `ba.tpm` for private-key operations. PEM-based private-key Lua APIs are
unavailable in this mode.

PSA builds automatically create a persistent device root in the separate
`psa` Non-Volatile Storage (NVS) partition. Xedge32 derives both `globalkey`
and `uniquekey` from that root, making encrypted configuration device-specific.
Unlike softTPM's product-wide `globalkey`, it is not shared across devices.
Losing the root makes data encrypted with its derived keys unreadable; creating
a new root does not recover that data.

The ESP-IDF provider stores persistent keys in NVS and can use supported
on-chip crypto accelerators. Acceleration does not imply key isolation:
hardware-protected keys require a suitable provider, key location, and chip
revision. Enabling this option alone does not supply that protection.
Protect production devices with appropriate ESP32 security features, including
secure boot and storage encryption. **Enabling NVS encryption for the default
`nvs` partition does not automatically encrypt the separate `psa` partition.**
Xedge32 initializes `psa` without NVS encryption. Encrypting it requires
additional firmware integration, key provisioning, and validation;
see [ESP-IDF's custom NVS encryption guidance](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/storage/nvs_encryption.html#encrypted-read-write).

### Random-generator startup

Native builds initialize the heap first, enable the ESP-IDF bootloader entropy
source for a short interval, fill a 48-byte buffer, and disable that source
before network, ADC or other peripheral use. Startup checks
`sharkssl_entropyEx` and aborts on failure. The allocator's lifetime cookie
does not depend on the cryptographic generator. PSA builds keep provider-owned
initialization and do not inject native seed data.

The BAS Lua bootstrap preserves earlier entropy contributions. Shared Xedge
resources can later add public Internet data, after the ACME clock check
`(os.time()+86400) > xedge.compileTime` or an SNTP event. This is supplemental
input; it does not replace hardware entropy or protect keys generated earlier.
`ba.rndseed` accepts actual numbers through its compatibility adapter and
32..65535-byte strings unchanged. PSA returns `nil,error` for this function.

### HTTPS performance

Native SharkSSL gives Xedge32 a clear advantage when clients open fresh HTTPS
connections. In our ESP-IDF 6.1 tests, it served **over twice as many requests
per second on ESP32-S3** and **over five times as many on ESP32-P4** as the PSA
configurations below:

| Target | Native SharkSSL / softTPM | PSA | Native request-rate advantage |
| --- | ---: | ---: | ---: |
| ESP32-S3 | 6.12 requests/s | 2.87 requests/s | 2.1x |
| ESP32-P4 | 12.86 requests/s | 2.39 requests/s | 5.4x |

Each backend completed 48 of 48 requests at 16 concurrent clients with a
30-second socket timeout. Every request opened a full TLS 1.3 connection,
using a TPM-backed P-384 server certificate and AES-256-GCM, without session
resumption. These measurements cover connection setup and short HTTPS requests.
Signing and sustained encrypted transfers have different performance profiles;
PSA can be faster for those operations.

The S3 test used a 240 MHz ESP32-S3 with 8 MiB PSRAM and Wi-Fi. Its PSA provider
used `-O2` and fixed-point optimization. The P4 test used a 360 MHz revision 1.3
chip with 32 MiB PSRAM and IP101 Ethernet; its PSA provider used size optimization,
no fixed-point precomputation, and software ECDSA. The P4 result applies to that
profile, not to newer chip revisions with hardware-accelerated ECDSA.

#### Tuning the PSA provider on ESP32-S3

Xedge32's S3 defaults select these provider options for faster elliptic-curve
operations:

| Boolean option | Setting |
| --- | --- |
| `CONFIG_MBEDTLS_COMPILER_OPTIMIZATION_PERF` | Enable **Optimize for performance (-O2)** |
| `CONFIG_MBEDTLS_ECP_FIXED_POINT_OPTIM` | Enable **fixed-point multiplication optimisations** |

For an existing S3 configuration, use menuconfig search (`/`) to check both
options; saved `sdkconfig` values take precedence over defaults. These settings
affect the ESP-IDF crypto provider. PSA remains optional and disabled by default.
Measure your application's connection latency, transfer rate, and memory use
when choosing a backend or tuning its provider.

### Retaining keys and changing backends

The Lua API is shared, but the keys and encrypted data are not interchangeable.
Before switching from softTPM to PSA, switching back, or changing a PSA
derivation profile, recover the data while its existing keys are still
available and plan its migration, or deliberately reset it. Certificates need
matching keys or re-enrollment. There is no automatic migration or fallback.

### PSA key storage and configuration recovery

ESP32-S3 and ESP32-P4 keep PSA keys in a separate `psa` NVS partition. Ordinary
flashing and application OTA preserve data partitions, but factory images and
explicit partition-flash commands can erase settings, files, or keys. Preserve
the `psa` partition when retaining device identities and encrypted data. Check
the partition guide before changing the storage layout or reprovisioning keys.

See [PSA key storage and recovery](partitions/README.md#psa-key-storage-s3p4)
for the partition layout, recovery behavior, and provisioning details.

## HTTP connection and socket capacity

ESP32-S3 and ESP32-P4 provide capacity for browser requests alongside
background services. The defaults are the same with softTPM or PSA:

| Target | HTTP connection slots | lwIP sockets | TCP accept queue |
| --- | ---: | ---: | ---: |
| ESP32-S3 | **16** | **32** | **16** |
| ESP32-P4 | **16** | **32** | **16** |
| Classic ESP32 | 8 | 16 | 6 |

These are three different budgets. The HTTP pool holds incoming HTTP/HTTPS
connections, including pending requests and idle keep-alive connections. The
lwIP socket budget also serves listeners, WebSockets, UDP and outgoing clients.
The accept queue holds established TCP connections waiting for the application
to accept them, separately for each listener. A browser can use several
connections; none of these numbers is a browser-user limit.

**PSA does not impose a separate 16-socket limit.** The S3/P4 defaults come from
[sdkconfig.defaults.esp32s3](sdkconfig.defaults.esp32s3) and
[sdkconfig.defaults.esp32p4](sdkconfig.defaults.esp32p4). For an existing S3/P4
configuration, use menuconfig search (`/`) to set the integer options
`LWIP_MAX_SOCKETS` to `32` and `LWIP_TCP_ACCEPTMBOX_SIZE` to `16`, then rebuild.
Saved `sdkconfig` values take precedence over defaults. Both S3 release console
variants and the P4 release use these settings with either crypto backend.

The HTTP connection pool is configured separately from lwIP's socket budget.
Increasing the socket count alone does not enlarge the HTTP pool. When the pool
is full, BAS may reclaim a pooled connection or reject a new one.

Both S3 and P4 completed the [HTTPS benchmark](#https-performance) at 16
concurrent clients with both backends. At 32 clients, both backends completed
16 of 48 requests with the same 30-second socket timeout and remained responsive
after the burst. Treat these as workload measurements, not browser-user limits.
Reuse HTTPS connections where possible, and size your application for its peak
RAM use, long-lived connections, and acceptable response time.

## Embedding Lua Apps and configuring NVS

For manufacturing, you can prepare filesystem and NVS images containing Lua
applications, Xedge configuration, certificates, and network settings. See the
[partition guide](partitions/README.md) for image generation and provisioning.

## Xedge32 and OEM Integration

Xedge32 is a starting point for OEM products that need local control, connected
services, or a device web interface. Embed and configure the platform for your
product, add application behavior in Lua, and extend the firmware with C APIs
for hardware or native functionality. Lua complements the native firmware and
lets you update application logic without rebuilding it for every change.

Learn more about [using Lua alongside C for embedded development](https://realtimelogic.com/articles/Using-Lua-for-Embedded-Development-vs-Traditional-C-Code)
and [creating Lua-to-C bindings](https://tutorial.realtimelogic.com/Lua-Bindings.lsp).

## Xedge on Other Platforms

Xedge brings the same browser-based Lua and LSP development workflow to several
embedded platforms. Explore the port that fits your hardware and RTOS.

- [Xedge for ThreadX](https://github.com/RealTimeLogic/Xedge4ThreadX): Xedge integrated with ThreadX, NetX Duo, and FileX.
- [Xedge for Zephyr](https://github.com/RealTimeLogic/Xedge4Zephyr): Xedge for the Zephyr RTOS.
- [Xedge for FreeRTOS](https://realtimelogic.com/downloads/bas/rt1020/): Ready-to-run project for the NXP i.MX RT1020 evaluation board.
- [Xedge for NuttX](https://nuttx.apache.org/docs/latest/applications/examples/xedge_demo/index.html): Example and build instructions in the Apache NuttX documentation.
- [Generic Xedge port](https://github.com/RealTimeLogic/BAS/blob/main/doc/compiling.md#xedge-rtos): BAS build instructions for integrating Xedge with your own RTOS target.

## Licensing and Dependencies

See this repository's [LICENSE](LICENSE), the [BAS license](main/BAS/LICENSE),
and the [BAS-Resources license](main/BAS-Resources/LICENSE). Other dependencies
retain their own licenses and notices.

## Export restrictions

This distribution includes cryptographic software. The country in
which you currently reside may have restrictions on the import,
possession, use, and/or re-export to another country, of
encryption software. BEFORE using any encryption software, please
check your country's laws, regulations and policies concerning the
import, possession, or use, and re-export of encryption software, to
see if this is permitted. See https://www.wassenaar.org/ for more
information.

The U.S. Government Department of Commerce, Bureau of Industry and
Security (BIS), has classified this software as Export Commodity
Control Number (ECCN) 5D002.C.1, which includes information security
software using or performing cryptographic functions with asymmetric
algorithms. The form and manner of this distribution makes it
eligible for export under the License Exception ENC Technology
Software Unrestricted (TSU) exception (see the BIS Export
Administration Regulations, Section 740.13) for both object code and
source code.

The following page provides details on the included cryptographic
software: https://github.com/RealTimeLogic/SharkSSL
