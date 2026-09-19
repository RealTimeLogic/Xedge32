---
name: xedge32-lua-bindings
description: Design, review, and implement Lua bindings specifically for Xedge32 on ESP32 and ESP-IDF. Use when adding or modifying Xedge32 native Lua APIs such as esp32.* or ba.* functions, ESP32 hardware userdata, GPIO/I2C/UART/ADC/RMT/PCNT/LEDC/camera/SD/network/OTA bindings, interrupt or FreeRTOS event callbacks into Lua, xedgeOpenAUX integration, or installESP32Libs registration.
---

# Xedge32 Lua Binding Skill

Use this skill for Lua bindings in Xedge32 on ESP32-class targets.

Read the generic BAS Lua binding skill first:

```text
https://realtimelogic.com/downloads/ai-skills/Lua-Binding-Skill.md
```

If that URL is unavailable, stop and say it is unreachable. Do not invent BAS rules. This skill is a specialization, not a replacement. The generic skill supplies the Lua C API, BAS mutex, callback, weak-reference, IoIntf, request/response, directory-object, and C/C++ boundary rules. The Xedge32 rules below override generic Xedge guidance when they name a more specific Xedge32 startup point or helper.

## Minimal Terms

- BAS: Barracuda App Server, the C/C++ web runtime that owns the Lua state and the `ba` Lua table.
- Xedge: a BAS-based runtime that starts Lua applications through Xedge startup hooks.
- Xedge32: the ESP32/ESP-IDF port of Xedge.
- ESP32 target: one chip family selected at build time, such as ESP32, ESP32-S2, ESP32-S3, or ESP32-P4.
- ESP-IDF: Espressif's C SDK for ESP32 targets. Its status type is `esp_err_t`; success is `ESP_OK`.
- Lua binding: C or C++ code that exposes native functions or objects to Lua.
- Userdata: a Lua object whose memory is allocated by C and checked with `luaL_checkudata`.
- Uservalue: a Lua value attached to a userdata so Lua's GC keeps it alive.
- GC: Lua garbage collection. A `__gc` method may run even when construction partly failed.
- Weak reference: a BAS registry reference created with `balua_wkRef` and later pushed with `balua_wkPush`; use it when async C jobs must recover a Lua callback.
- Dispatcher mutex: the BAS mutex protecting the Lua state. In Xedge32 root bindings it is named `soDispMutex`.
- `LThreadMgr`: BAS worker manager used to run callbacks in a Lua-safe BAS thread.
- `ThreadJob`: a BAS job object scheduled on `LThreadMgr`.
- `msgh`: the Lua error-handler stack index passed to a `ThreadJob` callback by `LThreadMgr`; pass it to `lua_pcall`.
- `eventBrokerQueue`: FreeRTOS queue used to move ISR, driver, lwIP, or event callbacks into safe Xedge32 callback code.
- ISR: interrupt service routine. It must not call Lua.
- lwIP: the TCP/IP stack used by ESP-IDF. Its callbacks are not Lua-safe.
- DMA: direct memory access. Some ESP-IDF drivers require DMA-capable memory.
- PSRAM: external RAM on some ESP32 boards. It may use pins that must be reserved.
- NVS: ESP-IDF non-volatile storage used for saved configuration.
- VFS: ESP-IDF virtual filesystem layer used by console, SD, and FAT support.
- `IoIntf`/`DiskIo`: BAS file I/O abstractions. `DiskIo` is a concrete disk-backed `IoIntf`.
- CSI, PPA, JPEG encoder: ESP32-P4 camera pipeline components used by `BaCamV4L2.c`.
- OTA: over-the-air firmware update support.
- Kconfig: ESP-IDF build-time configuration macros such as `CONFIG_CAM_ENABLED` and `CONFIG_IDF_TARGET_ESP32S3`.
- `activeGPIO`: root-level pin ownership table. Non-NULL means the pin is already reserved or in use.

## Source Scope

For Xedge32 binding work, inspect only root-level `.c` and `.h` files in:

```text
main
```

Use a non-recursive file listing, for example:

