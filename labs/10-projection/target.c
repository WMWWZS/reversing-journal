/* target.c -- lab10 的靶子：一个有相机、有敌人的"小世界"
 *
 * 它不做渲染，只维护数据：
 *   - 相机在原点，朝向 yaw 每 30 度/秒匀速转；视野 90 度
 *   - 三个敌人固定站在世界坐标上
 *   - 每 100ms 打印一次相机角度和"我算出来的屏幕坐标"（供你对照）
 *
 * 你写的外挂（w2s.exe）要独立地读这些数据、自己算一遍屏幕坐标，两边必须对得上。
 *
 * 编译：gcc -fexec-charset=GBK -g -O0 -no-pie -Wall -o target.exe target.c
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

typedef struct { float x, y, z; } Vec3;

typedef struct {
    Vec3  pos;        /* +0x00  相机位置 */
    float yaw;        /* +0x0C  朝向（度，0 = 朝 +X，逆时针） */
    float pitch;      /* +0x10  俯仰（度，这一节恒为 0） */
    float fov;        /* +0x14  水平视野（度） */
} Camera;             /* sizeof = 0x18 */

typedef struct {
    Vec3  pos;        /* +0x00 世界坐标 */
    int   team;       /* +0x0C */
    float hp;         /* +0x10 */
    char  name[16];   /* +0x14 内部名 */
} Unit;               /* sizeof = 0x24 */

typedef struct {
    Camera cam;       /* +0x00 */
    Unit  *units;     /* +0x18 */
    int    count;     /* +0x20 */
} World;              /* sizeof = 0x28 */

static World *g_world;

#define SCREEN_W 1280
#define SCREEN_H 720
#define DEG2RAD  0.017453292519943295f

/* 独立的参考实现：用"角度"算屏幕坐标（和外挂那套点积算法互为验证） */
static void project_by_angle(const Camera *c, const Vec3 *p, int *sx, int *sy)
{
    float dx = p->x - c->pos.x;
    float dy = p->y - c->pos.y;
    float dz = p->z - c->pos.z;

    float ang = atan2f(dy, dx) / DEG2RAD;          /* 目标的世界方位角 */
    float rel = ang - c->yaw;                       /* 相对相机朝向 */
    while (rel > 180.0f)  rel -= 360.0f;
    while (rel < -180.0f) rel += 360.0f;

    if (fabsf(rel) >= c->fov / 2.0f) {              /* 出画面 */
        *sx = -1; *sy = -1;
        return;
    }

    float half_w = SCREEN_W / 2.0f;
    float half_h = SCREEN_H / 2.0f;
    float t = tanf(rel * DEG2RAD) / tanf((c->fov / 2.0f) * DEG2RAD);
    *sx = (int)(half_w - t * half_w);   /* 逆时针为正 -> 屏幕往左 */

    float dist = sqrtf(dx * dx + dy * dy);
    float tz = (dist > 0.001f) ? (dz / dist) : 0.0f;
    *sy = (int)(half_h - tz * half_h);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    World *w = (World *)calloc(1, sizeof(World));
    Unit  *u = (Unit *)calloc(3, sizeof(Unit));
    if (!w || !u) { printf("分配失败\n"); return 1; }

    w->cam.pos.x = 0.0f; w->cam.pos.y = 0.0f; w->cam.pos.z = 0.0f;
    w->cam.yaw   = 0.0f;
    w->cam.pitch = 0.0f;
    w->cam.fov   = 90.0f;
    w->units = u;
    w->count = 3;

    /* 三个敌人：正前方 8 米、右前 20 度、左前 20 度（都在 6~8 米处） */
    u[0].pos = (Vec3){  8.0f,  0.0f, 0.0f };
    u[1].pos = (Vec3){  7.5f,  2.7f, 0.0f };
    u[2].pos = (Vec3){  7.5f, -2.7f, 0.0f };
    for (int i = 0; i < 3; i++) {
        u[i].team = i;
        u[i].hp = 100.0f - i * 10.0f;
        snprintf(u[i].name, sizeof(u[i].name), "enemy-%d", i + 1);
    }
    g_world = w;

    unsigned char *base = (unsigned char *)GetModuleHandleA(NULL);
    printf("=== lab10 靶子：相机 + 三个敌人 ===\n");
    printf("pid        = %lu\n", (unsigned long)GetCurrentProcessId());
    printf("g_world    = %p   （模块内偏移 0x%zX）\n",
           (void *)&g_world, (size_t)((unsigned char *)&g_world - base));
    printf("World.cam  = +0x%zX（pos +0x00 / yaw +0x0C / pitch +0x10 / fov +0x14）\n",
           offsetof(World, cam));
    printf("World.units= +0x%zX，count = +0x%zX，Unit 步长 = 0x%zX\n",
           offsetof(World, units), offsetof(World, count), sizeof(Unit));
    printf("屏幕        = %dx%d，水平视野 = %.0f 度\n\n", SCREEN_W, SCREEN_H, w->cam.fov);

    int tick = 0;
    for (;;) {
        if (_kbhit()) { int c = _getch(); if (c == 27) break; }

        w->cam.yaw += 30.0f * 0.1f;          /* 每秒 30 度 */
        if (w->cam.yaw >= 360.0f) w->cam.yaw -= 360.0f;

        if (++tick % 10 == 0) {
            printf("\nyaw = %6.1f 度\n", w->cam.yaw);
            for (int i = 0; i < w->count; i++) {
                int sx, sy;
                project_by_angle(&w->cam, &u[i].pos, &sx, &sy);
                printf("   %-8s world(%5.1f,%5.1f,%4.1f)  ->  屏幕(%4d,%4d)%s\n",
                       u[i].name, u[i].pos.x, u[i].pos.y, u[i].pos.z, sx, sy,
                       sx < 0 ? "  [画面外]" : "");
            }
        }
        Sleep(100);
    }

    free(g_world->units);
    free(g_world);
    return 0;
}
