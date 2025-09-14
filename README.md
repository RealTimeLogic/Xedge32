# Xedge32

### (Xedge Lua IDE and REPL for ESP32)

Looking to turn your ESP32 into a powerful [Edge Controller or IoT Gateway](https://realtimelogic.com/products/edge-controller/)? Look no further than Xedge32! This incredible development tool empowers users of all levels to program their ESP32 using the simple and intuitive Lua Scripting language. With Xedge32, you can easily unlock the full potential of your ESP32 and create advanced IoT solutions that will impress even the most tech-savvy enthusiasts.

![ESP32 Edge Controller](https://realtimelogic.com/images/xedge/v1/Xedge.png)

**Figure 1: Xedge32 in developer mode**

## Getting Started

You do not need to compile Xedge32. Compiling Xedge32 is for C code experts.

* [Download the Xedge32 Firmware](https://realtimelogic.com/downloads/bas/ESP32/)
* [Xedge32 Tutorials](https://realtimelogic.com/xedge32-tutorials/)
* [Xedge32 South Bridge API](https://realtimelogic.com/ba/ESP32/) - GPIO and HW APIs
* [How to use Xedge32 in developer mode](https://realtimelogic.com/ba/doc/en/Xedge.html)


## Understanding the Xedge32 Architecture

As outlined in the tutorial [Your First Xedge32 Project](https://realtimelogic.com/articles/Your-First-Xedge32-Project), Xedge32 is explicitly **[built for OEM integration](#xedge32-and-oem-integration)**. It's an ESP32 customized version of the more general-purpose Xedge framework, which itself is powered by the [Barracuda App Server](https://github.com/RealTimeLogic/BAS).

**For more details:**

* [Xedge product page](https://realtimelogic.com/products/xedge/)
* [Advanced compilation options including release build options](https://realtimelogic.com/ba/examples/xedge/readme.html)


## Compiling The C Code (for experts)

**Note:** [Xedge32](https://realtimelogic.com/ba/ESP32/) is built on the more generic [Xedge](https://realtimelogic.com/products/xedge/), which itself is based on the [Barracuda App Server library](https://realtimelogic.com/products/barracuda-application-server/). Xedge32, Xedge, and the Barracuda App Server are OEM software components designed for easy integration into OEM products. All components are [designed to be extended](https://realtimelogic.com/articles/Using-Lua-for-Embedded-Development-vs-Traditional-C-Code).

To compile the source code, you must use the latest ESP-IDF, which can be found on [GitHub](https://github.com/espressif/esp-idf).

The following Linux commands show all steps required for installing the development tools, downloading the required source code, and compiling the code:

```
   sudo apt -y update
   sudo apt -y install git wget zip flex bison gperf python3 python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

   cd
   # remove old installation, if any
   rm -rf .espressif esp/esp-idf

   # Install the latest esp-idf
   mkdir esp
   cd esp
   git clone --recursive https://github.com/espressif/esp-idf.git
   ./install.sh
   cd ..
   source esp-idf/export.sh

   # Download and update Xedge32
   git clone --recursive --recurse-submodules https://github.com/RealTimeLogic/xedge32.git xedge
   cd xedge
   git submodule update --init --remote

   # Build the Xedge resource file Xedge.zip, convert to C, and copy C file to Xedge directory
   chmod +x BuildESP32ResourceFile.sh
   ./BuildESP32ResourceFile.sh

   #set target to one of:
   #idf.py set-target esp32
   #idf.py set-target esp32s3

   # Configure Xedge32 options such as enabling CAM and mDNS. Details below.
   #idf.py menuconfig

   # Build the code
   idf.py build
```

Windows: The code can be compiled in a Linux console, including the Windows Subsystem for Linux (WSL). If you use WSL, it's recommended to use generation one (WSL1), as it can be difficult to get the USB serial working in WSL2. For more information, see [the WSL documentation](https://docs.microsoft.com/en-us/windows/wsl/about).

To upload the firmware to your ESP32, follow these steps:

1. Wait for the build process to complete.
2. Upload the code using:
   - Linux: idf.py flash monitor
   - WSL: idf.py -p /dev/ttyS4 -b 115200 flash monitor


# Configuring Xedge32

To configure Xedge32, use the `idf.py menuconfig` command. This allows you to enable various features such as mDNS, Camera, OPC UA, and softTPM eFuse registers. Below is an overview of each feature, along with configuration tips.

## Configuration Steps

### 1. Enable mDNS

mDNS (Multicast DNS) enables local network discovery, making it possible to access Xedge32 by navigating to `http://xedge32.local` in your browser. You can customize this name within your Lua script if desired.

### 2. Enable OPC UA

[OPC UA](https://realtimelogic.com/products/opc-ua/) is an industrial protocol useful for machine-to-machine communication. To enable OPC UA:

- Use `idf.py menuconfig` and select the OPC UA option.
- After configuring through menuconfig, ensure that you also choose "Yes" when prompted by `BuildESP32ResourceFile.sh`.

### 3. Enable softTPM eFuse

The softTPM eFuse option allows for secure storage of secrets directly in eFuse registers, making them permanently accessible on the device. This feature is part of the advanced security configuration settings. For full details on available configuration options, refer to the configuration section in the [generic Xedge build documentation](https://realtimelogic.com/ba/examples/xedge/readme.html).

## Embedding Lua Apps and configuring NVS

Lua apps and the Xedge configuration file can be embedded in the firmware binary using `fatfsgen.py` and  `nvs_partition_gen.py`. See the [Partitions Generator Utility](partitions/README.md) readme file for details.

## Xedge32 and OEM Integration

OEM integration means you are not using Xedge32 as a one-size-fits-all product. You are working with a platform designed to be embedded inside your firmware. Instead of starting from scratch, you extend and customize the platform to fit your specific needs, then ship it as an integral part of your product.

Xedge32 is built for precisely this. It gives you a solid base, and with Lua, you can add features, tweak behavior, and expose your own Lua to C APIs without modifying the core system. For a high-level conceptual view, check out the article [Using Lua for Embedded Development vs. Traditional C Code](https://realtimelogic.com/articles/Using-Lua-for-Embedded-Development-vs-Traditional-C-Code) and the tutorial on [creating Lua to C code APIs](https://tutorial.realtimelogic.com/Lua-Bindings.lsp).


## Export restrictions

This distribution includes cryptographic software. The country in 
which you currently reside may have restrictions on the import, 
possession, use, and/or re-export to another country, of 
encryption software.  BEFORE using any encryption software, please 
check your country's laws, regulations and policies concerning the
import, possession, or use, and re-export of encryption software, to 
see if this is permitted.  See http://www.wassenaar.org/ for more
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
