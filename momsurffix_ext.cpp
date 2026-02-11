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

// 关键：定义全局接口指针，供 SDK 自动生成的入口使用
SDKExtension *g_pExtensionIface = &g_MomSurfFixExt;

IEngineTrace *enginetrace = nullptr;
typedef void* (*CreateInterfaceFn)(const char *pName, int *pReturnCode);

// 前向声明回调
void OnEnableChanged(IConVar *var, const char *pOldValue, float flOldValue);

// ConVar 定义
ConVar g_cvEnable("momsurffix_enable", "1", 0, "Enable Surf Bug Fix", OnEnableChanged);
ConVar g_cvDebug("momsurffix_debug", "0", 0, "Print debug info");

// --- 参数化调整 ---
ConVar g_cvSensitivity("momsurffix_sensitivity", "0.97", 0, "Sensitivity threshold for speed loss detection (0.90 - 0.99)");
ConVar g_cvAntiStutterOffset("momsurffix_antistutter_offset", "0.01", 0, "Micro position adjustment to prevent stuttering (units)");

// 【新增】斜坡判定与落地判定参数
ConVar g_cvRampNormalZ("momsurffix_ramp_normalz", "0.7", 0, "Slope normal Z threshold. Surfaces steeper than this (lower Z) are treated as ramps/walls. (Default 0.7 ~= 45 deg)");
ConVar g_cvLandingSpeed("momsurffix_landing_speed", "100.0", 0, "Vertical speed threshold. Landings slower than this are treated as smooth surf exits (no view punch). (Default 100.0)");

// 偏移量定义
int g_off_Player = -1;
int g_off_MV = -1;
int g_off_VecVelocity = -1; 
int g_off_VecAbsOrigin = -1;
int g_off_GroundEntity = -1;
int g_off_VecMins = -1;
int g_off_VecMaxs = -1;

// 兼容性检查偏移
int g_off_WaterLevel = -1;
int g_off_MoveType = -1;

CSimpleDetour *g_pDetour = nullptr;

