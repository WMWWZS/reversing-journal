/* chaincfg.c -- 配置驱动的指针链工具：一份配置，指向任意进程
 *
 * 和 lab04 的 chain.exe 的区别：那边把 target.exe / 0x7030 / 8 字节指针全写死在代码里，
 * 这边全部来自 config.txt。换成真实游戏时，你只改配置，不动代码。
 *
 * config.txt 格式（# 后面是注释）：
 *
 *   process=target.exe          # 目标进程名
 *   slot=0x7030                 # 模块内的指针槽偏移
 *   ptr_size=8                  # 目标里的指针宽度：8（64 位）或 4（32 位）
 *   hops=0x00,0x10              # 跳序列；最后一跳只加偏移，前面每一跳都"加偏移再读指针"
 *   type=float                  # 字段类型：float / int32
 *   stride=0x28                 # 可选：数组步长
 *   index=0                     # 可选：数组下标
 *
 * 解析规则（逐字照做，别猜）：
 *   1) 地址 a = 模块基址 + slot
 *   2) 读 ptr_size 个字节 -> p                          （第 1 次读）
 *   3) 对 hops[0..n-2]：读 p + hops[i] 处的指针 -> p     （中间每一跳）
 *   4) 最后：字段地址 = p + hops[n-1] + index * stride
 *   5) 读 type 个字节 -> 数值；write / hold 则反向写
 *
 * 用法：
 *   chaincfg.exe read  [config.txt]
 *   chaincfg.exe write <值> [config.txt]
 *   chaincfg.exe hold  <值> <秒> [config.txt]
 *
 * 编译：gcc -fexec-charset=GBK -g -O0 -Wall -o chaincfg.exe chaincfg.c -lpsapi
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ---------- 配置 ---------- */
typedef struct {
    char   process[64];
    size_t slot;
    int    ptr_size;      /* 4 或 8 */
    size_t hops[8];
    int    hop_count;
    char   type[16];      /* float / int32 */
    size_t stride;
    int    index;
} Cfg;

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        *--e = 0;
    return s;
}

static int load_cfg(const char *path, Cfg *c)
{
    memset(c, 0, sizeof(*c));
    c->ptr_size = 8;
    c->stride = 0;
    strcpy(c->type, "float");

    FILE *f = fopen(path, "r");
    if (!f) {
        printf("[-] 打不开配置文件 %s\n", path);
        return 0;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (*s == 0 || *s == '#' || *s == ';')
            continue;

        char *eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = 0;
        char *k = trim(s);
        char *v = trim(eq + 1);

        if (_stricmp(k, "process") == 0) {
            strncpy(c->process, v, sizeof(c->process) - 1);
        } else if (_stricmp(k, "slot") == 0) {
            c->slot = (size_t)strtoull(v, NULL, 0);
        } else if (_stricmp(k, "ptr_size") == 0) {
            c->ptr_size = atoi(v);
        } else if (_stricmp(k, "hops") == 0) {
            c->hop_count = 0;
            char *p = v;
            while (*p && c->hop_count < 8) {
                c->hops[c->hop_count++] = (size_t)strtoull(p, &p, 0);
                while (*p == ',' || *p == ' ') p++;
            }
        } else if (_stricmp(k, "type") == 0) {
            strncpy(c->type, v, sizeof(c->type) - 1);
        } else if (_stricmp(k, "stride") == 0) {
            c->stride = (size_t)strtoull(v, NULL, 0);
        } else if (_stricmp(k, "index") == 0) {
            c->index = atoi(v);
        }
    }
    fclose(f);

    if (!c->process[0] || c->hop_count == 0 ||
        (c->ptr_size != 4 && c->ptr_size != 8)) {
        printf("[-] 配置不完整：process / hops / ptr_size(4|8) 必须有\n");
        return 0;
    }
    return 1;
}

/* ---------- 进程 ---------- */
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

/* ---------- 读指针（按 ptr_size） ---------- */
static int read_ptr(HANDLE h, uint64_t addr, int ptr_size, uint64_t *out)
{
    SIZE_T got = 0;
    if (ptr_size == 8) {
        uint64_t v = 0;
        if (!ReadProcessMemory(h, (const void *)(uintptr_t)addr, &v, 8, &got) || got != 8)
            return 0;
        *out = v;
        return 1;
    } else {
        uint32_t v = 0;
        if (!ReadProcessMemory(h, (const void *)(uintptr_t)addr, &v, 4, &got) || got != 4)
            return 0;
        *out = (uint64_t)v;      /* 零扩展到 64 位再当地址用 */
        return 1;
    }
}

static int read_value(HANDLE h, uint64_t addr, const Cfg *c, double *out)
{
    SIZE_T got = 0;
    if (_stricmp(c->type, "int32") == 0) {
        int32_t v = 0;
        if (!ReadProcessMemory(h, (const void *)(uintptr_t)addr, &v, 4, &got) || got != 4)
            return 0;
        *out = (double)v;
    } else {
        float v = 0.0f;
        if (!ReadProcessMemory(h, (const void *)(uintptr_t)addr, &v, 4, &got) || got != 4)
            return 0;
        *out = (double)v;
    }
    return 1;
}

static int write_value(HANDLE h, uint64_t addr, const Cfg *c, double v)
{
    SIZE_T put = 0;
    if (_stricmp(c->type, "int32") == 0) {
        int32_t iv = (int32_t)v;
        return WriteProcessMemory(h, (void *)(uintptr_t)addr, &iv, 4, &put) && put == 4;
    }
    float fv = (float)v;
    return WriteProcessMemory(h, (void *)(uintptr_t)addr, &fv, 4, &put) && put == 4;
}

