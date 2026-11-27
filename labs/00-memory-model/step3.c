/* step3.c -- p 和 hp 到底是什么关系
 *
 * 一句话：p 不是"拴住" hp，p 只是抄了一份 hp 的编号。
 * 这个程序用三个动作把这句话钉死：
 *   1. 两个指针装同一个编号 -> 它们指向同一格
 *   2. 改 hp -> 通过两个指针都能看到变化
 *   3. 把 p 改成指向别人 -> hp 那边一点影响都没有
 *
 * 编译：gcc -fexec-charset=GBK -g -O0 -Wall -o step3.exe step3.c
 */
#include <stdio.h>
#include <stdint.h>

int main(void)
{
    int  hp = 92;
    int  mp = 50;

    int *p = &hp;      /* 把 hp 的编号抄进 p 那 8 个字节 */
    int *q = &hp;      /* 再抄一份，抄到 q 里 */

    printf("=== 1. 两个指针，同一个目标 ===\n");
    printf("&hp = %p\n", (void *)&hp);
    printf("p   = %p   (p 里装的数)\n", (void *)p);
    printf("q   = %p   (q 里装的数)\n", (void *)q);
    printf("p == q 吗？%s\n\n", p == q ? "是" : "不是");

    printf("=== 2. 它们指的是同一格 ===\n");
    printf("*p = %d, *q = %d, hp = %d\n", *p, *q, hp);

    hp = 7;                                  /* 只改 hp 这个变量 */
    printf("把 hp 改成 7 之后：*p = %d, *q = %d, hp = %d\n\n", *p, *q, hp);

    *p = 300;                                /* 顺着 p 写，写的是同一格 */
    printf("执行 *p = 300 之后：hp = %d, *q = %d\n\n", hp, *q);

    printf("=== 3. 把 p 改成指向别人：那个联系只是一个数 ===\n");
    p = &mp;                                 /* 只换掉 p 那 8 个字节里的数 */
    printf("现在 p = %p, *p = %d   (指向 mp)\n", (void *)p, *p);
    printf("q = %p, *q = %d   (q 没动，还指向 hp)\n", (void *)q, *q);
    printf("hp = %d, mp = %d\n\n", hp, mp);

    *p = 800;                                /* 顺着 p 写，这次写的是 mp */
    printf("执行 *p = 800 之后：mp = %d, hp = %d   (hp 没被碰)\n\n", mp, hp);

    printf("=== 4. 这三个变量各自住在哪 ===\n");
    printf("&hp = %p\n", (void *)&hp);
    printf("&mp = %p\n", (void *)&mp);
    printf("&p  = %p   (p 自己也是一个格子)\n", (void *)&p);
    printf("&q  = %p\n", (void *)&q);

    printf("\n=== 5. p 里面那 8 个字节（低位在前）===\n");
    unsigned char *b = (unsigned char *)&p;
    for (size_t i = 0; i < sizeof(p); i++)
        printf("   &p + %zu = %02X\n", i, b[i]);

    return 0;
}
