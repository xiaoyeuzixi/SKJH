#pragma once
/*
 * Mem.h — DMA 内存读写单例
 * 移植自 KAKA PUBG DMA 项目的 Memory 类
 *
 * 用法：
 *   mem.Init("UAGame.exe");
 *   auto base = mem.GetBase("UAGame.exe");
 *   auto h = mem.CreateScatter();
 *   mem.AddScatter(h, addr1, &buf1);
 *   mem.ExecuteScatter(h);
 *   mem.CloseScatter(h);
 */

#include <Windows.h>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include "vmmdll.h"
#include "leechcore.h"

// ======================== Memory 单例 ========================
class Mem {
public:
    VMM_HANDLE   hVMM = nullptr;
    DWORD        pid  = 0;
    DWORD64      base = 0;
    std::string  process;

private:
    mutable std::mutex moduleMutex;
    mutable std::unordered_map<std::string, DWORD64> moduleBases;

public:

    // ── 初始化 ──
    bool Init(const char* procName, DWORD waitTimeoutMs = INFINITE) {
        Close();
        process = procName;
        LPCSTR args[] = {"", "-device", "fpga://algo=0"};
        hVMM = VMMDLL_Initialize(3, args);
        if (!hVMM) return false;
        if (WaitProcess(procName, waitTimeoutMs)) return true;
        Close();
        return false;
    }

    bool WaitProcess(const char* name, DWORD waitTimeoutMs = INFINITE) {
        const auto started = std::chrono::steady_clock::now();
        while (true) {
            DWORD foundPid = 0;
            if (VMMDLL_PidGetFromName(hVMM, (LPSTR)name, &foundPid) && foundPid) {
                if (pid != foundPid) {
                    std::lock_guard<std::mutex> lock(moduleMutex);
                    moduleBases.clear();
                    pid = foundPid;
                    base = 0;
                }
                base = GetBase(name);
                if (base) return true;
            }
            if (waitTimeoutMs != INFINITE) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
                if (elapsed >= waitTimeoutMs) return false;
            }
            Sleep(250);
        }
    }

    void Close() {
        if (hVMM) { VMMDLL_Close(hVMM); hVMM = nullptr; }
        std::lock_guard<std::mutex> lock(moduleMutex);
        moduleBases.clear();
        pid = 0;
        base = 0;
    }
    ~Mem() { Close(); }

    // ── 基础读写 ──
    bool Read(DWORD64 addr, void* buf, DWORD size) const {
        if (!hVMM || !pid || !addr || !buf || !size) return false;
        DWORD bytesRead = 0;
        const BOOL ok = VMMDLL_MemReadEx(hVMM, pid, addr, (PBYTE)buf, size, &bytesRead,
            VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING_IO);
        return ok && bytesRead == size;
    }
    template<typename T> T Read(DWORD64 addr) const {
        T v{}; Read(addr, &v, sizeof(T)); return v;
    }
    bool Write(DWORD64 addr, void* buf, DWORD size) const {
        return VMMDLL_MemWrite(hVMM, pid, addr, (PBYTE)buf, size);
    }

    // ── 模块基址 ──
    DWORD64 GetBase(const char* mod) const {
        if (!hVMM || !pid || !mod || !mod[0]) return 0;
        std::lock_guard<std::mutex> lock(moduleMutex);
        const auto cached = moduleBases.find(mod);
        if (cached != moduleBases.end()) return cached->second;
        const DWORD64 result = VMMDLL_ProcessGetModuleBaseU(hVMM, pid, mod);
        if (result) moduleBases.emplace(mod, result);
        return result;
    }
    DWORD64 GetBaseSize(const char* mod) const {
        PVMMDLL_MAP_MODULEENTRY e = nullptr;
        if (VMMDLL_Map_GetModuleFromNameU(hVMM, pid, (LPSTR)mod, &e, 0)) {
            const DWORD64 size = e ? e->cbImageSize : 0;
            if (e) VMMDLL_MemFree(e);
            return size;
        }
        return 0;
    }

    static bool IsUserAddress(DWORD64 address) {
        return address >= 0x10000ull && address < 0x0000800000000000ull;
    }

    // ── Scatter 批量读 ──
    // NOCACHE: 绕过 VMM 缓存, 直接从 FPGA 读取 (实时数据保证)
    // NOPAGING_IO: 跳过分页内存 I/O (减少总线等待)
    VMMDLL_SCATTER_HANDLE CreateScatter() const {
        return VMMDLL_Scatter_Initialize(hVMM, pid,
            VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING_IO);
    }
    void AddScatter(VMMDLL_SCATTER_HANDLE h, DWORD64 addr, void* buf, DWORD size) {
        VMMDLL_Scatter_PrepareEx(h, addr, size, (PBYTE)buf, nullptr);
    }
    template<typename T>
    void AddScatter(VMMDLL_SCATTER_HANDLE h, DWORD64 addr, T* buf) {
        AddScatter(h, addr, buf, sizeof(T));
    }
    void ExecuteScatter(VMMDLL_SCATTER_HANDLE h) {
        VMMDLL_Scatter_ExecuteRead(h);
        VMMDLL_Scatter_Clear(h, pid, VMMDLL_FLAG_NOCACHE);
    }
    void CloseScatter(VMMDLL_SCATTER_HANDLE h) { VMMDLL_Scatter_CloseHandle(h); }

    // ── 缓存刷新 ──
    void RefreshTlb()   { VMMDLL_ConfigSet(hVMM, VMMDLL_OPT_REFRESH_FREQ_TLB_PARTIAL, 0); }
    void RefreshMem()   { VMMDLL_ConfigSet(hVMM, VMMDLL_OPT_REFRESH_FREQ_MEM_PARTIAL, 0); }
    void RefreshAll()   { VMMDLL_ConfigSet(hVMM, VMMDLL_OPT_REFRESH_ALL, 0); }
};

// ── 全局单例 ──
inline Mem mem;