```powershell
Get-ChildItem -LiteralPath 'main' -File |
  Where-Object { $_.Extension -in '.c','.h' }
```

Root files analyzed for this skill:

- [`main/BaESP32.c`](main/BaESP32.c): main ESP32 Lua binding implementation. Defines `esp32Lib`, `basLib`, `installESP32Libs`, `lNewUdata`, `pushEspRetVal`, `activeGPIO`, `soDispMutex`, and most ESP32 hardware bindings.
- [`main/BaESP32.h`](main/BaESP32.h): shared declarations for `installESP32Libs`, `eventBrokerQueue`, `eventBrokerTask`, `lNewUdata`, `pushEspRetVal`, `lInitConfigTable`, `lsdcard`, `activeGPIO`, `ltMgr`, and `soDispMutex`.
- [`main/main.c`](main/main.c): ESP-IDF `app_main`, Xedge32 startup, `xedgeOpenAUX`, SD-card binding, `xedgeInitDiskIo`, and Lua shell dispatch through `LThreadMgr`.
- [`main/NetESP32.c`](main/NetESP32.c) and [`main/NetESP32.h`](main/NetESP32.h): network configuration, Wi-Fi/Ethernet/AP transitions, event broker queue creation, `_XedgeEvent` dispatch into Lua, `wifiScan`, and `netConnect`.
- [`main/CfgESP32.c`](main/CfgESP32.c) and [`main/CfgESP32.h`](main/CfgESP32.h): NVS-backed persistent configuration for mDNS name, SD card, and network settings.
- [`main/BaCam.c`](main/BaCam.c): camera binding for non-ESP32-P4 targets using `esp_camera`.
- [`main/BaCamV4L2.c`](main/BaCamV4L2.c): ESP32-P4 camera binding using V4L2, CSI, I2C master, PPA, and JPEG encoder while preserving the `esp32.cam` API shape.
- [`main/console.c`](main/console.c): ESP-IDF console/VFS setup. Treat it as startup support, not a normal Lua binding location.

Do not infer Xedge32 behavioral patterns from subdirectories unless the user widens scope. It is still valid to read BAS or ESP-IDF headers to verify declarations used by root files. If a change needs build files outside the analyzed `.c` and `.h` root scope, state that explicitly and either keep the implementation in an existing compiled root file or ask for permission to inspect the build file.

## First Workflow

When asked to add or review an Xedge32 Lua binding:

1. Read the generic BAS Lua binding skill at https://realtimelogic.com/downloads/ai-skills/Lua-Binding-Skill.md.
2. Read this skill.
3. Read the relevant root `.c`/`.h` files listed above. Start with [`main/BaESP32.c`](main/BaESP32.c), [`main/BaESP32.h`](main/BaESP32.h), and [`main/main.c`](main/main.c); add [`main/NetESP32.c`](main/NetESP32.c), [`main/NetESP32.h`](main/NetESP32.h), [`main/CfgESP32.c`](main/CfgESP32.c), [`main/CfgESP32.h`](main/CfgESP32.h), or camera files only when the requested API touches them.
4. Classify the binding:
   - `esp32.*` hardware/runtime function or object: normally implement in `BaESP32.c` and register in `esp32Lib`.
   - `ba.*` helper: register in `basLib` only when it intentionally extends the generic BAS `ba` table.
   - SD card or Xedge startup integration: check [`main/main.c`](main/main.c).
   - Network or `_XedgeEvent` behavior: check `NetESP32.c` and `CfgESP32.c`.
   - Camera behavior: check [`main/BaCam.c`](main/BaCam.c) and [`main/BaCamV4L2.c`](main/BaCamV4L2.c).
5. Decide ownership before coding: native handle owner, GPIO pins owned, callback owner, queue/task owner, and close/GC cleanup path.
6. Decide whether any operation blocks or runs from an unsafe callback context.
7. Pick the registration point and target/Kconfig guards.
8. Implement the smallest change consistent with existing root patterns.
9. Review with the checklist at the end of this skill.

