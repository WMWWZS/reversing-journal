/* step1.c -- 最小的一步：地址、格子、指针
 *
 * 这个程序不碰别的进程，不碰结构体，只回答三个问题：
 *   1. 变量的地址到底是什么？（一个数字）
 *   2. 指针是什么？（一个装着地址的格子）
 *   3. 一个数字在内存里长什么样？（几个字节，什么顺序）
 *
 * 中文输出的前提：编译时加 -fexec-charset=GBK。
 * 源码是 UTF-8，直接 printf 到 GBK(936) 控制台会乱码；加了这句编译器会
 * 把字符串字面量转成 GBK 再写进 exe。
 *
 * 编译：gcc -fexec-charset=GBK -g -O0 -Wall -o step1.exe step1.c
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
    int  hp   = 92;          /* 一个整数 */
    int  mp   = 50;          /* 另一个整数 */
    char name[8] = "bot-1";  /* 一串字符 */

    /* ---- 1. 每个变量都住在某个编号的格子里 ---- */
    printf("=== 1. 变量住在有编号的格子里 ===\n");
    printf("hp    值 %-4d   地址 %p   (当成普通数字就是 %llu)\n",
           hp, (void *)&hp, (unsigned long long)(uintptr_t)&hp);
    printf("mp    值 %-4d   地址 %p\n", mp, (void *)&mp);
    printf("name  值 %-5s   地址 %p\n", name, (void *)name);

    /* ---- 2. 指针就是"装着地址"的格子 ---- */
    printf("\n=== 2. 指针 = 装着地址的格子 ===\n");
    int *p = &hp;                                  /* p 里装的是 hp 的地址 */
    printf("p      = %p   和 &hp 是同一个数\n", (void *)p);
    printf("*p     = %d     顺着这个地址，读那里的 4 个字节\n", *p);

    *p = 500;                                      /* 顺着地址写 */
    printf("执行 *p = 500 之后，hp = %d\n", hp);     /* hp 自己变了 */

    printf("&p     = %p   p 自己也住在某个格子里\n", (void *)&p);
    printf("sizeof(p) = %zu 字节\n", sizeof(p));    /* 64 位程序里是 8 */

    /* ---- 3. 一个值在内存里到底是哪几个字节 ---- */
    printf("\n=== 3. 一个数在内存里的样子 ===\n");

    hp = 92;
    unsigned char *b = (unsigned char *)&hp;
    printf("hp = %d，它的 4 个字节，从低地址到高地址：\n", hp);
    for (size_t i = 0; i < sizeof(hp); i++)
        printf("   &hp + %zu  =  %02X\n", i, b[i]);
    printf("   92 的十六进制是 5C，所以这 4 格是 5C 00 00 00\n");
    printf("   低位字节放在低地址，这叫小端序（little-endian）\n");

    unsigned char *pb = (unsigned char *)&p;
    printf("\np = %p，它的 8 个字节：\n", (void *)p);
    for (size_t i = 0; i < sizeof(p); i++)
        printf("   &p + %zu   =  %02X\n", i, pb[i]);
    printf("   倒过来念，就还原成那个地址\n");

    /* ---- 4. 记住这条，后面全用它 ---- */
    printf("\n=== 4. 一句话 ===\n");
    printf("地址 = 哪一格；偏移 = 离开头几个字节；指针 = 装着地址的格子\n");

    return 0;
}
