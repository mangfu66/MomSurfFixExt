// ============================================================================
// 【0】SourceMod 扩展核心
// ============================================================================
#include "extension.h" 

// ============================================================================
// 【1】标准库
// ============================================================================
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <dlfcn.h> 
#include <algorithm>
#include <cmath>

// ============================================================================
// 【2】基础 SDK 头文件
// ============================================================================
#include <tier0/platform.h>
#include <tier0/memalloc.h>
#include <tier1/convar.h>
#include <gametrace.h>
#include <soundflags.h>
#include <ihandleentity.h> 
#include <interfaces/interfaces.h> 

// ============================================================================
// 【3】SDK 兼容垫片
// ============================================================================
class CBasePlayer;
class CBaseEntity;

enum PLAYER_ANIM 
{ 
    PLAYER_IDLE = 0, PLAYER_WALK, PLAYER_JUMP, PLAYER_SUPERJUMP, PLAYER_DIE, PLAYER_ATTACK1
};

// ============================================================================
// 【4】依赖上述类型的 SDK 头文件
// ============================================================================
#include <engine/IEngineTrace.h>
#include <ispatialpartition.h> 
#include <igamemovement.h> 
#include <tier0/vprof.h>
#include "simple_detour.h"

// ============================================================================
// 全局变量
// ============================================================================
#ifndef MAXPLAYERS
#define MAXPLAYERS 65
#endif

MomSurfFixExt g_MomSurfFixExt;
SDKExtension *g_pExtensionIface = &g_MomSurfFixExt;

IEngineTrace *enginetrace = nullptr;
typedef void* (*CreateInterfaceFn)(const char *pName, int *pReturnCode);

// 前向声明回调
void OnEnableChanged(IConVar *var, const char *pOldValue, float flOldValue);

// ConVar 定义
ConVar g_cvEnable("momsurffix_enable", "1", 0, "Enable Surf Bug Fix", OnEnableChanged);
ConVar g_cvDebug("momsurffix_debug", "0", 0, "Print debug info");

int g_off_Player = -1;
int g_off_MV = -1;
int g_off_VecVelocity = -1; 
int g_off_VecAbsOrigin = -1;
int g_off_GroundEntity = -1;

CSimpleDetour *g_pDetour = nullptr;

// ----------------------------------------------------------------------------
// 动态开关逻辑
// ----------------------------------------------------------------------------
void UpdateDetourState()
{
    if (!g_pDetour) return;

    // 打印状态变化，让你确认命令是否生效
    if (g_cvEnable.GetBool())
    {
        g_pDetour->Enable();
        Msg("[MomSurfFix] Status: ENABLED (Hook Active)\n");
    }
    else
    {
        g_pDetour->Disable();
        Msg("[MomSurfFix] Status: DISABLED (Vanilla Physics)\n");
    }
}

void OnEnableChanged(IConVar *var, const char *pOldValue, float flOldValue)
{
    UpdateDetourState();
}

// ----------------------------------------------------------------------------
// 辅助类与函数
// ----------------------------------------------------------------------------
class CTraceFilterSimple : public ITraceFilter
{
public:
    CTraceFilterSimple(const IHandleEntity *passentity, int collisionGroup)
        : m_pPassEnt(passentity), m_collisionGroup(collisionGroup) {}

    virtual bool ShouldHitEntity(IHandleEntity *pHandleEntity, int contentsMask)
    {
        return pHandleEntity != m_pPassEnt;
    }

    virtual TraceType_t GetTraceType() const
    {
        return TRACE_EVERYTHING;
    }

private:
    const IHandleEntity *m_pPassEnt;
    int m_collisionGroup;
};

void TracePlayerBBox(const Vector &start, const Vector &end, IHandleEntity *pPlayerEntity, int collisionGroup, CGameTrace &pm)
{
    if (!enginetrace) return;
    
    Ray_t ray;
    Vector mins(-16, -16, 0);
    Vector maxs(16, 16, 72); 
    ray.Init(start, end, mins, maxs);

    CTraceFilterSimple traceFilter(pPlayerEntity, collisionGroup);
    enginetrace->TraceRay(ray, MASK_PLAYERSOLID, &traceFilter, &pm);
}

