#include "simple_detour.h"
#include <stdio.h>

// Helper: change memory protection
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

    // 1. Prepare trampoline (backup original bytes + jump back)
    if (!m_pTrampoline)
    {
        m_pTrampoline = malloc(20);
        if (!m_pTrampoline) return false;

        // Allow trampoline execution
        UnprotectMemory(m_pTrampoline, 20);
    }

    // 2. Unprotect target address
    if (!UnprotectMemory(m_pTarget, 5)) return false;

    // 3. Backup original bytes (assume first 5 bytes are complete instructions)
    memcpy(m_OriginalBytes, m_pTarget, 5);

    // 4. Write trampoline content
    // Copy original bytes
    memcpy(m_pTrampoline, m_OriginalBytes, 5);
    // Write JMP back to target
    uint8_t *pTrampolineJmp = (uint8_t *)m_pTrampoline + 5;
    *pTrampolineJmp = 0xE9; // JMP rel32
    uint32_t relAddrBack = (uintptr_t)m_pTarget + 5 - ((uintptr_t)pTrampolineJmp + 5);
    *(uint32_t *)(pTrampolineJmp + 1) = relAddrBack;

    // 5. Patch target function to jump to hook
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
