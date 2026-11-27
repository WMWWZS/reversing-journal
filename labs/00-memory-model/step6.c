/* step6.c -- 指针链，缩到最小：三行读取，走完两跳
 *
 * 这个程序只做一件事：对着 lab04 的 target.exe，沿指针链走到 hp。
 *
 *   第 1 次读：[base + 0x7030]        8 字节  -> World 的地址
 *   第 2 次读：[World + 0x00]         8 字节  -> units（实体数组）的地址
 *   第 3 次读：[units + 0*0x28 + 0x10] 4 字节  -> hp 的值
 *
 * 就是这三行。多级指针没有第四种操作。
 * chain.c 那 500 行里，一半是在"自动找 0x7030 这个偏移"，一半是在报错和验证。
 *
 * 编译：gcc -fexec-charset=GBK -g -O0 -Wall -o step6.exe step6.c -lpsapi
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define SLOT_OFF   0x7030   /* lab04 target.exe 里 g_world 这个指针槽的偏移 */
#define OFF_UNITS  0x00     /* World 里 units 指针的偏移 */
#define OFF_HP     0x10     /* Entity 里 hp 的偏移 */
#define STRIDE     0x28     /* sizeof(Entity) */

static int read_u64(HANDLE h, uint64_t addr, uint64_t *out)
{
    SIZE_T got = 0;
    return ReadProcessMemory(h, (const void *)(uintptr_t)addr, out, 8, &got) && got == 8;
}

static int read_f32(HANDLE h, uint64_t addr, float *out)
{
    SIZE_T got = 0;
    return ReadProcessMemory(h, (const void *)(uintptr_t)addr, out, 4, &got) && got == 4;
}

static int get_base(HANDLE h, uint64_t *base)
{
    HMODULE mod = NULL;
    DWORD need = 0;
    if (!EnumProcessModules(h, &mod, sizeof(mod), &need))
        return 0;
    MODULEINFO mi;
    if (!GetModuleInformation(h, mod, &mi, sizeof(mi)))
        return 0;
    *base = (uint64_t)(uintptr_t)mi.lpBaseOfDll;
    return 1;
}

/* 试着在这个进程里走一遍链；能走到 "bot-1" 才算找对进程 */
static int walk(HANDLE h, uint64_t base, uint64_t *world_out, uint64_t *units_out,
                uint64_t *hp_addr_out, int *idx_out)
{
    uint64_t world = 0, units = 0;
    char name[17] = {0};
    SIZE_T got = 0;

    if (!read_u64(h, base + SLOT_OFF, &world) || world == 0)
        return 0;
    if (!read_u64(h, world + OFF_UNITS, &units) || units == 0)
        return 0;
    if (!ReadProcessMemory(h, (const void *)(uintptr_t)(units + 0x18), name, 16, &got))
        return 0;
    if (strcmp(name, "bot-1") != 0)
        return 0;

    *world_out = world;
    *units_out = units;
    *hp_addr_out = units + OFF_HP;
    *idx_out = 0;
    return 1;
}

int main(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        printf("[-] 拿不到进程列表\n");
        return 1;
    }

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);

    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, "target.exe") != 0)
                continue;

            HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                                   pe.th32ProcessID);
            if (!h)
                continue;

            uint64_t base = 0;
            if (!get_base(h, &base)) {
                CloseHandle(h);
                continue;
            }

            uint64_t world = 0, units = 0, hp_addr = 0;
            int idx = 0;
            if (!walk(h, base, &world, &units, &hp_addr, &idx)) {
                CloseHandle(h);      /* 不是 lab04 那个靶子，换下一个 */
                continue;
            }

            float hp = 0.0f;
            read_f32(h, hp_addr, &hp);

            printf("[+] 找到靶子: pid = %lu\n", (unsigned long)pe.th32ProcessID);
            printf("[+] 模块基址 = 0x%llX\n\n", (unsigned long long)base);

            printf("第 1 次读：[base + 0x%X] 的 8 个字节 = 0x%llX   <- World 的地址\n",
                   SLOT_OFF, (unsigned long long)world);
            printf("第 2 次读：[World + 0x%X] 的 8 个字节 = 0x%llX   <- units 的地址\n",
                   OFF_UNITS, (unsigned long long)units);
            printf("第 3 次读：[units + %d*0x%X + 0x%X] 的 4 个字节 = %g   <- hp\n\n",
                   idx, STRIDE, OFF_HP, hp);

            printf("三次读取，两跳指针，一次加偏移。这就是指针链的全部。\n");
            printf("每一跳的动作：读 8 个字节，把它当成地址，再加一个偏移。\n");

            CloseHandle(h);
            CloseHandle(snap);
            return 0;
        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    printf("[-] 没找到能走通指针链的 target.exe\n");
    printf("    先运行 ..\\04-pointer-chain\\run.bat 把靶子开起来，再跑本程序。\n");
    return 1;
}
