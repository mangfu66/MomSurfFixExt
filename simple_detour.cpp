#include "simple_detour.h"
#include <stdio.h>

// 辅助函数：修改内存保护属性
static bool UnprotectMemory(void *addr, size_t len)
{
    long pagesize = sysconf(_SC_PAGESIZE);
    void *pageStart = (void *)((uintptr_t)addr & ~(pagesize - 1));
    return mprotect(pageStart, (uintptr_t)addr - (uintptr_t)pageStart + len, PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
}

CSimpleDetour::CSimpleDetour(void *pTarget, void *pHook)
    : m_pTarget(pTarget), m_pHook(pHook), m_pTrampoline(nullptr), m_bEnabled(false), m_bCreated(false)
{
}

CSimpleDetour::~CSimpleDetour()
{
    Disable();
    if (m_pTrampoline)
    {
        free(m_pTrampoline);
    }
}

bool CSimpleDetour::Enable()
{
    if (m_bEnabled) return true;
    if (!m_pTarget || !m_pHook) return false;

    // 1. 准备 Trampoline (原函数备份 + 跳回指令)
    if (!m_pTrampoline)
    {
        m_pTrampoline = malloc(20); // 足够存几条指令
        if (!m_pTrampoline) return false;
        
        // 允许 Trampoline 执行
        UnprotectMemory(m_pTrampoline, 20);
    }

    // 2. 解除目标地址保护
    if (!UnprotectMemory(m_pTarget, 5)) return false;

    // 3. 备份原指令 (假设前5字节是完整的指令，CSGO Linux下通常是 push ebp; mov ebp, esp... 安全)
    memcpy(m_OriginalBytes, m_pTarget, 5);

    // 4. 写入 Trampoline 内容
    // 复制原指令
    memcpy(m_pTrampoline, m_OriginalBytes, 5);
    // 写入跳回目标函数的 JMP
    uint8_t *pTrampolineJmp = (uint8_t *)m_pTrampoline + 5;
    *pTrampolineJmp = 0xE9; // JMP rel32
    uint32_t relAddrBack = (uintptr_t)m_pTarget + 5 - ((uintptr_t)pTrampolineJmp + 5);
    *(uint32_t *)(pTrampolineJmp + 1) = relAddrBack;

    // 5. 修改目标函数开头为跳转到 Hook 函数
    uint8_t *pTarget = (uint8_t *)m_pTarget;
    *pTarget = 0xE9; // JMP rel32
    uint32_t relAddrHook = (uintptr_t)m_pHook - ((uintptr_t)pTarget + 5);
    *(uint32_t *)(pTarget + 1) = relAddrHook;

    m_bEnabled = true;
    return true;
}

void CSimpleDetour::Disable()
{
    if (!m_bEnabled) return;

    if (UnprotectMemory(m_pTarget, 5))
    {
        memcpy(m_pTarget, m_OriginalBytes, 5);
    }
    m_bEnabled = false;
}
