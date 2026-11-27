/* memtool.c -- 进程内存读写工具：读、写、保持
 *
 * 这是整门课里第一个"能改"的程序。它做的事情，和任何调试器的内存编辑窗口
 * （x64dbg 的 Dump 面板、Cheat Engine 的手动改值）完全一样，只是没有界面：
 *
 *   1. 按进程名找到 target.exe 的 PID
 *   2. 打开进程，要"读"和"写"两组权限
 *   3. 定位目标字段的地址
 *   4. WriteProcessMemory 写入新值
 *   5. 如果目标每一帧都会重算这个字段，就按固定间隔重写一次（hold）
 *
 * 第 3 步没有写死 0x7050，用的是"特征搜索"（signature scan）：
 *   在整个模块映像里搜字符串 "bot-1\0"，找到 name 字段；
 *   name 在 +0x18，hp 在 +0x10，所以 hp 地址 = 命中地址 - 0x08。
 *   写死偏移在对方换个编译参数之后就废了，特征通常还在。
 *
 * 用法：
 *   memtool.exe read              只读，打印当前 hp
 *   memtool.exe write 9999        写一次，观察它被目标进程覆盖
 *   memtool.exe hold 9999 10      保持 10 秒，每 50ms 重写一次
 *
 * 编译：gcc -g -O0 -Wall -o memtool.exe memtool.c -lpsapi
 *
 * 输出一律 ASCII：cmd 按 GBK 解释 UTF-8 中文会乱码，这是本项目铁律。
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* 布局常量：与 labs/01-struct-layout/entity.h 一致。
 * 改结构体就要改这里 —— 两处必须同步，注释是承诺，offsetof 是事实。 */
#define NAME_OFF_FROM_ENTITY 0x18
#define HP_OFF_FROM_ENTITY   0x10
#define HP_FROM_NAME_DELTA   (HP_OFF_FROM_ENTITY - NAME_OFF_FROM_ENTITY)  /* = -0x08 */

static const char SIG[] = "bot-1";   /* 5 个字符 + 结尾 NUL = 6 字节 */

/* ---------- 1. 按 exe 名找 PID ---------- */
static DWORD find_pid(const char *exe)
{
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, exe) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

/* ---------- 2. 取主模块基址与映像大小 ---------- */
static int get_module(HANDLE h, unsigned char **base, size_t *size)
{
    HMODULE mod = NULL;
    DWORD need = 0;
    if (!EnumProcessModules(h, &mod, sizeof(mod), &need))
        return 0;

    MODULEINFO mi;
    if (!GetModuleInformation(h, mod, &mi, sizeof(mi)))
        return 0;

    *base = (unsigned char *)mi.lpBaseOfDll;
    *size = mi.SizeOfImage;
    return 1;
}

/* ---------- 3. 在本地缓冲区里找特征字节 ---------- */
static int find_pattern(const unsigned char *buf, size_t size,
                        const void *pat, size_t plen, size_t *hit)
{
    if (plen == 0 || plen > size)
        return 0;
    for (size_t i = 0; i + plen <= size; i++) {
        if (memcmp(buf + i, pat, plen) == 0) {
            *hit = i;
            return 1;
        }
    }
    return 0;
}

/* ---------- 4. 两个原语：读一个 float / 写一个 float ---------- */
static int read_float(HANDLE h, unsigned char *addr, float *out)
{
    SIZE_T got = 0;
    return ReadProcessMemory(h, addr, out, sizeof(*out), &got) && got == sizeof(*out);
}

static int write_float(HANDLE h, unsigned char *addr, float v)
{
    SIZE_T put = 0;
    return WriteProcessMemory(h, addr, &v, sizeof(v), &put) && put == sizeof(v);
}

