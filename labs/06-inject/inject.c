/* inject.c -- 把一小段机器码塞进目标进程，让它自己执行
 *
 * 为什么需要这个：外面写内存（lab03）只能改一次，目标下一帧就盖回去了。
 * 要让"改"持续生效，最直接的办法是让**目标自己**每 50ms 写一次。
 *
 * 五步，一步都不能少：
 *   1. 找到 hp 的地址（用 lab04/05 学的链：槽位 -> World -> units -> hp）
 *   2. OpenProcess，这次还要 PROCESS_CREATE_THREAD
 *   3. VirtualAllocEx 在目标里申请一块可执行内存
 *   4. WriteProcessMemory 把机器码写进去
 *   5. CreateRemoteThread 让目标开一个线程跑它
 *
 * 机器码（44 字节）：
 *   mov  rax, <hp 的地址>
 *   mov  rdx, <Sleep 的地址>
 * loop:
 *   mov  dword [rax], <9999.0f>
 *   mov  ecx, 50
 *   sub  rsp, 0x28          ; Windows x64 调用约定：32 字节影子空间 + 对齐
 *   call rdx                ; Sleep(50)
 *   add  rsp, 0x28
 *   jmp  loop
 *
 * 编译：gcc -fexec-charset=GBK -g -O0 -Wall -o inject.exe inject.c -lpsapi
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define SLOT_OFF  0x7050
#define OFF_UNITS 0x00
#define OFF_HP    0x10
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

static int get_base(HANDLE h, unsigned char **base, size_t *size)
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

static int read_f32(HANDLE h, uint64_t addr, float *out)
{
    SIZE_T got = 0;
    return ReadProcessMemory(h, (const void *)(uintptr_t)addr, out, 4, &got) && got == 4;
}

int main(void)
{
    DWORD pid = find_pid("target.exe");
    if (!pid) {
        printf("[-] 没找到 target.exe，先把靶子开起来。\n");
        return 1;
    }
    printf("[+] target.exe 的 pid = %lu\n", (unsigned long)pid);

    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE |
                           PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD, FALSE, pid);
    if (!h) {
        printf("[-] OpenProcess 失败，GetLastError = %lu\n", GetLastError());
        printf("    （注入必须多要一个 PROCESS_CREATE_THREAD）\n");
        return 1;
    }

    unsigned char *base = NULL;
    size_t size = 0;
    if (!get_base(h, &base, &size)) {
        printf("[-] 拿不到模块信息\n");
        CloseHandle(h);
        return 1;
    }

    /* ---------- 1. 走链拿 hp 地址 ---------- */
    uint64_t world = 0, units = 0;
    if (!read_u64(h, (uint64_t)(uintptr_t)(base + SLOT_OFF), &world) || world == 0 ||
        !read_u64(h, world + OFF_UNITS, &units) || units == 0) {
        printf("[-] 链断了：槽位 base+0x%X 读不出 World。靶子是不是换了版本？\n", SLOT_OFF);
        CloseHandle(h);
        return 1;
    }

    char name[17] = {0};
    SIZE_T got = 0;
    ReadProcessMemory(h, (const void *)(uintptr_t)(units + OFF_NAME), name, 16, &got);
    if (strcmp(name, "bot-1") != 0) {
        printf("[-] 验证失败：units+0x%X 读到的名字是 \"%s\"，不是 bot-1\n", OFF_NAME, name);
        CloseHandle(h);
        return 1;
    }

    uint64_t hp_addr = units + OFF_HP;
    float hp0 = 0.0f;
    read_f32(h, hp_addr, &hp0);
    printf("[+] 链校验通过：units = %p，hp 地址 = %p，当前 hp = %g\n",
           (void *)(uintptr_t)units, (void *)(uintptr_t)hp_addr, hp0);

    /* ---------- 2. 拼机器码 ---------- */
    /* 这段字节是用 gas 汇编写好、objcopy 抽出来的（不是手搓的）：
     *
     *   mov  rbx, <hp 地址>          ; rbx 非易失，Sleep 不会破坏它
     *   mov  r12, <Sleep 地址>       ; r12 非易失
     * loop:
     *   mov  dword [rbx], <9999.0f>
     *   mov  ecx, 50
     *   and  rsp, -16                ; 保证 16 字节对齐（调用约定的硬要求）
     *   sub  rsp, 32                 ; 32 字节影子空间
     *   call r12
     *   add  rsp, 32
     *   jmp  loop
     *
     * 两个坑：① 用 rax/rcx/rdx 存地址会被 Sleep 踩掉；② rsp 不对齐会在 callee 里炸。 */
    unsigned char sc[] = {
        0x48, 0xBB, 0, 0, 0, 0, 0, 0, 0, 0,        /* +0  mov rbx, hp_addr     */
        0x49, 0xBC, 0, 0, 0, 0, 0, 0, 0, 0,        /* +10 mov r12, sleep_addr  */
        0xC7, 0x03, 0, 0, 0, 0,                    /* +20 mov [rbx], f32       */
        0xB9, 0x32, 0x00, 0x00, 0x00,              /* +26 mov ecx, 50          */
        0x48, 0x83, 0xE4, 0xF0,                    /* +31 and rsp, -16         */
        0x48, 0x83, 0xEC, 0x20,                    /* +35 sub rsp, 32          */
        0x41, 0xFF, 0xD4,                          /* +39 call r12             */
        0x48, 0x83, 0xC4, 0x20,                    /* +42 add rsp, 32          */
        0xEB, 0xE4,                                /* +46 jmp +20              */
        0xC3                                       /* +48 ret                  */
    };

    uint64_t sleep_addr = (uint64_t)(uintptr_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "Sleep");
    if (!sleep_addr) {
        printf("[-] 找不到 Sleep 的地址\n");
        CloseHandle(h);
        return 1;
    }

    float value = 9999.0f;
    memcpy(sc + 2, &hp_addr, 8);      /* mov rax, hp_addr  */
    memcpy(sc + 12, &sleep_addr, 8);  /* mov rdx, sleep    */
    memcpy(sc + 22, &value, 4);       /* 要写的 float      */

    printf("[+] 机器码 %zu 字节：hp 地址写死在 +2，Sleep 地址写死在 +12\n", sizeof(sc));
    printf("[+] Sleep 地址 = %p（系统 DLL 在同一次开机里共用基址）\n", (void *)(uintptr_t)sleep_addr);

    /* ---------- 3. 在目标里申请可执行内存 ---------- */
    void *remote = VirtualAllocEx(h, NULL, sizeof(sc), MEM_COMMIT | MEM_RESERVE,
                                  PAGE_EXECUTE_READWRITE);
    if (!remote) {
        printf("[-] VirtualAllocEx 失败，GetLastError = %lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }
    printf("[+] 目标进程里分配到的地址 = %p（PAGE_EXECUTE_READWRITE）\n", remote);

    /* ---------- 4. 写进去 ---------- */
    SIZE_T put = 0;
    if (!WriteProcessMemory(h, remote, sc, sizeof(sc), &put) || put != sizeof(sc)) {
        printf("[-] WriteProcessMemory 失败，GetLastError = %lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }
    printf("[+] 机器码写入成功（%zu 字节）\n", (size_t)put);

    /* ---------- 5. 让目标开线程跑它 ---------- */
    HANDLE th = CreateRemoteThread(h, NULL, 0, (LPTHREAD_START_ROUTINE)remote, NULL, 0, NULL);
    if (!th) {
        printf("[-] CreateRemoteThread 失败，GetLastError = %lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }
    printf("[+] 远程线程已启动，句柄 %p\n\n", (void *)th);

    printf("下面 10 次读数，每 250ms 一次。目标每 100ms 掉 2.5 血，\n");
    printf("但我们的线程每 50ms 写一次 9999 —— 看谁赢。\n\n");

    for (int i = 0; i < 10; i++) {
        float now = 0.0f;
        read_f32(h, hp_addr, &now);
        printf("   第 %2d 次：hp = %8.1f\n", i + 1, now);
        Sleep(250);
    }

    printf("\n[+] 线程还在目标里跑。关掉靶子窗口，它就跟着消失。\n");
    printf("    想停掉又不关程序？给你留个作业：在机器码里加一个退出标志。\n");

    CloseHandle(th);
    CloseHandle(h);
    return 0;
}