If the request does not identify the ESP32 target or feature guard, infer only when the source code already has a matching guard. Otherwise ask for the target or make the binding conditional.

The root Xedge32 code is C. Prefer C for new root bindings. Use C++ only when the user asks for it or the existing build already compiles the target file as C++.

## Runtime Model

Xedge32 startup is:

1. `app_main()` initializes console support and calls `initComponents()`.
2. `initComponents()` initializes NVS config, opens the SD card if configured, and starts a BAS `Thread` running `mainServerTask`.
3. `mainServerTask()` initializes the BAS memory pool and calls `barracuda()`, implemented by the Xedge runtime.
4. Xedge calls `xedgeOpenAUX(XedgeOpenAUX* aux)` after core Xedge/BAS objects exist and before the Lua app starts.
5. `xedgeOpenAUX` calls `installESP32Libs(aux->L)` before `netInit()`. Preserve this order. Network and event callbacks need `soDispMutex`, and `installESP32Libs` initializes it.
6. `installESP32Libs` clears and reserves entries in `activeGPIO`, constructs a small local mutex, sets `soDispMutex = HttpServer_getMutex(ltMgr.server)`, creates the global `esp32` table from `esp32Lib`, and adds `basLib` functions to global `ba`.

Generic Xedge guidance says product bindings go in `xedgeOpenAUX`. For Xedge32 root ESP32 APIs, use `installESP32Libs` as the installer and keep `xedgeOpenAUX` as the coordinator that calls it.

## API Placement

Use the current layout:

- `esp32.*`: ESP32 hardware, board, and runtime functions. Add to `esp32Lib`.
- `ba.*`: generic BAS table extensions. Add to `basLib` only when the API is not ESP32-specific or intentionally belongs on `ba`. Current root code adds `ba.mac`.
- `ba.create.*`: use only for BAS directory objects or APIs that must behave like BAS factories. Do not use it for ordinary ESP32 hardware userdata.
- `require("...")`: use only when the user explicitly asks for a separately loadable Lua module. Root Xedge32 bindings are compiled into the runtime.

Current `esp32Lib` names:

```text
adc, cam, gpio, i2cmaster, pwmtimer, pcnt, pwmchannel, uart,
wscan, netconnect, apinfo, mac, crc, execute, sdcard, loglevel,
rmttx, rmtrx, ota
```

`cam` is under `CONFIG_CAM_ENABLED`. `ota` is currently under `CONFIG_IDF_TARGET_ESP32S3`. Preserve existing guards unless changing the feature intentionally.

## Userdata Pattern

Most root Xedge32 hardware bindings use ordinary Lua userdata, not BAS typed userdata.

Use `lNewUdata(L, size, typeName, methods)` for new hardware objects. It:

- calls `lua_newuserdatauv(L, size, 1)`;
- zeroes the memory;
- creates the metatable on first use;
- sets `metatable.__index = metatable`;
- registers methods with no BAS upvalue;
- sets the metatable on the userdata.

Use local helpers:

- `*_getUD(L)` returns `luaL_checkudata(L, 1, TYPE_NAME)`.
- `*_checkUD(L)` calls `*_getUD`, rejects closed/uninitialized state, and returns the object.
- `*_close(L)` is idempotent and handles both explicit `close` and GC.

Minimum shape:

```c
#define BAMYDEV "MYDEV"

typedef struct {
   int callbackRef;
   gpio_num_t pin;
   mydev_handle_t handle;
} LMyDev;

static LMyDev*
MyDev_getUD(lua_State* L)
{
   return (LMyDev*)luaL_checkudata(L, 1, BAMYDEV);
}

static LMyDev*
MyDev_checkUD(lua_State* L)
{
   LMyDev* o = MyDev_getUD(L);
   if(!o->handle)
      luaL_error(L, "MYDEV closed");
   return o;
}

static int
MyDev_close(lua_State* L)
{
   LMyDev* o = MyDev_getUD(L);
   if(o->handle) {
      mydev_handle_t h = o->handle;
      o->handle = 0;
      activeGPIO[o->pin] = 0;
      mydev_del(h);
   }
   return 0;
}

static const luaL_Reg myDevLib[] = {
   {"close", MyDev_close},
   {"__close", MyDev_close},
   {"__gc", MyDev_close},
   {NULL, NULL}
};
```