// ----------------------------------------------------------------------------
// Detour Logic (更灵敏的反应式修复)
// ----------------------------------------------------------------------------
#ifndef THISCALL
    #define THISCALL
#endif
typedef int (THISCALL *TryPlayerMove_t)(void *, Vector *, CGameTrace *, float);

int Detour_TryPlayerMove(void *pThis, Vector *pFirstDest, CGameTrace *pFirstTrace, float flTimeLeft)
{
    TryPlayerMove_t Original = (TryPlayerMove_t)g_pDetour->GetTrampoline();
    
    // 【双重保险】如果开关关闭，强制执行原版逻辑，不运行任何额外代码
    if (!Original || !g_cvEnable.GetBool()) 
    {
        return Original(pThis, pFirstDest, pFirstTrace, flTimeLeft);
    }

    void *pPlayer = *(void **)((uintptr_t)pThis + g_off_Player);
    CMoveData *mv = *(CMoveData **)((uintptr_t)pThis + g_off_MV);
    if (!pPlayer || !mv) return Original(pThis, pFirstDest, pFirstTrace, flTimeLeft);

    Vector *pVel = (Vector *)((uintptr_t)mv + g_off_VecVelocity);
    Vector *pOrigin = (Vector *)((uintptr_t)mv + g_off_VecAbsOrigin);

    // 1. 记录原始状态
    Vector preVelocity = *pVel;
    Vector preOrigin = *pOrigin;
    float preSpeedSq = preVelocity.LengthSqr();

    // 2. 运行原版引擎
    int result = Original(pThis, pFirstDest, pFirstTrace, flTimeLeft);

    // 3. 检查是否需要修复
    
    // 必须是滑翔状态 (速度 > 250)
    if (preSpeedSq < 250.0f * 250.0f) return result;

    // 必须在空中
    unsigned long hGroundEntity = *(unsigned long *)((uintptr_t)pPlayer + g_off_GroundEntity);
    if (hGroundEntity != 0xFFFFFFFF) return result;

    // 【调整阈值】只要速度损失超过 10% 就尝试修复 (原版是 30%)
    // 这会让插件更灵敏，更容易触发修复，不再感觉是“空壳”
    float postSpeedSq = pVel->LengthSqr();
    if (postSpeedSq > preSpeedSq * 0.9f) return result;

    // 4. 执行修复
    IHandleEntity *pEntity = (IHandleEntity *)pPlayer;
    CGameTrace trace;
    
    // 探测前方
    Vector endPos = preOrigin + (preVelocity * flTimeLeft);
    TracePlayerBBox(preOrigin, endPos, pEntity, COLLISION_GROUP_PLAYER_MOVEMENT, trace);

    if (trace.DidHit() && trace.plane.normal.z < 0.7f)
    {
        float backoff = DotProduct(preVelocity, trace.plane.normal);
        if (backoff < 0.0f)
        {
            Vector fixVel = preVelocity - (trace.plane.normal * backoff);

            // 安全限速 (800) 防止飞天
            if (fixVel.z > 800.0f) fixVel.z = 800.0f;

            // 应用修复
            *pVel = fixVel;
            
            // 推离墙体
            if (trace.plane.normal.z > 0.0f) 
                 *pOrigin = trace.endpos + (trace.plane.normal * 0.1f);

            // 调试信息：如果开启 debug，你会看到这条消息，证明插件活在
            if (g_cvDebug.GetBool())
                Msg("[MomSurfFix] Fixed Ramp Bug! Speed kept: %.0f -> %.0f\n", sqrt(preSpeedSq), fixVel.Length());
        }
    }

    return result;
}

