# .NET Native Hosting Files

Source: local .NET SDK host packs installed under:

- `Microsoft.NETCore.App.Host.win-x64/10.0.9`
- `Microsoft.NETCore.App.Host.win-x86/10.0.9`

Copied files:

- `include/nethost.h`
- `include/hostfxr.h`
- `include/coreclr_delegates.h`
- `lib/win-x64/libnethost.lib`
- `lib/win-x64/nethost.lib`
- `lib/win-x86/libnethost.lib`
- `lib/win-x86/nethost.lib`

`EditorClrHost` links `nethost.lib` and uses `get_hostfxr_path` to find the installed `hostfxr` runtime when the Editor starts.