Before using the skeleton, replace `mydev_handle_t` and `mydev_del` with real ESP-IDF types/functions and add all owned resources. If the object does not own a GPIO pin, omit the `pin` field and all `activeGPIO` operations. If it owns multiple pins, store and release all of them. If construction can fail after allocating userdata, the zeroed object must still be safe for `__gc`.

## Error Handling

Use these Xedge32 conventions:

- Return `true` when an ESP-IDF operation returns `ESP_OK`.
- Return `nil, err` for ESP-IDF failures with `pushEspRetVal(L, err, msg, throwOnInvArg)`.
- Use `throwOnInvArg=TRUE` when `ESP_ERR_INVALID_ARG` means caller error.
- Use `luaL_argerror` or `luaL_error` for invalid Lua arguments, invalid pins, missing callbacks, invalid modes, duplicate pin use, and use-after-close.
- Use `ESP_ERROR_CHECK` only for startup invariants or operations existing code treats as fatal. Do not use it for normal Lua-callable runtime errors.
- Use `pushErr(L, msg)` only when matching an existing local `nil, msg` API shape.

Typical Lua-callable ESP-IDF wrapper:

```c
esp_err_t err = my_esp_call(...);
return pushEspRetVal(L, err, "my_esp_call", TRUE);
```

Do not return raw `esp_err_t` values unless the Lua API explicitly documents them.

## Config Tables

For optional Lua config tables:

- Call `lInitConfigTable(L, ix)` before reading the table.
- Use `balua_getIntField`, `balua_checkIntField`, `balua_getBoolField`, `balua_checkStringField`, and `balua_getStringField`.
- Use named table fields once a constructor has more than two or three options.
- Use `callback` as the callback field name.
- Document defaults and Kconfig/target requirements.

Existing shapes to preserve when extending APIs:

```lua
esp32.gpio(pin, mode, { pullup=true, pulldown=false, type="...", callback=function(level) end })
esp32.adc(unit, channel, { attenuation="11db", volt=false, filter="mean", callback=function(value) end })
esp32.i2cmaster(port, sda, scl, speed)
esp32.rmttx{ gpio=..., resolution=..., mem=..., queue=..., priority=..., invert=false, dma=false }
esp32.rmtrx{ gpio=..., resolution=..., callback=function(symbols) end }
esp32.uart(port, { baudrate=9600, rxpin=..., txpin=..., callback=function(event, data) end })
```

## Pin Ownership

Use `activeGPIO` for pins exposed by Lua:

1. Validate each GPIO number.
2. Check every required pin for existing ownership before creating native resources.
3. Reject non-NULL entries with `throwPinInUse` or an equivalent Lua error.
4. Reserve pins only after the native object is far enough constructed for close/failure cleanup to release them.
5. Release pins in every close and construction-failure path.
6. Preserve `RESERVED_PIN` entries created in `installESP32Libs` for flash and PSRAM pins.

For multi-pin devices, check all pins first, then reserve them as a group. For peripherals that claim pins internally, still reserve the externally visible pins when Lua accepted them from the caller.

## Blocking and Mutex Rules

The Lua C API may be used only while the BAS dispatcher mutex is held. In Xedge32 root bindings, that mutex is `soDispMutex`.

For blocking ESP-IDF calls:

1. Copy all Lua values needed by the blocking call into C-owned memory or C scalars.
2. Release `soDispMutex`.
3. Run only non-Lua C/ESP-IDF code.
4. Reacquire `soDispMutex`.
5. Push Lua return values or errors.

Pattern:

```c
ThreadMutex_release(soDispMutex);
err = blocking_esp_call(...);
ThreadMutex_set(soDispMutex);
```

Existing blocking examples include camera capture, UART read/write, ADC continuous close, Wi-Fi scan, and network connect.

