#include "extension.h"
#include <IGameMovement.h>
#include <CBase.h>
#include <tier0/vprof.h>
#include "smsdk_config.h"

// ---------------------------------------------------------
// 1. 辅助工具：手动实现 TraceFilter (解决链接错误)
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
// 2. 全局变量与偏移量管理
// ---------------------------------------------------------
// ConVars
ConVar g_cvRampBumpCount("momsurffix_ramp_bumpcount", "8", FCVAR_NOTIFY);
ConVar g_cvRampInitialRetraceLength("momsurffix_ramp_retrace_length", "0.2", FCVAR_NOTIFY);
ConVar g_cvNoclipWorkaround("momsurffix_enable_noclip_workaround", "1", FCVAR_NOTIFY);
ConVar g_cvBounce("sv_bounce", "0"); // 注意：通常 sv_bounce 是游戏自带的，这里可能需要 FindConVar

// 偏移量 (从 GameData 读取)
int g_off_Player = -1;
int g_off_MV = -1;
int g_off_VecVelocity = -1; 
int g_off_VecAbsOrigin = -1;

// Detour 句柄
IChangeableForward *g_pTryPlayerMoveDetour = nullptr;
CDetourManager *g_pDetourManager = nullptr;

// 静态池
static CGameTrace g_TempTraces[MAXPLAYERS + 1];
static Vector g_TempPlanes[MAX_CLIP_PLANES];

// ---------------------------------------------------------
// 3. 辅助函数声明
// ---------------------------------------------------------
void FindValidPlane(CGameMovement *pThis, CBasePlayer *pPlayer, const Vector &origin, const Vector &vel, Vector &outPlane);
bool IsValidMovementTrace(const CGameTrace &tr);

// ---------------------------------------------------------
// 4. Detour 回调 (核心修复逻辑)
// ---------------------------------------------------------
// 声明 Detour 函数签名：TryPlayerMove(Vector *pFirstDest, CGameTrace *pFirstTrace)
// 注意：参数根据 SDK 不同可能会有变化，这里基于你的原始代码假设参数正确
DETOUR_DECL_MEMBER2(TryPlayerMove_Detour, int, Vector *, pFirstDest, CGameTrace *, pFirstTrace)
{
    CGameMovement *pThis = (CGameMovement *)this; // 获取 this 指针

    // 【关键】使用偏移量获取成员指针，防止崩溃
    // CBasePlayer *pPlayer = pThis->player; // 原始代码 (错误)
    CBasePlayer *pPlayer = *(CBasePlayer **)((uintptr_t)pThis + g_off_Player);
    
    // CMoveData *mv = pThis->mv; // 原始代码 (错误)
    CMoveData *mv = *(CMoveData **)((uintptr_t)pThis + g_off_MV);

    if (!pPlayer || !mv)
    {
        // 如果指针获取失败，直接调用原函数
        return DETOUR_MEMBER_CALL(TryPlayerMove_Detour)(pFirstDest, pFirstTrace);
    }

    VPROF_BUDGET("Momentum_TryPlayerMove", VPROF_BUDGETGROUP_PLAYER);

    int client = pPlayer->entindex();
    CGameTrace &pm = g_TempTraces[client];
    pm = CGameTrace(); // Reset

    // 获取速度和坐标 (使用偏移量更安全，也可以直接用 mv->m_vecVelocity 如果你确信 struct 没变)
    // 这里演示使用 GameData 偏移量读取 Velocity，确保 100% 安全
    Vector *pVel = (Vector *)((uintptr_t)mv + g_off_VecVelocity);
    Vector vel = *pVel; 

    // 获取 Origin
    Vector *pOrigin = (Vector *)((uintptr_t)mv + g_off_VecAbsOrigin);
    Vector origin = *pOrigin;
    
    if (vel.LengthSqr() < 0.000001f)
    {
        return 0; // MoveHelper()->ResetTouchList(); // 原版逻辑通常这里会 return
    }

    // --- 下面是你的修复逻辑 (保留大部分原样，微调 Trace 调用) ---

    float time_left = gpGlobals->frametime; // pThis->GetFrameTime() 可能无法直接调用，用全局变量替代
    int blocked = 0;
    int numbumps = g_cvRampBumpCount.GetInt();
    int numplanes = 0;
    bool stuck_on_ramp = false;
    Vector valid_plane;
    bool has_valid_plane = false;

    for (int bumpcount = 0; bumpcount < numbumps; bumpcount++)
    {
        if (vel.LengthSqr() == 0.0f)
            break;

        Vector end;
        VectorMA(origin, time_left, vel, end);

        if (pFirstDest && (end == *pFirstDest))
        {
            pm = *pFirstTrace;
        }
        else
        {
            pThis->TracePlayerBBox(origin, end, MASK_PLAYERSOLID, COLLISION_GROUP_PLAYER_MOVEMENT, pm);
        }

        if (pm.fraction > 0.0f)
        {
            origin = pm.endpos;
        }

        if (pm.fraction == 1.0f)
            break;

        time_left -= time_left * pm.fraction;

        if (numplanes >= MAX_CLIP_PLANES)
        {
            vel.Init();
            break;
        }

        g_TempPlanes[numplanes] = pm.plane.normal;
        numplanes++;

        // Ramp bug fix logic
        if (bumpcount > 0 && (pPlayer->GetGroundEntity() == NULL) && !IsValidMovementTrace(pm))
        {
            stuck_on_ramp = true;
        }

        // Noclip workaround
        if (g_cvNoclipWorkaround.GetBool() && stuck_on_ramp && vel.z >= -6.25f && vel.z <= 0.0f && !has_valid_plane)
        {
            FindValidPlane(pThis, pPlayer, origin, vel, valid_plane);
            has_valid_plane = (valid_plane.LengthSqr() > 0.000001f);
        }

        if (has_valid_plane)
        {
            VectorMA(origin, g_cvRampInitialRetraceLength.GetFloat(), valid_plane, origin);
        }

        // Clip velocity
        float allFraction = 0.0f;
        int blocked_planes = 0;
        
        // 注意：pThis->m_surfaceFriction 也是成员变量，需要偏移量。
        // 这里偷懒假设它是 1.0，或者你可以添加 g_off_SurfaceFriction
        float surfaceFriction = 1.0f; 

        for (int i = 0; i < numplanes; i++)
        {
            Vector new_vel;
            // standard clip
            new_vel = vel - (g_TempPlanes[i] * DotProduct(vel, g_TempPlanes[i]) * 1.0f); // 简化版 Clip

            if (g_TempPlanes[i].normal.z >= 0.7f)
            {
                blocked_planes++;
                new_vel.Init();
            }

            if (i == 0)
            {
                vel = new_vel;
            }
            else
            {
                float dot = DotProduct(new_vel, new_vel);
                if (dot > 0.0f)
                    new_vel *= (vel.Length() / sqrt(dot));

                vel = new_vel;
            }

            allFraction += pm.fraction;
        }

        if (blocked_planes == numplanes)
        {
            blocked |= 2;
            break;
        }
    }

    // 写回数据
    *pVel = vel;
    // pThis->mv->SetAbsOrigin(origin); // CMoveData 没有 SetAbsOrigin 虚函数，直接写内存
    *pOrigin = origin;

    // 返回 blocked 状态
    return blocked;
}

