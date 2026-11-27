/* target.c -- lab06 的靶子：v3。会一直掉血，专门用来被注入
 *
 * 和 lab05 的 v2 是同一个程序结构（同一套全局变量顺序，所以 g_world 槽位还是 0x7050），
 * 只多了一件事：每 100ms 掉 2.5 血，让"注入的代码有没有起作用"肉眼可见。
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

typedef struct {
    uint32_t id;
    float    pos[3];
    float    hp;
    uint8_t  team;
    uint8_t  _pad[3];
    char     name[16];
} Entity;

typedef struct {
    Entity *units;
    int     count;
    int     _pad;
} World;

static unsigned long long g_build_stamp;
static unsigned long long g_frame_counter;
static int  g_verbose;
static int  g_max_units;
static char g_version[8];

static World *g_world;

static void draw(int frame)
{
    COORD top = {0, 0};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), top);

    printf("=== lab06 target v3 ===  pid %lu   frame %-5d                  \n",
           (unsigned long)GetCurrentProcessId(), frame);
    printf("g_world 槽位 = %p                                                  \n",
           (void *)&g_world);
    printf("debug: verbose=%d stamp=%llu                                       \n",
           g_verbose, (unsigned long long)g_build_stamp);
    printf("------------------------------------------------------------------\n");
    for (int i = 0; i < g_world->count; i++) {
        Entity *e = &g_world->units[i];
        printf("   [%d] %-6s hp = %10.1f  %s                        \n",
               i, e->name, e->hp, e->hp <= 0.0f ? "<DEAD>" : "");
    }
    printf("------------------------------------------------------------------\n");
    printf("hp -2.5 per 100 ms.  ESC = quit.                                  \n");
}

int main(void)
{
    World *w = (World *)calloc(1, sizeof(World));
    Entity *u = (Entity *)calloc(4, sizeof(Entity));
    if (!w || !u) {
        printf("分配失败\n");
        return 1;
    }

    w->units = u;
    w->count = 4;
    for (int i = 0; i < 4; i++) {
        u[i].id   = (uint32_t)(i + 1);
        u[i].hp   = 100.0f;
        u[i].team = (uint8_t)(i % 2);
        snprintf(u[i].name, sizeof(u[i].name), "bot-%d", i + 1);
    }
    g_world = w;
    snprintf(g_version, sizeof(g_version), "3.0.0");
    g_max_units = 4;

    int frame = 0;
    for (;;) {
        if (_kbhit()) {
            int c = _getch();
            if (c == 27)
                break;
        }

        for (int i = 0; i < g_world->count; i++) {
            g_world->units[i].hp -= 2.5f;
            if (g_world->units[i].hp < 0.0f)
                g_world->units[i].hp = 0.0f;
        }
        g_frame_counter++;

        draw(++frame);
        Sleep(100);
    }

    printf("\nbye.\n");
    free(g_world->units);
    free(g_world);
    return 0;
}
