/* step5-reader.c -- 跨进程读取 = 一次 memcpy，只是源不在自己这边
 *
 * 这个程序只做一件事，但要把它做到能对照：
 *   1. 从 where.txt 拿到 pid 和远程地址
 *   2. OpenProcess 要"查询信息 + 读内存"
 *   3. ReadProcessMemory 把对方那 4 个字节抄进我的 float
 *   4. 同一份字节，用本地 memcpy 再做一次 -- 两个结果必须一模一样
 *
 * 第 4 步是关键：它证明跨进程读没有魔法，就是 memcpy 加了一个 handle。
 *
 * 编译：gcc -fexec-charset=GBK -g -O0 -Wall -o step5-reader.exe step5-reader.c -lpsapi
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

int main(void)
{
    unsigned long pid = 0;
    unsigned long long addr = 0;

    FILE *f = fopen("where.txt", "r");
    if (!f) {
        printf("[-] 找不到 where.txt。先运行 step5-target.exe。\n");
        return 1;
    }
    if (fscanf(f, "%lu %llx", &pid, &addr) != 2) {
        printf("[-] where.txt 内容不对。\n");
        fclose(f);
        return 1;
    }
    fclose(f);

    printf("[+] 目标进程 pid  = %lu\n", pid);
    printf("[+] 远程地址      = 0x%llX   (这就是 g_unit.hp)\n\n", addr);

    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, (DWORD)pid);
    if (!h) {
        printf("[-] OpenProcess 失败，GetLastError = %lu\n", GetLastError());
        return 1;
    }

    /* --- 1. 跨进程：把对方那 4 个字节抄过来 --- */
    float remote = 0.0f;
    SIZE_T got = 0;
    if (!ReadProcessMemory(h, (const void *)(uintptr_t)addr, &remote, sizeof(remote), &got)
        || got != sizeof(remote)) {
        printf("[-] ReadProcessMemory 失败，GetLastError = %lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }

    /* --- 2. 同一个数，在我自己的进程里 memcpy 一次 --- */
    float local = 92.5f;
    unsigned char local_bytes[4];
    memcpy(local_bytes, &local, 4);

    unsigned char *rb = (unsigned char *)&remote;

    printf("=== 抄回来的东西 ===\n");
    printf("远程的 4 个字节: %02X %02X %02X %02X\n", rb[0], rb[1], rb[2], rb[3]);
    printf("本地的 4 个字节: %02X %02X %02X %02X\n",
           local_bytes[0], local_bytes[1], local_bytes[2], local_bytes[3]);
    printf("完全一样吗？  : %s\n", memcmp(rb, local_bytes, 4) == 0 ? "一样" : "不一样");
    printf("按 float 解释  : 远程 = %g，本地 = %g\n\n", remote, local);

    printf("=== 重点在这里 ===\n");
    printf("ReadProcessMemory(h, 地址, 缓冲区, 4) 就等于 memcpy(缓冲区, 地址, 4)，\n");
    printf("只多了一样东西：这个地址属于哪个进程（h）。\n");
    printf("而 h 只能用 OpenProcess 换，换的时候要权限。\n\n");

    printf("=== 这是拷贝，不是实时窗口 ===\n");
    printf("函数返回的那一刻，我缓冲区里的 4 个字节就定住了。\n");
    printf("之后靶子再把 hp 改成别的值，我手上这份还是 %g。\n", remote);

    CloseHandle(h);
    return 0;
}
