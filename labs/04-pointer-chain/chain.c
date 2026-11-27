/* chain.c -- 自动挖出指针链，然后沿链读 / 写
 *
 * 三种用法：
 *   chain.exe find                    自动找链：从 "bot-1" 这个名字倒推回模块里的指针槽
 *   chain.exe read  <slot> [idx]      用已知的槽位偏移，沿链读第 idx 个实体的 hp
 *   chain.exe hold  <值> <秒> <slot> [idx]   沿链把 hp 保持住若干秒
 *
 * 链条（base = 模块基址）：
 *   [base + slot] --8字节--> World --[+0x00]--> units --[+idx*0x28 +0x10]--> hp
 *
 * step6.c 里那三次读取，就是这个程序的 read 分支。
 * find 分支额外的部分只有一件事：先找出 slot 这个偏移是多少。
 * 找法是：在目标进程的私有内存里搜 "bot-1"，拿到 units；
 *         再全内存搜"谁存着 units"，拿到 World；
 *         再在模块映像里搜"谁存着 World"，拿到 slot。
 *
 * 编译：gcc -fexec-charset=GBK -g -O0 -Wall -o chain.exe chain.c -lpsapi
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define STRIDE     0x28
#define OFF_HP     0x10
#define OFF_NAME   0x18
#define OFF_UNITS  0x00

/* ---------- 进程 / 模块 ---------- */
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

/* ---------- 读原语 ---------- */
static int read_u64(HANDLE h, const void *addr, uint64_t *out)
{
    SIZE_T got = 0;
    return ReadProcessMemory(h, addr, out, 8, &got) && got == 8;
}

static int read_f32(HANDLE h, const void *addr, float *out)
{
    SIZE_T got = 0;
    return ReadProcessMemory(h, addr, out, 4, &got) && got == 4;
}

static int write_f32(HANDLE h, void *addr, float v)
{
    SIZE_T put = 0;
    return WriteProcessMemory(h, addr, &v, 4, &put) && put == 4;
}

/* ---------- 私有内存快照（堆、栈） ---------- */
typedef struct {
    uint64_t addr;
    size_t   len;
    unsigned char *data;
} Blob;

typedef struct {
    Blob  *v;
    int    n, cap;
    size_t total;
} Dump;

static int dump_push(Dump *d, uint64_t addr, unsigned char *data, size_t len)
{
    if (d->n == d->cap) {
        int cap = d->cap ? d->cap * 2 : 16;
        Blob *nv = (Blob *)realloc(d->v, (size_t)cap * sizeof(Blob));
        if (!nv)
            return 0;
        d->v = nv;
        d->cap = cap;
    }
    d->v[d->n].addr = addr;
    d->v[d->n].len  = len;
    d->v[d->n].data = data;
    d->n++;
    d->total += len;
    return 1;
}

static void dump_free(Dump *d)
{
    for (int i = 0; i < d->n; i++)
        free(d->v[i].data);
    free(d->v);
    d->v = NULL;
    d->n = d->cap = 0;
}