// ----------------------------------------------------------------------------
// SDK 生命周期
// ----------------------------------------------------------------------------
bool MomSurfFixExt::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
    char conf_error[255];
    IGameConfig *conf = nullptr;
    if (!gameconfs->LoadGameConfigFile("momsurffix_fix.games", &conf, conf_error, sizeof(conf_error)))
    {
        snprintf(error, maxlength, "Could not read momsurffix_fix.games: %s", conf_error);
        return false;
    }

    if (!conf->GetOffset("CGameMovement::player", &g_off_Player) ||
        !conf->GetOffset("CGameMovement::mv", &g_off_MV) ||
        !conf->GetOffset("CMoveData::m_vecVelocity", &g_off_VecVelocity) ||
        !conf->GetOffset("CMoveData::m_vecAbsOrigin", &g_off_VecAbsOrigin))
    {
        snprintf(error, maxlength, "Failed to get core offsets.");
        gameconfs->CloseGameConfigFile(conf);
        return false;
    }

    if (!conf->GetOffset("CBasePlayer::m_hGroundEntity", &g_off_GroundEntity))
    {
         snprintf(error, maxlength, "Missing 'CBasePlayer::m_hGroundEntity'.");
         gameconfs->CloseGameConfigFile(conf);
         return false;
    }

    void *pTryPlayerMoveAddr = nullptr;
    if (!conf->GetMemSig("CGameMovement::TryPlayerMove", &pTryPlayerMoveAddr) || !pTryPlayerMoveAddr)
    {
        snprintf(error, maxlength, "Failed to find TryPlayerMove signature.");
        gameconfs->CloseGameConfigFile(conf);
        return false;
    }

    // 初始化 Detour 对象
    g_pDetour = new CSimpleDetour(pTryPlayerMoveAddr, (void *)Detour_TryPlayerMove);
    
    // 立即根据 ConVar 初始值设置状态
    UpdateDetourState();

    void *pCreateInterface = nullptr;
    if (conf->GetMemSig("CreateInterface", &pCreateInterface) && pCreateInterface)
    {
        CreateInterfaceFn factory = (CreateInterfaceFn)pCreateInterface;
        enginetrace = (IEngineTrace *)factory(INTERFACEVERSION_ENGINETRACE_SERVER, nullptr);
    }

    if (!enginetrace)
    {
        snprintf(error, maxlength, "Could not find interface: %s", INTERFACEVERSION_ENGINETRACE_SERVER);
        gameconfs->CloseGameConfigFile(conf);
        return false;
    }

    // 手动注册 ConVar
    void *hVStdLib = dlopen("libvstdlib_srv.so", RTLD_NOW | RTLD_NOLOAD);
    if (!hVStdLib) hVStdLib = dlopen("libvstdlib.so", RTLD_NOW | RTLD_NOLOAD);
    
    ICvar *pCvar = nullptr;
    if (hVStdLib) {
        CreateInterfaceFn factory = (CreateInterfaceFn)dlsym(hVStdLib, "CreateInterface");
        if (factory) {
            pCvar = (ICvar *)factory(CVAR_INTERFACE_VERSION, nullptr);
        }
        dlclose(hVStdLib);
    }
    
    if (!pCvar && pCreateInterface) {
         CreateInterfaceFn factory = (CreateInterfaceFn)pCreateInterface;
         pCvar = (ICvar *)factory(CVAR_INTERFACE_VERSION, nullptr);
    }

    if (pCvar) {
        g_pCVar = pCvar;       
        ConVar_Register(0);    
    }

    gameconfs->CloseGameConfigFile(conf);
    return true;
}

void MomSurfFixExt::SDK_OnUnload()
{
    if (g_pDetour)
    {
        g_pDetour->Disable();
        delete g_pDetour;
        g_pDetour = nullptr;
    }
}

void MomSurfFixExt::SDK_OnAllLoaded()
{
}

bool MomSurfFixExt::QueryRunning(char *error, size_t maxlength)
{
    return true;
}
