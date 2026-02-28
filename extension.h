#ifndef _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_
#define _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_

#include "smsdk_ext.h"

class MomSurfFixExt2 : public SDKExtension
{
public:
    virtual bool SDK_OnLoad(char *error, size_t maxlength, bool late);
    virtual void SDK_OnUnload();
    virtual void SDK_OnAllLoaded();
    virtual bool QueryRunning(char *error, size_t maxlength);
    virtual void LevelInit(char const *pMapName);
};

extern MomSurfFixExt2 g_MomSurfFixExt2;

#endif
