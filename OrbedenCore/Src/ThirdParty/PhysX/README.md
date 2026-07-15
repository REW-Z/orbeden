# PhysX CPU-only package

This directory contains the official PhysX 5.9.0.6d94eeb9 static CPU SDK used by Orbeden.

Build invariants:

- `PX_GENERATE_STATIC_LIBRARIES=ON`
- `PX_GENERATE_GPU_PROJECTS=OFF`
- `PX_GENERATE_GPU_STATIC_LIBRARIES=OFF`
- no CUDA toolkit, NVIDIA driver API, NVTX, `PhysXGpu`, DLL, or shared-object dependency
- dynamic C/C++ runtime on Windows, matching the engine projects

The official public headers are preserved. Some GPU-only declarations remain for PhysX ABI/source compatibility, but `DISABLE_CUDA_PHYSX` and `PX_SUPPORT_GPU_PHYSX=0` make those facilities unavailable. Orbeden does not expose or call them.

Windows x64 libraries use the MSVC ABI and are shared by MSVC and clang-cl builds. Linux Clang and GCC packages are built natively under Ubuntu/WSL. FreeBSD and Nintendo Switch are unsupported until a verified upstream platform port and target SDK are supplied.

Rebuild with `Build/BuildPhysX.ps1` on Windows or `Build/BuildPhysXLinux.sh` under Ubuntu/WSL. See `Docs/BuildAndPackaging.md` for commands.