int main(int argc, char **argv)
{
    const char *mode = (argc > 1) ? argv[1] : "read";
    float value  = (argc > 2) ? (float)atof(argv[2]) : 9999.0f;
    int  seconds = (argc > 3) ? atoi(argv[3]) : 10;
    if (seconds <= 0)
        seconds = 1;

    /* --- 找进程 --- */
    DWORD pid = find_pid("target.exe");
    if (!pid) {
        printf("[-] target.exe not found. start it first (run.bat does it for you).\n");
        return 1;
    }
    printf("[+] target.exe pid = %lu\n", (unsigned long)pid);

    /* --- 打开进程：读 + 写 + 操作（VM_OPERATION 是 WriteProcessMemory 的前置） --- */
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
                           PROCESS_VM_WRITE | PROCESS_VM_OPERATION,
                           FALSE, pid);
    if (!h) {
        printf("[-] OpenProcess failed, GetLastError = %lu\n", GetLastError());
        return 1;
    }

    unsigned char *base = NULL;
    size_t size = 0;
    if (!get_module(h, &base, &size)) {
        printf("[-] cannot get module info, GetLastError = %lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }
    printf("[+] module base %p  size 0x%zX\n", (void *)base, size);

    /* --- 把整个映像读进本地，准备扫描 --- */
    unsigned char *buf = (unsigned char *)malloc(size);
    if (!buf) {
        printf("[-] malloc failed\n");
        CloseHandle(h);
        return 1;
    }

    SIZE_T got = 0;
    if (!ReadProcessMemory(h, base, buf, size, &got) || got != size) {
        printf("[-] ReadProcessMemory failed, GetLastError = %lu (got %zu/%zu)\n",
               GetLastError(), (size_t)got, size);
        free(buf);
        CloseHandle(h);
        return 1;
    }

    /* --- 特征搜索 --- */
    size_t hit = 0;
    if (!find_pattern(buf, size, SIG, sizeof(SIG), &hit)) {
        printf("[-] signature \"%s\" not found. is this the right process?\n", SIG);
        free(buf);
        CloseHandle(h);
        return 1;
    }

    unsigned char *name_addr = base + hit;
    unsigned char *hp_addr   = base + hit + HP_FROM_NAME_DELTA;

    printf("[+] \"%s\" found at  base + 0x%zX\n", SIG, hit);
    printf("[+] name field     base + 0x%zX   (entity + 0x%X)\n", hit, NAME_OFF_FROM_ENTITY);
    printf("[+] hp   field     base + 0x%zX   (entity + 0x%X = name %+d)\n",
           hit + HP_FROM_NAME_DELTA, HP_OFF_FROM_ENTITY, HP_FROM_NAME_DELTA);
    (void)name_addr;

    /* --- 读当前值 --- */
    float before = 0.0f;
    if (!read_float(h, hp_addr, &before)) {
        printf("[-] read failed, GetLastError = %lu\n", GetLastError());
        free(buf);
        CloseHandle(h);
        return 1;
    }
    printf("[+] current hp = %g\n", before);
    free(buf);

    if (_stricmp(mode, "read") == 0) {
        CloseHandle(h);
        return 0;
    }

    if (_stricmp(mode, "write") == 0) {
        if (!write_float(h, hp_addr, value)) {
            printf("[-] write failed, GetLastError = %lu\n", GetLastError());
            CloseHandle(h);
            return 1;
        }
        float after = 0.0f;
        read_float(h, hp_addr, &after);
        printf("[+] wrote %g  ->  readback = %g\n", value, after);
        printf("[!] now watch the target window: the number keeps falling.\n");
        printf("    one write is not enough, because the target recomputes it every frame.\n");
        CloseHandle(h);
        return 0;
    }

    if (_stricmp(mode, "hold") == 0) {
        int ticks = seconds * 20;          /* 20 次/秒 */
        printf("[+] holding hp = %g for %d s (%d writes, every 50 ms)\n",
               value, seconds, ticks);
        for (int i = 0; i < ticks; i++) {
            write_float(h, hp_addr, value);
            if (i % 20 == 0) {
                float now = 0.0f;
                read_float(h, hp_addr, &now);
                printf("    t = %2d s   hp = %g\n", i / 20, now);
            }
            Sleep(50);
        }
        printf("[+] stop writing now.\n");
        Sleep(400);                        /* 让目标跑 4 帧 */
        float after = 0.0f;
        read_float(h, hp_addr, &after);
        printf("[+] hp 0.4 s later = %g   <- released, target recomputes again\n", after);
        CloseHandle(h);
        return 0;
    }

    printf("[-] unknown mode \"%s\". use: read | write <v> | hold <v> <sec>\n", mode);
    CloseHandle(h);
    return 1;
}
