#pragma once

#if defined(_WIN32)
#define ORBEDEN_NATIVE_CALL __cdecl
#else
#define ORBEDEN_NATIVE_CALL
#endif