## Interrupt and Event Bridge

Never call Lua directly from an ISR, ESP-IDF driver callback, lwIP callback, or arbitrary FreeRTOS task. Bridge into Lua in stages:

1. Unsafe context creates a small `EventBrokerQueueNode`.
2. ISR code uses `xQueueSendFromISR`; non-ISR code uses `xQueueSend`.
3. `eventBrokerTask` receives the node and calls its C broker callback.
4. The broker callback creates a `ThreadJob` and schedules it with `LThreadMgr_run(&ltMgr, job)` while holding `soDispMutex`.
5. The `ThreadJob` function runs in an `LThreadMgr` thread and may call Lua using the supplied `msgh` error handler.

For GPIO-like jobs, use the existing `dispatchThreadJob(pin, runFunction)` helper when it fits. For non-GPIO jobs, follow the `NetESP32.c` pattern: allocate a `ThreadJob`-derived struct, copy any parameters that might expire, queue through `eventBrokerQueue`, and free copied memory after `lua_pcall`.

## Callback Lifetime

For userdata-owned async callbacks:

- Store the Lua callback in uservalue slot 1 so GC keeps it alive for the userdata lifetime.
- Create a weak reference with `balua_wkRef` when a later `ThreadJob` must push the callback without having the userdata on the stack.
- Use `lReferenceCallback(L, userdataIx, callbackIx)` for the local Xedge32 pattern.
- Push callbacks in jobs with `balua_wkPush(L, callbackRef)`.
- Stop or close the owning object on repeated callback failure if the source would otherwise keep producing events.
- Release weak refs during close when adding new code and a `balua_wkUnref` declaration is available in the target headers.

Bound or coalesce high-rate events. Existing GPIO, PCNT, ADC, RMT, and UART paths avoid unbounded Lua callback queues.

## FreeRTOS and Memory Rules

- ISR code must use ISR-safe ESP-IDF/FreeRTOS APIs only.
- lwIP and ESP-IDF event callbacks should queue work instead of locking `soDispMutex` directly when that can deadlock.
- Use `baMalloc`, `baFree`, and `baStrdup` for BAS-owned async memory.
- Use ESP-IDF allocators such as `heap_caps_*` only when DMA, PSRAM, alignment, or driver ownership requires them.
- Use `baFatalE(FE_MALLOC, 0)` only for allocations that existing async plumbing treats as unrecoverable.
- Free copied async parameters in the `ThreadJob` function after the Lua call.

## Target and Kconfig Guards

Do not assume all ESP32 targets support the same peripherals.

Use existing guards as the model:

- `CONFIG_CAM_ENABLED` for camera registration.
- `!defined(CONFIG_IDF_TARGET_ESP32P4)` for the classic `esp_camera` path.
- `defined(CONFIG_IDF_TARGET_ESP32P4)` for the V4L2/CSI/PPA/JPEG camera path.
- `CONFIG_IDF_TARGET_ESP32`, `CONFIG_IDF_TARGET_ESP32S2`, `CONFIG_IDF_TARGET_ESP32S3`, and `CONFIG_IDF_TARGET_ESP32P4` for target branches.
- `SOC_*` capability checks such as `SOC_LEDC_SUPPORT_HS_MODE` and `SOC_SDMMC_USE_GPIO_MATRIX`.
- ADC calibration scheme guards.
- `ESP_IDF_VERSION` guards for changed ESP-IDF struct fields.
- Existing Wi-Fi, Ethernet, mDNS, camera, SD-card, OTA, and console `CONFIG_*` guards.

If a peripheral is unsupported on a target, omit the Lua API under the same guard or fail at compile time with a clear reason. Do not register a function that can crash because the hardware or driver is absent.

To choose a guard, search the root files for the closest existing peripheral or target guard before adding a new one. Prefer reusing established `CONFIG_*`, `SOC_*`, or `ESP_IDF_VERSION` checks over inventing a new macro.

## Network, SD Card, Config, and Camera

Use existing subsystem helpers instead of parallel state:

