/* probe.c —— 只读探测器：在另一个进程里找到某个 float 的坐落位置
 *
 * 它干的事就四步：
 *   1. 按进程名找到 game.exe 的 PID
 *   2. 打开进程，只要"查询信息 + 读内存"两个权限（只要读，不要写）
 *   3. 把整个模块的映像读到本地缓冲区
 *   4. 在缓冲区里搜 4 个字节（那个 float 的 IEEE754 编码）
 *
 * 用法：probe.exe 92.5
 * 编译：gcc -O0 -g -Wall -o probe.exe probe.c -lpsapi
 *
 * 输出一律 ASCII：cmd 按 GBK 解释 UTF-8 中文会乱码，这是本项目铁律。
 * 注意：这是只读工具。全程没有 WriteProcessMemory，也没打算加。
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* 按 exe 名找 PID */
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

/* 把命中点前后各 0x20 字节按行打印：左边偏移、中间十六进制、右边可打印字符 */
static void dump(const unsigned char *buf, size_t size, size_t hit)
{
    size_t start = (hit >= 0x20) ? hit - 0x20 : 0;
    printf("      offset | 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F | ascii\n");
    printf("      -------+-------------------------------------------------+-----------------\n");

    for (size_t off = start; off < start + 0x60 && off + 16 <= size; off += 16) {
        printf("      %05zX  | ", off);
        for (int k = 0; k < 16; k++)
            printf("%02X ", buf[off + k]);
        printf("| ");
        for (int k = 0; k < 16; k++) {
            unsigned char c = buf[off + k];
            putchar((c >= 32 && c < 127) ? c : '.');
        }
        putchar('\n');
    }
    printf("      hit at module offset 0x%zX\n", hit);
}

int main(int argc, char **argv)
{
    float target = 92.5f;                      /* 默认值，可用命令行覆盖 */
    if (argc > 1)
        target = (float)atof(argv[1]);

    char pat[4];
    memcpy(pat, &target, sizeof(pat));
    printf("[*] target value %g  ->  IEEE754 bytes %02X %02X %02X %02X\n",
           target, (unsigned char)pat[0], (unsigned char)pat[1],
           (unsigned char)pat[2], (unsigned char)pat[3]);

    DWORD pid = find_pid("game.exe");
    if (!pid) {
        printf("[-] game.exe not found. start it first (in another window).\n");
        return 1;
    }
    printf("[+] game.exe pid = %lu\n", (unsigned long)pid);

    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) {
        printf("[-] OpenProcess failed, GetLastError = %lu\n", GetLastError());
        return 1;
    }

    HMODULE mod = NULL;
    DWORD need = 0;
    if (!EnumProcessModules(h, &mod, sizeof(mod), &need)) {
        printf("[-] EnumProcessModules failed, GetLastError = %lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }

    MODULEINFO mi;
    if (!GetModuleInformation(h, mod, &mi, sizeof(mi))) {
        printf("[-] GetModuleInformation failed, GetLastError = %lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }

    unsigned char *base = (unsigned char *)mi.lpBaseOfDll;
    size_t size = mi.SizeOfImage;
    printf("[+] module base %p, image size 0x%zX bytes\n", (void *)base, size);

    unsigned char *buf = (unsigned char *)malloc(size);
    if (!buf) {
        printf("[-] local buffer allocation failed\n");
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
    printf("[+] read %zu bytes, scanning...\n", (size_t)got);

    int found = 0;
    for (size_t i = 0; i + 4 <= size && found < 8; i++) {
        if (memcmp(buf + i, pat, 4) == 0) {
            found++;
            printf("\n[+] hit #%d: address %p  (base + 0x%zX)\n",
                   found, (void *)(base + i), i);
            dump(buf, size, i);
        }
    }

    if (!found)
        printf("\n[-] no hit. check: is game.exe really running? value copied right?\n");

    free(buf);
    CloseHandle(h);
    return 0;
}