static int dump_private(HANDLE h, Dump *d, size_t max_total)
{
    MEMORY_BASIC_INFORMATION mbi;
    unsigned char *p = NULL;

    while (VirtualQueryEx(h, p, &mbi, sizeof(mbi))) {
        uint64_t a = (uint64_t)(uintptr_t)mbi.BaseAddress;
        size_t   len = mbi.RegionSize;

        int ok = mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
                 !(mbi.Protect & PAGE_GUARD) && !(mbi.Protect & PAGE_NOACCESS) &&
                 (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                                 PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
        if (ok && len > 0 && d->total + len <= max_total) {
            unsigned char *buf = (unsigned char *)malloc(len);
            SIZE_T got = 0;
            if (buf && ReadProcessMemory(h, mbi.BaseAddress, buf, len, &got) && got == len) {
                if (!dump_push(d, a, buf, len))
                    free(buf);
            } else {
                free(buf);
            }
        }
        p = (unsigned char *)(uintptr_t)(a + len);
        if (len == 0 || p == NULL)
            break;
    }
    return d->n > 0;
}

static int dump_find_bytes(const Dump *d, const char *pat, size_t plen, uint64_t *hit)
{
    for (int i = 0; i < d->n; i++)
        for (size_t j = 0; j + plen <= d->v[i].len; j++)
            if (memcmp(d->v[i].data + j, pat, plen) == 0) {
                *hit = d->v[i].addr + j;
                return 1;
            }
    return 0;
}

static int dump_find_u64(const Dump *d, uint64_t want, uint64_t after, uint64_t *hit)
{
    for (int i = 0; i < d->n; i++)
        for (size_t j = 0; j + 8 <= d->v[i].len; j++) {
            uint64_t v;
            memcpy(&v, d->v[i].data + j, 8);
            if (v != want)
                continue;
            uint64_t a = d->v[i].addr + j;
            if (a <= after)
                continue;
            *hit = a;
            return 1;
        }
    return 0;
}

/* ---------- 沿链解析 ---------- */
static int resolve(HANDLE h, unsigned char *base, size_t slot_off, int idx,
                   unsigned char **out, uint64_t *world_out, uint64_t *units_out)
{
    uint64_t world = 0, units = 0;
    if (!read_u64(h, base + slot_off, &world) || world == 0)
        return 0;
    if (!read_u64(h, (const void *)(uintptr_t)world, &units) || units == 0)
        return 0;
    if (world_out) *world_out = world;
    if (units_out) *units_out = units;
    *out = (unsigned char *)(uintptr_t)(units + (size_t)idx * STRIDE + OFF_HP);
    return 1;
}

/* 连续 4 个名字都是 bot-N，才承认这是实体数组 */
static int verify_names(HANDLE h, uint64_t units)
{
    for (int i = 0; i < 4; i++) {
        char want[16], got[17] = {0};
        SIZE_T rg = 0;
        snprintf(want, sizeof(want), "bot-%d", i + 1);
        if (!ReadProcessMemory(h, (const void *)(uintptr_t)(units + (size_t)i * STRIDE + OFF_NAME),
                               got, 16, &rg))
            return 0;
        if (strcmp(want, got) != 0)
            return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *mode = (argc > 1) ? argv[1] : "read";

    DWORD pid = find_pid("target.exe");
    if (!pid) {
        printf("[-] 没找到 target.exe，先把靶子开起来（run.bat 会帮你开）。\n");
        return 1;
    }
    printf("[+] target.exe 的 pid = %lu\n", (unsigned long)pid);

    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
                           PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, pid);
    if (!h) {
        printf("[-] OpenProcess 失败，GetLastError = %lu\n", GetLastError());
        return 1;
    }

    unsigned char *base = NULL;
    size_t img_size = 0;
    if (!get_module(h, &base, &img_size)) {
        printf("[-] 拿不到模块信息，GetLastError = %lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }
    printf("[+] 模块基址 = %p，大小 0x%zX\n", (void *)base, img_size);

    /* ================= find ================= */
    if (_stricmp(mode, "find") == 0) {
        unsigned char *img = (unsigned char *)malloc(img_size);
        if (!img) {
            printf("[-] 内存不够\n");
            CloseHandle(h);
            return 1;
        }
        SIZE_T got = 0;
        if (!ReadProcessMemory(h, base, img, img_size, &got) || got != img_size) {
            printf("[-] 读模块失败，GetLastError = %lu\n", GetLastError());
            free(img);
            CloseHandle(h);
            return 1;
        }

        Dump d = {0};
        if (!dump_private(h, &d, 256u << 20)) {
            printf("[-] 读不到私有内存\n");
            free(img);
            CloseHandle(h);
            return 1;
        }
        printf("[+] 私有内存快照：%d 块，共 %zu 字节\n", d.n, d.total);

        /* 第 1 步：找实体数组 */
        uint64_t nm = 0;
        if (!dump_find_bytes(&d, "bot-1", 6, &nm)) {
            printf("[-] 没搜到 \"bot-1\"，靶子真的在跑吗？\n");
            dump_free(&d);
            free(img);
            CloseHandle(h);
            return 1;
        }
        uint64_t units = nm - OFF_NAME;
        if (!verify_names(h, units)) {
            printf("[-] 找到了 bot-1，但周围四个名字对不上，可能找错了。\n");
            dump_free(&d);
            free(img);
            CloseHandle(h);
            return 1;
        }
        printf("[+] 第 1 步：units（实体数组）= %p   （bot-1 在 units+0x%X）\n",
               (void *)(uintptr_t)units, OFF_NAME);

        /* 第 2 步：谁存着 units？那个格子在 World 里 */
        uint64_t after = 0, world = 0, slot_addr = 0;
        size_t slot = 0;
        int solved = 0;

        while (dump_find_u64(&d, units, after, &world)) {
            after = world;

            /* 第 3 步：谁存着 World？那个格子在模块映像里 */
            for (size_t i = 0; i + 8 <= img_size; i++) {
                uint64_t v;
                memcpy(&v, img + i, 8);
                if (v != world)
                    continue;

                unsigned char *hp = NULL;
                if (!resolve(h, base, i, 0, &hp, NULL, NULL))
                    continue;
                if (hp != (unsigned char *)(uintptr_t)(units + OFF_HP))
                    continue;

                slot = i;
                slot_addr = (uint64_t)(uintptr_t)(base + i);
                solved = 1;
                break;
            }
            if (solved)
                break;
        }

        dump_free(&d);
        free(img);

        if (!solved) {
            printf("[-] 链没走通。可能靶子刚重启过，重跑一次 find。\n");
            CloseHandle(h);
            return 1;
        }

        printf("[+] 第 2 步：World = %p   （它的 +0x%X 存着 units）\n",
               (void *)(uintptr_t)world, OFF_UNITS);
        printf("[+] 第 3 步：模块里的槽位 = base + 0x%zX   （地址 %p，存着 World）\n",
               slot, (void *)(uintptr_t)slot_addr);
        printf("\n[+] 整条链：[base+0x%zX] -> +0x%02X -> +0*0x%X + 0x%X\n",
               slot, OFF_UNITS, STRIDE, OFF_HP);

        float hp = 0.0f;
        unsigned char *hp_addr = NULL;
        if (resolve(h, base, slot, 0, &hp_addr, NULL, NULL) && read_f32(h, hp_addr, &hp))
            printf("[+] 验证：沿这条链读到 hp = %g\n", hp);
        printf("[+] 下一步：chain.exe read 0x%zX 0\n", slot);
        printf("SLOT=0x%zX\n", slot);   /* 纯 ASCII 一行，给 run.bat 用 findstr 抓 */

        CloseHandle(h);
        return 0;
    }

    /* ================= read / hold ================= */
    int is_read = (_stricmp(mode, "read") == 0);
    int is_hold = (_stricmp(mode, "hold") == 0);
    if (!is_read && !is_hold) {
        printf("[-] 不认识的模式 \"%s\"。用法：\n", mode);
        printf("    chain.exe find\n");
        printf("    chain.exe read <slot> [idx]\n");
        printf("    chain.exe hold <值> <秒> <slot> [idx]\n");
        CloseHandle(h);
        return 1;
    }

    float  value = 9999.0f;
    int    seconds = 5;
    size_t slot_off = 0;
    int    idx = 0;

    if (is_read) {
        slot_off = (argc > 2) ? (size_t)strtoull(argv[2], NULL, 0) : 0;
        idx      = (argc > 3) ? atoi(argv[3]) : 0;
    } else {
        value    = (argc > 2) ? (float)atof(argv[2]) : 9999.0f;
        seconds  = (argc > 3) ? atoi(argv[3]) : 5;
        slot_off = (argc > 4) ? (size_t)strtoull(argv[4], NULL, 0) : 0;
        idx      = (argc > 5) ? atoi(argv[5]) : 0;
    }
    if (seconds <= 0) seconds = 1;
    if (idx < 0 || idx > 3) idx = 0;
    if (slot_off == 0) {
        printf("[-] 缺 slot 偏移。先跑一次：chain.exe find\n");
        CloseHandle(h);
        return 1;
    }

    uint64_t world = 0, units = 0;
    unsigned char *hp_addr = NULL;
    if (!resolve(h, base, slot_off, idx, &hp_addr, &world, &units)) {
        printf("[-] 链断了：World 或 units 指针读不出来。靶子重启过吗？\n");
        CloseHandle(h);
        return 1;
    }

    printf("[1] [base+0x%zX] 的 8 字节 = %p   (World)\n",
           slot_off, (void *)(uintptr_t)world);
    printf("[2] [World+0x%02X] 的 8 字节 = %p   (units)\n",
           OFF_UNITS, (void *)(uintptr_t)units);
    printf("[3] units + %d*0x%X + 0x%X = %p   <- hp 的地址\n",
           idx, STRIDE, OFF_HP, (void *)hp_addr);

    float hp = 0.0f;
    read_f32(h, hp_addr, &hp);
    printf("[+] hp[%d] = %g\n", idx, hp);

    if (is_read) {
        CloseHandle(h);
        return 0;
    }

    int ticks = seconds * 20;
    printf("[+] 沿这条链把 hp[%d] 保持为 %g，持续 %d 秒\n", idx, value, seconds);
    for (int i = 0; i < ticks; i++) {
        write_f32(h, hp_addr, value);
        if (i % 20 == 0) {
            float now = 0.0f;
            read_f32(h, hp_addr, &now);
            printf("    t = %2d 秒   hp = %g\n", i / 20, now);
        }
        Sleep(50);
    }
    Sleep(400);
    float after = 0.0f;
    read_f32(h, hp_addr, &after);
    printf("[+] 松手 0.4 秒后 hp = %g   <- 目标进程又自己算回去了\n", after);

    CloseHandle(h);
    return 0;
}