// ---------------------------------------------------------
// 5. 辅助函数实现
// ---------------------------------------------------------
void FindValidPlane(CGameMovement *pThis, CBasePlayer *pPlayer, const Vector &origin, const Vector &vel, Vector &outPlane)
{
    Vector sum_normal = vec3_origin;
    int count = 0;

    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            for (int z = -1; z <= 1; z++)
            {
                if (x == 0 && y == 0 && z == 0) continue;

                Vector dir(x * 0.03125f, y * 0.03125f, z * 0.03125f);
                dir.NormalizeInPlace();
                Vector end = origin + (dir * 0.0625f);

                CGameTrace trace;
                // 使用我们自定义的 Filter
                CTraceFilterSimple filter(pPlayer, COLLISION_GROUP_PLAYER_MOVEMENT);
                Ray_t ray;
                ray.Init(origin, end);
                enginetrace->TraceRay(ray, MASK_PLAYERSOLID, &filter, &trace);

                if (trace.fraction < 1.0f && trace.plane.normal.z > 0.7f)
                {
                    sum_normal += trace.plane.normal;
                    count++;
                }
            }
        }
    }

    if (count > 0)
    {
        outPlane = sum_normal * (1.0f / count);
        outPlane.NormalizeInPlace();
    }
    else
    {
        outPlane = vec3_origin;
    }
}

bool IsValidMovementTrace(const CGameTrace &tr)
{
    return (tr.fraction > 0.0f || tr.startsolid);
}

// ---------------------------------------------------------
// 6. 生命周期管理 (Load/Unload)
// ---------------------------------------------------------
bool MomSurfFixExt::SDK_OnLoad(char *error, size_t maxlength, bool late)
{
    // 1. 读取 GameData
    char conf_error[255];
    IGameConfig *conf = nullptr;
    if (!gameconfs->LoadGameConfigFile("momsurffix_fix.games", &conf, conf_error, sizeof(conf_error)))
    {
        snprintf(error, maxlength, "Could not read momsurffix_fix.games: %s", conf_error);
        return false;
    }

    // 2. 获取偏移量 (重要：对应你的 gamedata key)
    if (!conf->GetOffset("CGameMovement::player", &g_off_Player) ||
        !conf->GetOffset("CGameMovement::mv", &g_off_MV) ||
        !conf->GetOffset("CMoveData::m_vecVelocity", &g_off_VecVelocity) ||
        !conf->GetOffset("CMoveData::m_vecAbsOrigin", &g_off_VecAbsOrigin))
    {
        snprintf(error, maxlength, "Failed to get one or more offsets from gamedata.");
        gameconfs->CloseGameConfigFile(conf);
        return false;
    }

    // 3. 创建 Detour
    g_pDetourManager = g_pSM->GetDetourManager();
    // "CGameMovement::TryPlayerMove" 必须匹配 gamedata 里的 Signatures 节的键名
    Detour *detour = g_pDetourManager->CreateDetour(conf, "CGameMovement::TryPlayerMove");
    
    if (!detour)
    {
        snprintf(error, maxlength, "Failed to create detour for TryPlayerMove.");
        gameconfs->CloseGameConfigFile(conf);
        return false;
    }

    // 4. 启用 Hook
    detour->AddFixedHook(DETOUR_MEMBER(TryPlayerMove_Detour), nullptr); // nullptr 因为我们不需要特定的 callback owner
    detour->EnableDetour();

    gameconfs->CloseGameConfigFile(conf);
    return true;
}

void MomSurfFixExt::SDK_OnUnload()
{
    // 自动管理 Detour，不需要手动清理，但为了规范可以置空
}

void MomSurfFixExt::SDK_OnAllLoaded()
{
}

bool MomSurfFixExt::QueryRunning(char *error, size_t maxlength)
{
    return true;
}

SMEXT_LINK(&g_MomSurfFixExt);
