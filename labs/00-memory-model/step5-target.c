/* step5-target.c -- 一个只有 4 个字节值得看的靶子
 *
 * 它做三件事：
 *   1. 把 hp = 92.5f 放进一个全局结构体（-no-pie，地址固定）
 *   2. 把自己的 pid 和 hp 的地址写进 where.txt，方便对方程序找到
 *   3. 停在 getchar 上，等你按回车才退出
 *
 * 编译：gcc -fexec-charset=GBK -g -O0 -no-pie -Wall -o step5-target.exe step5-target.c
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stddef.h>

typedef struct {
    int   id;       /* +0x00 */
    float pos[3];   /* +0x04 */
    float hp;       /* +0x10 */
} Unit;

static Unit g_unit;   /* 全局：地址 = 模块基址 + 固定偏移 */

int main(void)
{
    g_unit.id     = 1;
    g_unit.pos[0] = 0.0f;
    g_unit.pos[1] = 0.0f;
    g_unit.pos[2] = 0.0f;
    g_unit.hp     = 92.5f;

    unsigned long pid = (unsigned long)GetCurrentProcessId();
    void *hp_addr = (void *)&g_unit.hp;

    printf("进程号 pid     = %lu\n", pid);
    printf("结构体首地址   = %p\n", (void *)&g_unit);
    printf("hp 的地址      = %p   (离结构体开头 0x%zX)\n",
           hp_addr, offsetof(Unit, hp));
    printf("hp 的值        = %g\n\n", g_unit.hp);

    FILE *f = fopen("where.txt", "w");
    if (f) {
        fprintf(f, "%lu %p\n", pid, hp_addr);
        fclose(f);
        printf("已写入 where.txt，对方程序会从里面读地址\n");
    } else {
        printf("写不了 where.txt\n");
    }

    printf("等着…… 按回车结束本程序\n");
    getchar();
    return 0;
}
