# PalworldSrvConnect

A client-side UE4SS C++ mod that adds DNS SRV record support to Palworld's direct-connect field.

[Steam Community](https://steamcommunity.com/sharedfiles/filedetails/?id=3772333758)

## Behavior

- Normal IP endpoints such as `1.1.1.1:8211` are left unchanged.
- A domain without a port, such as `play.example.com`, triggers a lookup for:

  ```text
  _palworld._udp.play.example.com
  ```

- SRV records are selected by RFC 2782 priority and weight rules.
- The selected target is resolved to IPv4 when possible, then a temporary `IP:PORT` value is passed to Palworld.
- The visible and saved address is restored to the original domain after the connection attempt.
- An explicit endpoint such as `play.example.com:8211` bypasses SRV and preserves Palworld's original address and port behavior.
- If no SRV record exists or the query fails, the original input is preserved. Palworld may then reject a portless address using its normal validation.
- A portless full SRV owner such as `_custom._udp.example.com` is queried as written.
- Resolution results and errors are written to the UE4SS console and log.

This is a client-side mod. It does not need to be installed on the dedicated server.

## DNS Configuration

Server example:

```dns
_palworld._udp.play.example.com. 60 IN SRV 0 100 8211 play.example.com.
play.example.com.              60 IN A   1.1.1.1
```

The SRV values are `priority weight port target`. Records at the lowest priority are selected according to their weights.

If Cloudflare manages the target hostname, set its A record to **DNS only**.

## Requirements

- UE4SS installation

The prebuilt mod is intended for:

```text
UE4SS v3.0.1 Beta Git SHA c838a8ac
Game__Shipping__Win64 (MSVC)
```

UE4SS C++ mods require ABI compatibility. A DLL built against another UE4SS revision may fail to load with Windows error `0x7f`. Rebuild this mod against the exact UE4SS revision used by the game installation when necessary.

## Installation

1. Install the compatible UE4SS build for Palworld.
2. Copy the complete `PalworldSrvConnect` directory into the active UE4SS `Mods` directory shown in the UE4SS log.
3. Verify this layout:

   ```text
   PalworldSrvConnect/
   |-- enabled.txt
   `-- dlls/
       `-- main.dll
   ```

Common UE4SS Mods locations include:

```text
<Palworld>\Mods\NativeMods\UE4SS\Mods\
<Palworld>\Pal\Binaries\Win64\ue4ss\Mods\
```

Start Palworld, open the multiplayer direct-connect screen, and enter a domain without a port.

## Building

Build requirements:

- Windows 10 or Windows 11
- Visual Studio or Visual Studio Build Tools with **Desktop development with C++**
- A current CMake release
- Git for Windows

The setup script downloads the UE4SS source revision pinned to the tested runtime. The build script uses `vswhere.exe` and CMake to select the newest installed Visual Studio generator supported by the local CMake version.

Run `build.bat`, or run the following command in PowerShell:

```powershell
./build.ps1
```

To select a specific generator and toolset:

```powershell
./build.ps1 -Generator "Visual Studio 18 2026" -Toolset v145
```

Use `cmake --help` to see the exact generator names supported by the installed CMake version.

If a UE4SS dependency does not yet compile with v145 and Visual Studio 2022 is also installed, explicitly fall back to v143:

```powershell
./build.ps1 -Generator "Visual Studio 17 2022" -Toolset v143
```

The packaged mod is written to:

```text
dist/PalworldSrvConnect/
|-- enabled.txt
`-- dlls/
    `-- main.dll
```

## Core Tests

Endpoint parsing and SRV record selection can be tested without Windows:

```bash
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

## Known Limitations

- Windows Palworld clients only.
- The first connection to a domain performs a system DNS query. Later queries use the DNS TTL cache.

## Implementation Notes

The mod hooks the current Palworld direct-connect UI before its built-in format validation, then keeps lower-level input and connection hooks as fallbacks. It changes only address parameters and saved display text; Palworld's authentication, password, session, and network connection logic remains intact.

Relevant Palworld functions include:

- `UWBP_JoinGame_C::OnClicked_JoinByIPButton(FString Address)`
- `UPalUIJoinGameBase::ConnectServerByAddress(FString Address, int32 Port)`
