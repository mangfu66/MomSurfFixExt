#include <cstdarg>
#include <cstdio>
#include "extension.h"

#if !defined(DLLEXPORT)
  #if defined __WIN32__ || defined _WIN32
    #define DLLEXPORT __declspec(dllexport)
  #else
    #define DLLEXPORT __attribute__((visibility("default")))
  #endif
#endif

extern "C" DLLEXPORT void Warning(const char *pMsg, ...)
{
    va_list ap;
    va_start(ap, pMsg);
    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), pMsg, ap);
    va_end(ap);

    if (smutils)
        smutils->LogError(&g_MomSurfFixExt, "[MomSurfFix] %s", buffer);
    else
        printf("[MomSurfFix] %s", buffer);
}

extern "C" DLLEXPORT void Msg(const char *pMsg, ...)
{
    va_list ap;
    va_start(ap, pMsg);
    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), pMsg, ap);
    va_end(ap);

    if (smutils)
        smutils->LogMessage(&g_MomSurfFixExt, "[MomSurfFix] %s", buffer);
    else
        printf("[MomSurfFix] %s", buffer);
}
