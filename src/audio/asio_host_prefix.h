#pragma once

// The ASIO SDK host helpers are ANSI and use the COM `interface` keyword.
// MSVC CMake builds define UNICODE and WIN32_LEAN_AND_MEAN, which hides both.
// This prefix is force-included only for those SDK sources. It is not the SDK.

#ifdef UNICODE
#undef UNICODE
#endif
#ifdef _UNICODE
#undef _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#ifndef interface
#define interface struct
#endif
