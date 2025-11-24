#pragma once
#include <3ds.h>
#include <3ds/services/gspgpu.h>

#ifndef GSPGPU_FramebufferFormat
# ifdef GSPGPU_FramebufferFormats
#  define GSPGPU_FramebufferFormat GSPGPU_FramebufferFormats
# endif
#endif

#ifndef NORETURN
#define NORETURN __attribute__((noreturn))
#endif

#include <CTRPluginFramework.hpp>
#include <CTRPluginFramework/Menu/PluginMenu.hpp>
