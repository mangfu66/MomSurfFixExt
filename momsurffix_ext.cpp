// ---------------------------------------------------------
// 【第一区】暴力兼容补丁 (必须在文件最最最前面)
// ---------------------------------------------------------
// 这里的顺序至关重要：必须先定义宏，再引入任何 SDK 头文件
#include <cstdlib> // 为了 aligned_alloc

// 强制拦截 Windows 内存函数
#undef _aligned_malloc
#undef _aligned_free
// 注意：Linux 的 aligned_alloc 参数顺序是 (alignment, size)
#define _aligned_malloc(size, align) aligned_alloc(align, size)
#define _aligned_free free
#define MemAlloc_AllocAlignedFileLine(size, align, file, line) aligned_alloc(align, size)

// ---------------------------------------------------------
// 【第二区】SDK 与 SourceMod
// ---------------------------------------------------------
// 在宏定义之后才引入 extension.h，确保 SDK 内部使用的是我们要的宏
#include "extension.h"

// 引入内存分配器接口
#include <tier0/memalloc.h>
extern IMemAlloc *g_pMemAlloc;

// 引入 Trace 接口
#include <engine/IEngineTrace.h>
// 定义全局指针
IEngineTrace *enginetrace = nullptr;

// 引入 IHandleEntity
#include <ihandleentity.h>

// 引入空间划分 (ITraceFilter 定义)
#include <ispatialpartition.h>

// 引入 GameMovement 接口
#include <igamemovement.h>

#include <tier0/vprof.h>
#include "smsdk_config.h"
#include "simple_detour.h"

// ---------------------------------------------------------
// 常量与前置声明
// ---------------------------------------------------------
#ifndef MAXPLAYERS
#define MAXPLAYERS 65
#endif
#ifndef MAX_CLIP_PLANES
#define MAX_CLIP_PLANES 5
#endif

// 修复 enum 前置声明报错
enum PLAYER_ANIM { 
    PLAYER_IDLE, PLAYER_WALK, PLAYER_JUMP, PLAYER_SUPERJUMP, PLAYER_DIE, PLAYER_ATTACK1 
};

// 前置声明
class CBasePlayer;
class CBaseEntity;
class CGameMovement; 

// ---------------------------------------------------------
// 辅助类
// ---------------------------------------------------------
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

// ---------------------------------------------------------
// 全局变量
// ---------------------------------------------------------
ConVar g_cvRampBumpCount("momsurffix_ramp_bumpcount", "8", FCVAR_NOTIFY);
ConVar g_cvRampInitialRetraceLength("momsurffix_ramp_retrace_length", "0.2", FCVAR_NOTIFY);
ConVar g_cvNoclipWorkaround("momsurffix_enable_noclip_workaround", "1", FCVAR_NOTIFY);
ConVar g_cvBounce("sv_bounce", "0");

int g_off_Player = -1;
int g_off_MV = -1;
int g_off_VecVelocity = -1; 
int g_off_VecAbsOrigin = -1;
int g_off_GroundEntity = -1;

CSimpleDetour *g_pDetour = nullptr;

static CGameTrace g_TempTraces[MAXPLAYERS + 1];
static Vector g_TempPlanes[MAX_CLIP_PLANES];

// ---------------------------------------------------------
// 辅助函数声明
// ---------------------------------------------------------
void FindValidPlane(CGameMovement *pThis, CBasePlayer *pPlayer, const Vector &origin, const Vector &vel, Vector &outPlane);
bool IsValidMovementTrace(const CGameTrace &tr);

// 手动实现 TracePlayerBBox，因为我们无法链接到 CGameMovement 的内部实现
void Manual_TracePlayerBBox(IGameMovement *pGM, const Vector &start, const Vector &end, unsigned int fMask, int collisionGroup, CGameTrace &pm)
{
    if (!enginetrace) return;

    // 假设未蹲下，获取默认 Hull。如果需要精确，需要读取 m_bDucked
    // 这里简化处理，直接调用接口获取
    Vector mins = pGM->GetPlayerMins(false); 
    Vector maxs = pGM->GetPlayerMaxs(false);

    Ray_t ray;
    ray.Init(start, end, mins, maxs);

    // 获取玩家实体
    CBasePlayer *player = pGM->GetMovingPlayer();
    
    CTraceFilterSimple traceFilter((IHandleEntity *)player, collisionGroup);
    enginetrace->TraceRay(ray, fMask, &traceFilter, &pm);
}