// ----------------------------------------------------------------------------
// 动态开关逻辑
// ----------------------------------------------------------------------------
void UpdateDetourState()
{
    if (!g_pDetour) return;

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

void TracePlayerBBox(const Vector &start, const Vector &end, void *pPlayer, IHandleEntity *pPlayerEntity, int collisionGroup, CGameTrace &pm)
{
    if (!enginetrace) return;
    
    Ray_t ray;
    Vector mins, maxs;
    
    if (g_off_VecMins != -1 && g_off_VecMaxs != -1)
    {
        mins = *(Vector *)((uintptr_t)pPlayer + g_off_VecMins);
        maxs = *(Vector *)((uintptr_t)pPlayer + g_off_VecMaxs);
    }
    else
    {
        mins = Vector(-16, -16, 0);
        maxs = Vector(16, 16, 72); 
    }

    ray.Init(start, end, mins, maxs);

    CTraceFilterSimple traceFilter(pPlayerEntity, collisionGroup);
    enginetrace->TraceRay(ray, MASK_PLAYERSOLID, &traceFilter, &pm);
}

// ----------------------------------------------------------------------------
// Detour Logic (全参数化完全体)
// ----------------------------------------------------------------------------
#ifndef THISCALL
    #define THISCALL
#endif
typedef int (THISCALL *TryPlayerMove_t)(void *, Vector *, CGameTrace *, float);

#define MOVETYPE_LADDER 9

int Detour_TryPlayerMove(void *pThis, Vector *pFirstDest, CGameTrace *pFirstTrace, float flTimeLeft)
{
    TryPlayerMove_t Original = (TryPlayerMove_t)g_pDetour->GetTrampoline();
    
    if (!Original || !g_cvEnable.GetBool()) 
    {
        return Original(pThis, pFirstDest, pFirstTrace, flTimeLeft);
    }

    void *pPlayer = *(void **)((uintptr_t)pThis + g_off_Player);
    CMoveData *mv = *(CMoveData **)((uintptr_t)pThis + g_off_MV);
    if (!pPlayer || !mv) return Original(pThis, pFirstDest, pFirstTrace, flTimeLeft);

    // ========================================================================
    // 兼容性保护 (梯子 & 水下)
    // ========================================================================
    if (g_off_MoveType != -1)
    {
        unsigned char moveTypeByte = *(unsigned char *)((uintptr_t)pPlayer + g_off_MoveType);
        if (moveTypeByte == MOVETYPE_LADDER) 
            return Original(pThis, pFirstDest, pFirstTrace, flTimeLeft);
    }

    if (g_off_WaterLevel != -1)
    {
        unsigned char waterLevel = *(unsigned char *)((uintptr_t)pPlayer + g_off_WaterLevel);
        if (waterLevel >= 2)
            return Original(pThis, pFirstDest, pFirstTrace, flTimeLeft);
    }

    Vector *pVel = (Vector *)((uintptr_t)mv + g_off_VecVelocity);
    Vector *pOrigin = (Vector *)((uintptr_t)mv + g_off_VecAbsOrigin);

    // 1. 记录原始状态
    Vector preVelocity = *pVel;
    Vector preOrigin = *pOrigin;
    float preSpeedSq = preVelocity.LengthSqr();
    
    unsigned long *pGroundEntity = (unsigned long *)((uintptr_t)pPlayer + g_off_GroundEntity);
    unsigned long hGroundEntityPre = *pGroundEntity;

    // 2. 运行原版引擎
    int result = Original(pThis, pFirstDest, pFirstTrace, flTimeLeft);

    // ========================================================================
    // 【智能落地优化】Smart Anti-Landing-Punch
    // ========================================================================
    unsigned long hGroundEntityPost = *pGroundEntity;

    if (hGroundEntityPre == 0xFFFFFFFF && hGroundEntityPost != 0xFFFFFFFF && preSpeedSq > 250.0f * 250.0f)
    {
        // 【参数化】使用 Cvar 控制落地判定速度 (默认 100.0)
        float landingSpeedThreshold = g_cvLandingSpeed.GetFloat();

        // 垂直速度小于阈值 -> 视为滑行切出 -> 消除震动
        // 垂直速度大于阈值 -> 视为跳跃落地 -> 保留震动
        if (std::abs(pVel->z) < landingSpeedThreshold)
        {
             *pGroundEntity = 0xFFFFFFFF;
        }
    }

    // ========================================================================
    // 撞坡修复逻辑
    // ========================================================================

    if (preSpeedSq < 250.0f * 250.0f) return result;

    float postSpeedSq = pVel->LengthSqr();
    
    // 【参数化】灵敏度 (默认 0.97)
    float sensitivity = g_cvSensitivity.GetFloat();
    if (postSpeedSq > preSpeedSq * sensitivity) return result;

    // 4. 执行修复检测
    IHandleEntity *pEntity = (IHandleEntity *)pPlayer;
    CGameTrace trace;
    
    Vector endPos = preOrigin + (preVelocity * flTimeLeft);
    TracePlayerBBox(preOrigin, endPos, pPlayer, pEntity, COLLISION_GROUP_PLAYER_MOVEMENT, trace);

    // 【参数化】斜坡判定阈值 (默认 0.7)
    float rampNormalZ = g_cvRampNormalZ.GetFloat();

    // 只针对陡峭斜坡触发修复 (z < rampNormalZ)
    if (trace.DidHit() && trace.plane.normal.z < rampNormalZ)
    {
        float backoff = DotProduct(preVelocity, trace.plane.normal);
        
        if (backoff < 0.0f)
        {
            Vector fixVel = preVelocity - (trace.plane.normal * backoff);

            // 【参数化】防抖偏移量 (默认 0.01)
            float antiStutterOffset = g_cvAntiStutterOffset.GetFloat();

            if (trace.plane.normal.z > 0.0f) 
            {
                 *pOrigin = trace.endpos + (trace.plane.normal * antiStutterOffset);
            }

            *pVel = fixVel;
            *pGroundEntity = 0xFFFFFFFF;
            
            if (g_cvDebug.GetBool())
                Msg("[MomSurfFix] FIXED! Speed: %.0f -> %.0f\n", sqrt(preSpeedSq), fixVel.Length());
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

    sm_sendprop_info_t info;
    if (gamehelpers->FindSendPropInfo("CBasePlayer", "m_hGroundEntity", &info))
    {
        g_off_GroundEntity = info.actual_offset;
    }
    else
    {
        if (!conf->GetOffset("CBasePlayer::m_hGroundEntity", &g_off_GroundEntity))
        {
             snprintf(error, maxlength, "Missing 'CBasePlayer::m_hGroundEntity'.");
             gameconfs->CloseGameConfigFile(conf);
             return false;
        }
    }

    // 碰撞箱偏移
    if (gamehelpers->FindSendPropInfo("CBasePlayer", "m_vecMins", &info))
        g_off_VecMins = info.actual_offset;
    if (gamehelpers->FindSendPropInfo("CBasePlayer", "m_vecMaxs", &info))
        g_off_VecMaxs = info.actual_offset;

    // 兼容性偏移
    if (gamehelpers->FindSendPropInfo("CBaseEntity", "m_nWaterLevel", &info))
        g_off_WaterLevel = info.actual_offset;
    if (gamehelpers->FindSendPropInfo("CBaseEntity", "m_MoveType", &info))
        g_off_MoveType = info.actual_offset;
    else if (gamehelpers->FindSendPropInfo("CBasePlayer", "m_MoveType", &info))
        g_off_MoveType = info.actual_offset;

    void *pTryPlayerMoveAddr = nullptr;
    if (!conf->GetMemSig("CGameMovement::TryPlayerMove", &pTryPlayerMoveAddr) || !pTryPlayerMoveAddr)
    {
        snprintf(error, maxlength, "Failed to find TryPlayerMove signature.");
        gameconfs->CloseGameConfigFile(conf);
        return false;
    }

    g_pDetour = new CSimpleDetour(pTryPlayerMoveAddr, (void *)Detour_TryPlayerMove);
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
