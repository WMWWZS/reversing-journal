/* target.c -- lab 03 的活靶子：血量在真实时间里持续下降
 *
 * 和 lab 01 的 game.c 用同一个 Entity 结构体（include 同一份 entity.h），
 * 区别只有一个：game.c 打印三次就停住等人回车，target.c 一直跑。
 *
 * 为什么要"一直跑"：只有它会持续覆盖血量，你才能亲眼看到
 *   写进去 9999 -> 0.1 秒后被打回原形   （写一次不够）
 *   每 50ms 重写一遍 -> 数字钉死在 9999  （这叫"锁血"）
 *
 * 编译：gcc -g -O0 -no-pie -Wall -o target.exe target.c
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <stddef.h>

#include "../01-struct-layout/entity.h"   /* 全场唯一的结构体真相源 */

#define N 4

static Entity g_entities[N];   /* 全局数组：地址写死在 exe 里，-no-pie 下每次运行都一样 */

static void init(void)
{
    for (int i = 0; i < N; i++) {
        g_entities[i].id   = (uint32_t)(i + 1);
        g_entities[i].hp   = 100.0f;
        g_entities[i].team = (uint8_t)(i % 2);
        snprintf(g_entities[i].name, sizeof(g_entities[i].name), "bot-%d", i + 1);
    }
}

/* 把光标挪回左上角再重画，不闪屏 */
static void draw(int frame)
{
    COORD top = {0, 0};
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), top);

    printf("live target  |  g_entities = %p  |  hp at +0x%02X  |  frame %d      \n",
           (void *)g_entities, (unsigned)offsetof(Entity, hp), frame);
    printf("----------------------------------------------------------------------\n");
    for (int i = 0; i < N; i++) {
        printf("   [%d] %-6s  hp = %8.1f   %s        \n",
               i, g_entities[i].name, g_entities[i].hp,
               g_entities[i].hp <= 0.0f ? "<DEAD>" : "");
    }
    printf("----------------------------------------------------------------------\n");
    printf("hp -2.5 per frame (100 ms).  ESC = quit.                             \n");
}

int main(void)
{
    init();

    int frame = 0;
    for (;;) {
        if (_kbhit()) {
            int c = _getch();
            if (c == 27)      /* ESC */
                break;
        }

        for (int i = 0; i < N; i++) {
            g_entities[i].hp -= 2.5f;
            if (g_entities[i].hp < 0.0f)
                g_entities[i].hp = 0.0f;
        }

        draw(++frame);
        Sleep(100);
    }

    printf("\nbye.\n");
    return 0;
}
