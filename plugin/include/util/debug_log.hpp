#pragma once
#include <CTRPluginFramework.hpp>
#include <cstdarg>
#include <cstring>

inline void Logf(const char* fmt, ...) {
    using namespace CTRPluginFramework;

    va_list va; 
    va_start(va, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    // Ensure directory exists (best-effort)
    Directory::Create("sdmc:/Fates3GX");

    File f;
    if (File::Open(f, "sdmc:/Fates3GX/fates_3gx.log", File::WRITE | File::CREATE) == 0) {
        f.Seek(0, File::END);
        f.Write(buf, strlen(buf));
        f.Write("\r\n", 2);
        f.Close();
    }
}



