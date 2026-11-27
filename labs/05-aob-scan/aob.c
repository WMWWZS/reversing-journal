/* aob.c -- 特征扫描（AOB）：不靠偏移，靠一段不会变的机器码
 *
 * 背景：lab04 里 g_world 的槽位在 base+0x7030。新版游戏加了几个全局变量，
 * 槽位往后挪了，写死的 0x7030 直接读到垃圾 —— 这就是"游戏更新，外挂失效"。
 *
 * 解决办法不是重新找偏移（下次更新还得再找一遍），而是找一条**访问这个槽位的指令**：
 *
 *   mov rax, [rip + disp32]     机器码 = 48 8B 05 ?? ?? ?? ??
 *                                真正地址 = 指令地址 + 7 + disp32
 *
 * 这条指令的形状（48 8B 05 + 相对偏移）在更新后通常不变，变的只是 disp 那几个字节。
 * 所以我们扫描 "48 8B 05"，把每个命中的 disp 解出来，算出目标地址，
 * 再用"沿链能不能走到 bot-1"来验证哪一个命中是真的。
 *
 * 编译：gcc -fexec-charset=GBK -g -O0 -Wall -o aob.exe aob.c -lpsapi
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define OFF_UNITS 0x00
#define OFF_NAME  0x18

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

static int read_u64(HANDLE h, uint64_t addr, uint64_t *out)
{
    SIZE_T got = 0;
    return ReadProcessMemory(h, (const void *)(uintptr_t)addr, out, 8, &got) && got == 8;
}

static int read_name(HANDLE h, uint64_t addr, char *buf, size_t n)
{
    SIZE_T got = 0;
    memset(buf, 0, n);
    return ReadProcessMemory(h, (const void *)(uintptr_t)addr, buf, n - 1, &got);
}

/* 从槽位地址出发，沿链验证：槽 -> World -> units -> units[0].name == bot-1 */
static int chain_ok(HANDLE h, uint64_t slot_addr, uint64_t *world_out)
{
    uint64_t world = 0, units = 0;
    char name[17];

    if (!read_u64(h, slot_addr, &world) || world == 0)
        return 0;
    if (!read_u64(h, world + OFF_UNITS, &units) || units == 0)
        return 0;
    if (!read_name(h, units + OFF_NAME, name, sizeof(name)))
        return 0;
    if (strcmp(name, "bot-1") != 0)
        return 0;

    if (world_out)
        *world_out = world;
    return 1;
}

int main(void)
{
    DWORD pid = find_pid("target.exe");
    if (!pid) {
        printf("[-] 没找到 target.exe，先把靶子开起来。\n");
        return 1;
    }
    printf("[+] target.exe 的 pid = %lu\n", (unsigned long)pid);

    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) {
        printf("[-] OpenProcess 失败，GetLastError = %lu\n", GetLastError());
        return 1;
    }

    unsigned char *base = NULL;
    size_t size = 0;
    if (!get_module(h, &base, &size)) {
        printf("[-] 拿不到模块信息\n");
        CloseHandle(h);
        return 1;
    }
    printf("[+] 模块基址 = %p，大小 0x%zX\n", (void *)base, size);

    unsigned char *img = (unsigned char *)malloc(size);
    if (!img) {
        CloseHandle(h);
        return 1;
    }
    SIZE_T got = 0;
    if (!ReadProcessMemory(h, base, img, size, &got) || got != size) {
        printf("[-] 读模块失败，GetLastError = %lu\n", GetLastError());
        free(img);
        CloseHandle(h);
        return 1;
    }

    /* ---------- 第 1 步：先证明旧偏移已经废了 ---------- */
    uint64_t old_val = 0;
    printf("\n=== 第 1 步：旧配置还有效吗 ===\n");
    if (read_u64(h, (uint64_t)(uintptr_t)(base + 0x7030), &old_val)) {
        printf("  base + 0x7030 的 8 个字节 = 0x%llX\n", (unsigned long long)old_val);
        if (chain_ok(h, (uint64_t)(uintptr_t)(base + 0x7030), NULL))
            printf("  沿它还能走通 -> 旧偏移居然还有效（本轮靶子没变）\n");
        else
            printf("  沿它走不通 -> 旧偏移已失效，正是这一节要解决的问题\n");
    }

    /* ---------- 第 2 步：扫描访问该槽位的指令 ---------- */
    printf("\n=== 第 2 步：扫描 48 8B 05 ?? ?? ?? ??（mov rax,[rip+disp32]）===\n");

    int hits = 0, good = 0;
    for (size_t i = 0; i + 7 <= size; i++) {
        if (img[i] != 0x48 || img[i + 1] != 0x8B || img[i + 2] != 0x05)
            continue;

        int32_t disp;
        memcpy(&disp, img + i + 3, 4);

        uint64_t insn_addr = (uint64_t)(uintptr_t)(base + i);
        uint64_t target = insn_addr + 7 + (int64_t)disp;

        /* 只关心落在模块内的目标（全局变量都在模块里） */
        if (target < (uint64_t)(uintptr_t)base ||
            target >= (uint64_t)(uintptr_t)(base + size))
            continue;

        hits++;
        uint64_t world = 0;
        int ok = chain_ok(h, target, &world);
        if (ok)
            good++;

        if (hits <= 12) {
            printf("  命中 #%-2d 指令 @ base+0x%05zX : %02X %02X %02X %02X %02X %02X %02X\n",
                   hits, i, img[i], img[i + 1], img[i + 2], img[i + 3],
                   img[i + 4], img[i + 5], img[i + 6]);
            printf("          算出目标 = 0x%llX  (模块内偏移 0x%zX)  %s\n",
                   (unsigned long long)target, (size_t)(target - (uint64_t)(uintptr_t)base),
                   ok ? "<== 链走通，这就是 g_world" : "");
        }

        if (ok) {
            printf("\n[+] 找到了：\n");
            printf("    指令地址   = base + 0x%zX\n", i);
            printf("    指令字节   = %02X %02X %02X %02X %02X %02X %02X\n",
                   img[i], img[i + 1], img[i + 2], img[i + 3],
                   img[i + 4], img[i + 5], img[i + 6]);
            printf("    相对偏移   = %d (0x%X)\n", disp, (uint32_t)disp);
            printf("    槽位地址   = 指令 + 7 + 相对偏移 = 0x%llX\n", (unsigned long long)target);
            printf("    槽位偏移   = base + 0x%zX   <- 这是新的静态偏移\n",
                   (size_t)(target - (uint64_t)(uintptr_t)base));
            printf("    沿链读 World = 0x%llX\n", (unsigned long long)world);
            break;   /* 教学版：找到第一个验证通过的就停 */
        }
    }

    printf("\n共扫到 %d 条候选指令，其中 %d 条能走通链。\n", hits, good);
    if (!good) {
        printf("[-] 没有候选通过验证。可能这个函数被内联了，或者靶子不对。\n");
        free(img);
        CloseHandle(h);
        return 1;
    }

    printf("\n结论：偏移会变，但\"访问这个全局变量\"的指令形状（48 8B 05）不变。\n");
    printf("      扫描它、解出 disp、再加 7，就能重新算出槽位地址。\n");

    free(img);
    CloseHandle(h);
    return 0;
}