- Network config uses `netConfig_t`, `cfgGetNet`, `cfgSetNet`, and `netConnect`.
- Valid network adapters are `close`, `wifi`, `IP101`, `RTL8201`, `LAN87XX`, `DP83848`, `KSZ80XX`, `W5500`, and `DM9051`.
- Network events call global Lua `_XedgeEvent` through the event bridge. Do not call `_XedgeEvent` directly from ESP-IDF callbacks.
- SD-card config uses `cfgSetSdCard`; successful `esp32.sdcard` changes restart the ESP32.
- `xedgeOpenAUX` registers SD card `DiskIo` as `ba.openio("sd")` when `gotSdCard` is set.
- Persistent Xedge32 config lives in NVS namespace `xedge` through `CfgESP32.c`.
- Keep `esp32.cam` API compatibility between [`main/BaCam.c`](main/BaCam.c) and [`main/BaCamV4L2.c`](main/BaCamV4L2.c) unless the user asks for a target-specific feature.

## Adding a Binding

For a new `esp32.mydev` binding:

1. Add the `LMyDev` struct, helpers, methods, and constructor near related bindings in `BaESP32.c`.
2. Include only the ESP-IDF headers needed by that binding.
3. Add `{ "mydev", lmydev }` to `esp32Lib` under the correct guards.
4. Add declarations to `BaESP32.h` only if another root file must call the function.
5. If a new root `.c` file is necessary, check whether the build system compiles it. If build files are outside the allowed scope, stop and ask before editing them.
6. Keep install-time initialization in `installESP32Libs` only for shared state needed before Lua code or callbacks can use the binding.
7. Preserve stack balance in installer functions.
8. Verify constructor, each method, invalid arguments, repeated close, GC close, pin reuse, callback failure, and target guards.

## Verification

After code changes:

1. Re-read the edited functions and confirm Lua stack balance for each return path.
2. Confirm every constructor failure path releases resources acquired earlier.
3. Confirm `close`, `__close`, and `__gc` tolerate zeroed or partially constructed userdata.
4. Confirm unsafe callback paths reach Lua only through `eventBrokerQueue` and `LThreadMgr`.
5. If the existing repo build/test instructions are in scope, use them. If they are outside the permitted scope, report that build verification needs the build files or project instructions and provide the static checks completed.
6. For target-guarded code, state which ESP32 target and Kconfig combination was reviewed or built.

## Review Checklist

Before finalizing:

- Did the agent read the generic BAS skill and the relevant root Xedge32 files?
- Is the API registered in `esp32Lib`, `basLib`, or a deliberately chosen custom installer?
- Is `xedgeOpenAUX -> installESP32Libs -> netInit` ordering preserved?
- Are compile-time guards correct for the ESP32 target and ESP-IDF version?
- Does every Lua-visible ESP-IDF failure use `pushEspRetVal` unless it is a programmer error?
- Does every owning userdata have idempotent `close`, `__close`, and `__gc`?
- Can `__gc` safely run on a partially constructed object?
- Are all Lua-exposed pins validated, reserved in `activeGPIO`, and released on every close/failure path?
- Are flash and PSRAM reserved pins preserved?
- Are blocking calls wrapped by release/reacquire of `soDispMutex` with no Lua API calls in between?
- Are async callbacks anchored in a uservalue and weak reference when needed?
- Are ISR, lwIP, ESP-IDF event, and FreeRTOS callbacks bridged through `eventBrokerQueue` and `LThreadMgr`?
- Is async memory copied and freed in the correct context?
- Does network, SD-card, or persistent config code use `CfgESP32.c` helpers?
- If new files are added, is the build integration verified or explicitly called out as out of scope?

## Documentation Expectations

For each new or changed binding, document:

- Lua function or constructor signature.
- Config table fields and defaults.
- Return values, including `nil, err`.
- Target and Kconfig requirements.
- Owned native resources and close behavior.
- Pin ownership and conflicts.
- Blocking behavior and mutex release behavior.
- Callback context and event delivery path.
- Restart, network, SD-card, OTA, or persistent-config side effects.
