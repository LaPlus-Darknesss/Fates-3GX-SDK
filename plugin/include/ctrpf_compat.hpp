#pragma once
// Always pull libctru first
#include <3ds.h>
#include <3ds/services/gspgpu.h>
#include <3ds/gpu/gx.h>

#ifndef GSPGPU_FramebufferFormat
# ifdef GSPGPU_FramebufferFormats
#  define GSPGPU_FramebufferFormat GSPGPU_FramebufferFormats
# endif
#endif

#ifndef NORETURN
#define NORETURN __attribute__((noreturn))
#endif

#include "pf.hpp"