// ---------------------------------------------------------
// Detour 回调
// ---------------------------------------------------------
typedef int (*TryPlayerMove_t)(CGameMovement *, Vector *, CGameTrace *, float);

int Detour_TryPlayerMove(CGameMovement *pThis, Vector *pFirstDest, CGameTrace *pFirstTrace, float flTimeLeft)
{
    // 获取成员
    void *pPlayer = *(void **)((uintptr_t)pThis + g_off_Player);
    CMoveData *mv = *(CMoveData **)((uintptr_t)pThis + g_off_MV);

    TryPlayerMove_t Original = (TryPlayerMove_t)g_pDetour->GetTrampoline();

    if (!pPlayer || !mv || !Original)
    {
        return 0;
    }

    VPROF_BUDGET("Momentum_TryPlayerMove", VPROF_BUDGETGROUP_PLAYER);

    // 获取 client 索引
    int client = ((IHandleEntity *)pPlayer)->GetRefEHandle().GetEntryIndex();
    
    CGameTrace &pm = g_TempTraces[client];
    pm = CGameTrace(); 

    Vector *pVel = (Vector *)((uintptr_t)mv + g_off_VecVelocity);
    Vector vel = *pVel; 

    Vector *pOrigin = (Vector *)((uintptr_t)mv + g_off_VecAbsOrigin);
    Vector origin = *pOrigin;
    
    if (vel.LengthSqr() < 0.000001f)
    {
        return Original(pThis, pFirstDest, pFirstTrace, flTimeLeft); 
    }

    float time_left = flTimeLeft;
    int blocked = 0;
    int numbumps = g_cvRampBumpCount.GetInt();
    int numplanes = 0;
    bool stuck_on_ramp = false;
    Vector valid_plane;
    bool has_valid_plane = false;

    // 转换为接口指针以便调用虚函数
    IGameMovement *pGM = (IGameMovement *)pThis;

    for (int bumpcount = 0; bumpcount < numbumps; bumpcount++)
    {
        if (vel.LengthSqr() == 0.0f) break;

        Vector end;
        VectorMA(origin, time_left, vel, end);

        if (pFirstDest && (end == *pFirstDest))
        {
            pm = *pFirstTrace;
        }
        else
        {
            // 使用我们手动实现的 Trace，替代 pThis->TracePlayerBBox
            Manual_TracePlayerBBox(pGM, origin, end, MASK_PLAYERSOLID, COLLISION_GROUP_PLAYER_MOVEMENT, pm);
        }

        if (pm.fraction > 0.0f)
        {
            origin = pm.endpos;
        }

        if (pm.fraction == 1.0f) break;

        time_left -= time_left * pm.fraction;

        if (numplanes >= MAX_CLIP_PLANES)
        {
            vel.Init();
            break;
        }

        g_TempPlanes[numplanes] = pm.plane.normal;
        numplanes++;

        // 地面检测
        unsigned long hGroundEntity = *(unsigned long *)((uintptr_t)pPlayer + g_off_GroundEntity);
        bool bIsAirborne = (hGroundEntity == 0xFFFFFFFF); 

        if (bumpcount > 0 && bIsAirborne && !IsValidMovementTrace(pm))
        {
            stuck_on_ramp = true;
        }

        if (g_cvNoclipWorkaround.GetBool() && stuck_on_ramp && vel.z >= -6.25f && vel.z <= 0.0f && !has_valid_plane)
        {
            FindValidPlane(pThis, (CBasePlayer*)pPlayer, origin, vel, valid_plane);
            has_valid_plane = (valid_plane.LengthSqr() > 0.000001f);
        }

        if (has_valid_plane)
        {
            VectorMA(origin, g_cvRampInitialRetraceLength.GetFloat(), valid_plane, origin);
        }

        float allFraction = 0.0f;
        int blocked_planes = 0;
        
        for (int i = 0; i < numplanes; i++)
        {
            Vector new_vel;
            // Vector 没有 normal 成员，直接访问 z
            if (g_TempPlanes[i].z >= 0.7f)
            {
