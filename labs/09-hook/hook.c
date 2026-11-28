/* hook.c -- 改代码：把"减血"这条指令干掉
 *
 * 五步，一步都不能少：
 *   1. VirtualProtectEx  把代码页临时改成可写
 *   2. WriteProcessMemory 写补丁字节（这里是 3 个 0x90 = NOP）
 *   3. FlushInstructionCache 告诉 CPU 代码变了
 *   4. VirtualProtectEx  把页权限还原
 *   5. 留一份原字节，能一键恢复
 *
 * 怎么找到那条指令：不写死地址，扫特征码 F3 0F 5C C1（subss xmm0,xmm1），
 * 然后**用行为验证**——打完补丁看 hp 是否停止下降；没停就还原、换下一个候选。
 *
 * 编译：gcc -fexec-charset=GBK -g -O0 -Wall -o hook.exe hook.c -lpsapi
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define SLOT_OFF  0x7030     /* g_world 在模块里的偏移 */
#define OFF_UNITS 0x00
#define OFF_HP    0x10
#define PATCH_LEN 4

static const unsigned char SIG[4] = { 0xF3, 0x0F, 0x5C, 0xC1 };   /* subss xmm0,xmm1 = 4 字节 */

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
            if (_stricmp(pe.szExeFile, exe) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static int get_module(HANDLE h, unsigned char **base, size_t *size)
{
    HMODULE mod = NULL; DWORD need = 0;
    if (!EnumProcessModules(h, &mod, sizeof(mod), &need)) return 0;
    MODULEINFO mi;
    if (!GetModuleInformation(h, mod, &mi, sizeof(mi))) return 0;
    *base = (unsigned char *)mi.lpBaseOfDll;
    *size = mi.SizeOfImage;
    return 1;
}

static int read_u64(HANDLE h, uint64_t a, uint64_t *v)
{
    SIZE_T got = 0;
    return ReadProcessMemory(h, (const void *)(uintptr_t)a, v, 8, &got) && got == 8;
}

static int read_f32(HANDLE h, uint64_t a, float *v)
{
    SIZE_T got = 0;
    return ReadProcessMemory(h, (const void *)(uintptr_t)a, v, 4, &got) && got == 4;
}

/* 走链：g_world -> units -> hp */
static int resolve_hp(HANDLE h, unsigned char *base, uint64_t *hp_addr)
{
    uint64_t world = 0, units = 0;
    if (!read_u64(h, (uint64_t)(uintptr_t)(base + SLOT_OFF), &world) || !world) return 0;
    if (!read_u64(h, world + OFF_UNITS, &units) || !units) return 0;
    *hp_addr = units + OFF_HP;
    return 1;
}

/* 打补丁：五步。失败返回 0 */
static int patch_bytes(HANDLE h, unsigned char *addr, const unsigned char *bytes, size_t len)
{
    DWORD old_prot = 0;
    if (!VirtualProtectEx(h, addr, len, PAGE_EXECUTE_READWRITE, &old_prot)) {
        printf("    [-] VirtualProtectEx 失败，GetLastError = %lu\n", GetLastError());
        return 0;
    }
    SIZE_T put = 0;
    if (!WriteProcessMemory(h, addr, bytes, len, &put) || put != len) {
        printf("    [-] WriteProcessMemory 失败，GetLastError = %lu\n", GetLastError());
        VirtualProtectEx(h, addr, len, old_prot, &old_prot);
        return 0;
    }
    FlushInstructionCache(h, addr, len);
    VirtualProtectEx(h, addr, len, old_prot, &old_prot);
    return 1;
}

/* 观察 hp 是否停止下降：读两次，间隔 400ms */
static int hp_frozen(HANDLE h, uint64_t hp_addr)
{
    float a = 0, b = 0;
    read_f32(h, hp_addr, &a);
    Sleep(400);
    read_f32(h, hp_addr, &b);
    printf("    观察：%.2f -> %.2f  %s\n", a, b, (a == b) ? "停住了" : "还在变");
    return (a == b);
}

int main(void)
{
    DWORD pid = find_pid("target.exe");
    if (!pid) {
        printf("[-] 没找到 target.exe，先把靶子开起来。\n");
        return 1;
    }
    printf("[+] target.exe 的 pid = %lu\n", (unsigned long)pid);

    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
                           PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, pid);
    if (!h) {
        printf("[-] OpenProcess 失败，GetLastError = %lu（注入/改代码需要 VM_OPERATION）\n",
               GetLastError());
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

    uint64_t hp_addr = 0;
    if (!resolve_hp(h, base, &hp_addr)) {
        printf("[-] 链断了：base+0x%X 读不出 World/units。靶子版本对不上？\n", SLOT_OFF);
        CloseHandle(h);
        return 1;
    }
    float hp0 = 0;
    read_f32(h, hp_addr, &hp0);
    printf("[+] hp 地址 = %p，当前 hp = %g\n", (void *)(uintptr_t)hp_addr, hp0);

    /* ---- 在模块映像里扫特征码 ---- */
    unsigned char *img = (unsigned char *)malloc(size);
    if (!img) { CloseHandle(h); return 1; }
    SIZE_T got = 0;
    if (!ReadProcessMemory(h, base, img, size, &got) || got != size) {
        printf("[-] 读模块失败\n");
        free(img); CloseHandle(h); return 1;
    }

    int hits = 0, patched = 0;
    size_t hit_off = 0;
    unsigned char original[PATCH_LEN];

    for (size_t i = 0; i + PATCH_LEN <= size; i++) {
        if (memcmp(img + i, SIG, PATCH_LEN) != 0)
            continue;
        hits++;
        unsigned char *insn = base + i;

        printf("\n[*] 候选 #%d：base + 0x%zX 处的字节 = %02X %02X %02X %02X %02X %02X %02X %02X\n",
               hits, i, img[i], img[i + 1], img[i + 2], img[i + 3],
               img[i + 4], img[i + 5], img[i + 6], img[i + 7]);

        memcpy(original, img + i, PATCH_LEN);
        unsigned char nops[PATCH_LEN] = { 0x90, 0x90, 0x90, 0x90 };

        if (!patch_bytes(h, insn, nops, PATCH_LEN))
            continue;

        /* 回读：确认真的写进去了（写内存必须回读验证） */
        unsigned char check[PATCH_LEN] = {0};
        SIZE_T rd = 0;
        ReadProcessMemory(h, insn, check, PATCH_LEN, &rd);
        printf("    写入后回读 = %02X %02X %02X %02X %s\n",
               check[0], check[1], check[2], check[3],
               (check[0] == 0x90 && check[1] == 0x90 && check[2] == 0x90 && check[3] == 0x90) ? "(已生效)" : "(没写进去!)");

        /* 用行为验证：hp 停了才算补对 */
        if (hp_frozen(h, hp_addr)) {
            patched = 1;
            hit_off = i;
            printf("    [+] hp 停止下降 —— 就是这条\n");
            break;
        }

        printf("    [-] hp 还在掉，这不是减血那条，还原换下一个\n");
        patch_bytes(h, insn, original, PATCH_LEN);
    }

    free(img);

    if (!patched) {
        printf("\n[-] %d 个候选都不是。可能编译器换了写法（比如 addss 减负数）。\n", hits);
        CloseHandle(h);
        return 1;
    }

    /* ---- 成功之后：连续观察 ---- */
    printf("\n=== 补丁生效后（每 300ms 读一次，共 6 次）===\n");
    for (int i = 0; i < 6; i++) {
        float v = 0;
        read_f32(h, hp_addr, &v);
        printf("    hp = %7.1f\n", v);
        Sleep(300);
    }

    /* ---- 还原，证明真的能一键恢复 ---- */
    printf("\n=== 还原原字节之后 ===\n");
    unsigned char *insn = base + hit_off;
    if (!patch_bytes(h, insn, original, PATCH_LEN)) {
        printf("[-] 还原失败！原字节：%02X %02X %02X %02X\n",
               original[0], original[1], original[2], original[3]);
        CloseHandle(h);
        return 1;
    }
    for (int i = 0; i < 6; i++) {
        float v = 0;
        read_f32(h, hp_addr, &v);
        printf("    hp = %7.1f\n", v);
        Sleep(300);
    }

    printf("\n[+] 补丁地址 = base + 0x%zX，原字节 = %02X %02X %02X %02X（已写回）\n",
           hit_off, original[0], original[1], original[2], original[3]);
    printf("[+] 五步都走过：VirtualProtectEx -> WriteProcessMemory -> FlushInstructionCache -> 还原权限 -> 可恢复\n");

    CloseHandle(h);
    return 0;
}