/* ---------- 走链 ---------- */
static int resolve(HANDLE h, const Cfg *c, uint64_t base,
                   uint64_t *field_addr, int verbose)
{
    uint64_t p = 0;

    if (!read_ptr(h, base + c->slot, c->ptr_size, &p) || p == 0) {
        printf("[-] 第 1 跳失败：读 %s（%d 字节）在 base+0x%zX 处拿不到指针\n",
               c->process, c->ptr_size, c->slot);
        return 0;
    }
    if (verbose)
        printf("[1] [base+0x%zX] 读 %d 字节 = 0x%llX\n",
               c->slot, c->ptr_size, (unsigned long long)p);

    for (int i = 0; i < c->hop_count; i++) {
        uint64_t next_addr = p + c->hops[i];

        if (i == c->hop_count - 1) {
            uint64_t final = next_addr + (uint64_t)c->index * c->stride;
            if (verbose)
                printf("[%d] 0x%llX + 0x%zX%s = 0x%llX   <- 字段地址\n",
                       i + 2, (unsigned long long)p, c->hops[i],
                       c->index ? " + index*stride" : "",
                       (unsigned long long)final);
            *field_addr = final;
            return 1;
        }

        if (!read_ptr(h, next_addr, c->ptr_size, &p) || p == 0) {
            printf("[-] 第 %d 跳失败：0x%llX + 0x%zX 处读不到指针\n",
                   i + 2, (unsigned long long)p, c->hops[i]);
            return 0;
        }
        if (verbose)
            printf("[%d] [0x%llX + 0x%zX] 读 %d 字节 = 0x%llX\n",
                   i + 2, (unsigned long long)(next_addr - c->hops[i]), c->hops[i],
                   c->ptr_size, (unsigned long long)p);
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *mode = (argc > 1) ? argv[1] : "read";
    const char *cfg_path = "config.txt";
    double value = 0.0;
    int seconds = 5;

    if (_stricmp(mode, "write") == 0) {
        if (argc < 3) {
            printf("[-] 用法：chaincfg.exe write <值> [config]\n");
            return 1;
        }
        value = atof(argv[2]);
        if (argc > 3) cfg_path = argv[3];
    } else if (_stricmp(mode, "hold") == 0) {
        if (argc < 4) {
            printf("[-] 用法：chaincfg.exe hold <值> <秒> [config]\n");
            return 1;
        }
        value = atof(argv[2]);
        seconds = atoi(argv[3]);
        if (argc > 4) cfg_path = argv[4];
    } else {
        if (argc > 2) cfg_path = argv[2];
    }
    if (seconds <= 0) seconds = 1;

    Cfg c;
    if (!load_cfg(cfg_path, &c))
        return 1;

    printf("[+] 配置 %s：process=%s slot=0x%zX ptr_size=%d hops=%d 段 type=%s\n",
           cfg_path, c.process, c.slot, c.ptr_size, c.hop_count, c.type);

    DWORD pid = find_pid(c.process);
    if (!pid) {
        printf("[-] 没找到进程 %s\n", c.process);
        return 1;
    }

    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
                           PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, pid);
    if (!h) {
        printf("[-] OpenProcess 失败，GetLastError = %lu\n", GetLastError());
        return 1;
    }

    uint64_t base = 0;
    if (!get_base(h, &base)) {
        printf("[-] 拿不到模块基址\n");
        CloseHandle(h);
        return 1;
    }
    printf("[+] pid = %lu，模块基址 = 0x%llX\n", (unsigned long)pid, (unsigned long long)base);

    uint64_t field = 0;
    if (!resolve(h, &c, base, &field, 1)) {
        CloseHandle(h);
        return 1;
    }

    double cur = 0.0;
    if (read_value(h, field, &c, &cur))
        printf("[+] 当前值 = %g\n", cur);
    else
        printf("[-] 字段地址 0x%llX 读不出来：指针宽度或偏移写错了？\n",
               (unsigned long long)field);

    if (_stricmp(mode, "read") == 0) {
        CloseHandle(h);
        return 0;
    }

    if (_stricmp(mode, "write") == 0) {
        if (!write_value(h, field, &c, value)) {
            printf("[-] 写失败，GetLastError = %lu\n", GetLastError());
        } else {
            double now = 0.0;
            read_value(h, field, &c, &now);
            printf("[+] 写入 %g，回读 = %g\n", value, now);
        }
        CloseHandle(h);
        return 0;
    }

    if (_stricmp(mode, "hold") == 0) {
        int ticks = seconds * 20;
        printf("[+] 保持 %g，持续 %d 秒（每 50ms 写一次）\n", value, seconds);
        for (int i = 0; i < ticks; i++) {
            write_value(h, field, &c, value);
            if (i % 20 == 0) {
                double now = 0.0;
                read_value(h, field, &c, &now);
                printf("    t = %2d 秒   值 = %g\n", i / 20, now);
            }
            Sleep(50);
        }
        Sleep(400);
        double after = 0.0;
        read_value(h, field, &c, &after);
        printf("[+] 松手 0.4 秒后 = %g\n", after);
        CloseHandle(h);
        return 0;
    }

    printf("[-] 不认识的模式 %s\n", mode);
    CloseHandle(h);
    return 1;
}
