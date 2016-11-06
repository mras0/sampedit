#include <win32/base.h>
#include <stdio.h>

void fatal_error(const wchar_t* api, unsigned error /*= GetLastError()*/)
{
    wchar_t* msg;
    if (FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&msg),
        0,
        nullptr)) {
        wprintf(L"%s failed: %d (0x%X). %s\n", api, error, error, msg);
    } else {
        wprintf(L"%s failed: %d (0x%X)\n", api, error, error);
    }
    exit(error);
}