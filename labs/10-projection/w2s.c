/* w2s.c -- 世界坐标 -> 屏幕像素（透视投影）
 *
 * 这是自瞄/透视的数学内核：把"敌人站在世界哪里"变成"他在我屏幕上哪个像素"。
 *
 * 两步：
 *   1. 把敌人相对相机的位置，投到相机的"右方向 / 前方向"两个轴上
 *        depth   = 前方投影 = 沿视线多远
 *        lateral = 左右投影 = 偏左还是偏右
 *   2. 用视野角做缩放：
 *        屏幕 x = 中心 + (lateral / depth) * (半宽 / tan(fov/2))
 *
 * 本程序同时用"角度法"再算一遍（atan2 求方位角，减去相机 yaw），
 * 两个独立算法结果必须一致——这就是这一节的验证方式。
 *
 * 配置（config.txt）：
 *   process=target.exe
 *   slot=0x8030        # 模块内 g_world 槽位
 *   ptr_size=8
 *   cam_off=0x00       # World.cam
 *   units_off=0x18     # World.units
 *   count_off=0x20     # World.count
 *   stride=0x24        # sizeof(Unit)
 *   screen=1280x720
 *
 * 编译：gcc -fexec-charset=GBK -g -O0 -Wall -o w2s.exe w2s.c -lpsapi -lm
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#define DEG2RAD 0.017453292519943295f

typedef struct { float x, y, z; } Vec3;

typedef struct {
    char   process[64];
    size_t slot, cam_off, units_off, count_off, stride;
    int    ptr_size, sw, sh;
} Cfg;

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && strchr(" \t\r\n", e[-1])) *--e = 0;
    return s;
}

static int load_cfg(const char *path, Cfg *c)
{
    memset(c, 0, sizeof(*c));
    c->ptr_size = 8;
    c->sw = 1280; c->sh = 720;

    FILE *f = fopen(path, "r");
    if (!f) { printf("[-] 打不开 %s\n", path); return 0; }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1);

        if (!_stricmp(k, "process"))         strncpy(c->process, v, sizeof(c->process) - 1);
        else if (!_stricmp(k, "slot"))       c->slot = (size_t)strtoull(v, NULL, 0);
        else if (!_stricmp(k, "ptr_size"))   c->ptr_size = atoi(v);
        else if (!_stricmp(k, "cam_off"))    c->cam_off = (size_t)strtoull(v, NULL, 0);
        else if (!_stricmp(k, "units_off"))  c->units_off = (size_t)strtoull(v, NULL, 0);
        else if (!_stricmp(k, "count_off"))  c->count_off = (size_t)strtoull(v, NULL, 0);
        else if (!_stricmp(k, "stride"))     c->stride = (size_t)strtoull(v, NULL, 0);
        else if (!_stricmp(k, "screen"))     sscanf(v, "%dx%d", &c->sw, &c->sh);
    }
    fclose(f);
    return c->process[0] != 0 && c->slot != 0 && c->stride != 0;
}

static DWORD find_pid(const char *exe)
{
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do { if (!_stricmp(pe.szExeFile, exe)) { pid = pe.th32ProcessID; break; } }
        while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static int get_base(HANDLE h, uint64_t *base)
{
    HMODULE mod = NULL; DWORD need = 0;
    if (!EnumProcessModules(h, &mod, sizeof(mod), &need)) return 0;
    MODULEINFO mi;
    if (!GetModuleInformation(h, mod, &mi, sizeof(mi))) return 0;
    *base = (uint64_t)(uintptr_t)mi.lpBaseOfDll;
    return 1;
}

static int rd(HANDLE h, uint64_t a, void *buf, size_t n)
{
    SIZE_T got = 0;
    return ReadProcessMemory(h, (const void *)(uintptr_t)a, buf, n, &got) && got == n;
}

static int rd_ptr(HANDLE h, uint64_t a, int ptr_size, uint64_t *out)
{
    if (ptr_size == 8) return rd(h, a, out, 8);
    uint32_t v = 0;
    if (!rd(h, a, &v, 4)) return 0;
    *out = (uint64_t)v;
    return 1;
}

/* 方法 A：点积投影（真正的透视投影算法） */
static void project_dot(const Vec3 *cpos, float yaw, float fov,
                        int sw, int sh, const Vec3 *p, int *sx, int *sy, int *visible)
{
    float dx = p->x - cpos->x, dy = p->y - cpos->y, dz = p->z - cpos->z;

    float fwd_x = cosf(yaw * DEG2RAD),  fwd_y = sinf(yaw * DEG2RAD);
    float rgt_x = sinf(yaw * DEG2RAD),  rgt_y = -cosf(yaw * DEG2RAD);

    float depth   = dx * fwd_x + dy * fwd_y;      /* 沿视线多远 */
    float lateral = dx * rgt_x + dy * rgt_y;      /* 偏左/偏右 */

    if (depth <= 0.1f) { *visible = 0; *sx = *sy = -1; return; }

    float half_w = sw / 2.0f, half_h = sh / 2.0f;
    float t_half = tanf((fov / 2.0f) * DEG2RAD);
    /* 出界判定：横向投影超过视野半角就不可见（和角度法必须一致） */
    if (fabsf(lateral / depth) > t_half) { *visible = 0; *sx = *sy = -1; return; }
    float scale  = half_w / t_half;

    float horiz = sqrtf(dx * dx + dy * dy);
    *sx = (int)lroundf(half_w + (lateral / depth) * scale);
    *sy = (int)lroundf(half_h - (horiz > 0.001f ? dz / horiz : 0.0f) * half_h);
    *visible = 1;
}

/* 方法 B：角度法（用 atan2 求方位角再减去相机朝向）——独立实现，用来对拍 */
static void project_angle(const Vec3 *cpos, float yaw, float fov,
                          int sw, int sh, const Vec3 *p, int *sx, int *sy, int *visible)
{
    float dx = p->x - cpos->x, dy = p->y - cpos->y, dz = p->z - cpos->z;
    float ang = atan2f(dy, dx) / DEG2RAD;
    float rel = ang - yaw;
    while (rel > 180.0f) rel -= 360.0f;
    while (rel < -180.0f) rel += 360.0f;

    if (fabsf(rel) >= fov / 2.0f) { *visible = 0; *sx = *sy = -1; return; }

    float half_w = sw / 2.0f, half_h = sh / 2.0f;
    float t = tanf(rel * DEG2RAD) / tanf((fov / 2.0f) * DEG2RAD);
    *sx = (int)lroundf(half_w - t * half_w);

    float horiz = sqrtf(dx * dx + dy * dy);
    *sy = (int)lroundf(half_h - (horiz > 0.001f ? dz / horiz : 0.0f) * half_h);
    *visible = 1;
}

int main(int argc, char **argv)
{
    const char *cfgpath = (argc > 1) ? argv[1] : "config.txt";
    Cfg c;
    if (!load_cfg(cfgpath, &c)) { printf("[-] 配置不完整\n"); return 1; }

    DWORD pid = find_pid(c.process);
    if (!pid) { printf("[-] 没找到 %s，先开靶子\n", c.process); return 1; }

    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) { printf("[-] OpenProcess 失败 %lu\n", GetLastError()); return 1; }

    uint64_t base = 0, world = 0, units = 0;
    if (!get_base(h, &base) || !rd_ptr(h, base + c.slot, c.ptr_size, &world) || !world) {
        printf("[-] 读不到 World（slot=0x%zX）\n", c.slot); CloseHandle(h); return 1;
    }
    if (!rd_ptr(h, world + c.units_off, c.ptr_size, &units) || !units) {
        printf("[-] 读不到 units\n"); CloseHandle(h); return 1;
    }

    Vec3  cpos; float yaw = 0, pitch = 0, fov = 0;
    int   count = 0;
    rd(h, world + c.cam_off, &cpos, sizeof(cpos));
    rd(h, world + c.cam_off + 0x0C, &yaw, 4);
    rd(h, world + c.cam_off + 0x10, &pitch, 4);
    rd(h, world + c.cam_off + 0x14, &fov, 4);
    rd(h, world + c.count_off, &count, 4);
    if (count < 0 || count > 64) count = 0;

    printf("[+] pid=%lu  base=0x%llX  World=0x%llX  units=0x%llX  count=%d\n",
           (unsigned long)pid, (unsigned long long)base,
           (unsigned long long)world, (unsigned long long)units, count);
    printf("[+] 相机：pos(%.1f,%.1f,%.1f)  yaw=%.1f  pitch=%.1f  fov=%.0f  屏幕 %dx%d\n\n",
           cpos.x, cpos.y, cpos.z, yaw, pitch, fov, c.sw, c.sh);

    printf("    单位          世界坐标                角度法        点积法       差\n");
    printf("    ------------  ----------------------  ------------  ------------  ----\n");

    int maxdiff = 0;
    for (int i = 0; i < count; i++) {
        Vec3 p; char name[17] = {0};
        uint64_t ent = units + (size_t)i * c.stride;
        if (!rd(h, ent, &p, sizeof(p))) continue;
        rd(h, ent + 0x14, name, 16);

        int ax, ay, av, bx, by, bv;
        project_angle(&cpos, yaw, fov, c.sw, c.sh, &p, &ax, &ay, &av);
        project_dot  (&cpos, yaw, fov, c.sw, c.sh, &p, &bx, &by, &bv);

        if (av != bv) { printf("    %-12s 判定不一致！\n", name); continue; }

        if (!av) {
            printf("    %-12s (%5.1f,%5.1f,%4.1f)     画面外        画面外       -\n",
                   name, p.x, p.y, p.z);
            continue;
        }
        int d = abs(ax - bx) + abs(ay - by);
        if (d > maxdiff) maxdiff = d;
        printf("    %-12s (%5.1f,%5.1f,%4.1f)   (%4d,%4d)   (%4d,%4d)   %d\n",
               name, p.x, p.y, p.z, ax, ay, bx, by, d);
    }

    printf("\n[+] 两种算法最大差异 = %d 像素：%s\n", maxdiff,
           maxdiff <= 1 ? "一致（这就是验证）" : "不一致，检查公式或符号");

    CloseHandle(h);
    return maxdiff <= 1 ? 0 : 2;
}
