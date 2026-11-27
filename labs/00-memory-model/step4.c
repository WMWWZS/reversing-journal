/* step4.c -- 一个 float 在内存里到底长什么样
 *
 * 回答三个问题：
 *   1. 92.5 在内存里是哪 4 个字节？
 *   2. 这 4 个字节怎么倒推回 92.5？（符号 / 指数 / 尾数）
 *   3. 为什么搜内存时必须按 4 个字节精确匹配，float 和 double 不能混
 *
 * 编译：gcc -fexec-charset=GBK -g -O0 -Wall -o step4.exe step4.c
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* 不用 math.h：2 的整数次幂自己算 */
static double pow2i(int k)
{
    double r = 1.0;
    if (k >= 0) {
        for (int i = 0; i < k; i++) r *= 2.0;
    } else {
        for (int i = 0; i < -k; i++) r /= 2.0;
    }
    return r;
}

static void explain(float v)
{
    uint32_t u;
    memcpy(&u, &v, sizeof(u));

    unsigned char *b = (unsigned char *)&v;

    printf("屏幕上的值   : %g\n", v);
    printf("32 位十六进制: 0x%08X\n", u);
    printf("内存里的字节 : %02X %02X %02X %02X   (低地址在前)\n",
           b[0], b[1], b[2], b[3]);

    uint32_t sign = (u >> 31) & 0x1;
    uint32_t expf = (u >> 23) & 0xFF;
    uint32_t mant = u & 0x7FFFFF;

    printf("  符号位      : %u        (0 = 正数)\n", sign);
    printf("  指数字段    : %u (0x%02X)，真实指数 = %u - 127 = %d\n",
           expf, expf, expf, (int)expf - 127);
    printf("  尾数        : 0x%06X   (隐含的那个 1 不算在里面)\n", mant);

    double v2 = (sign ? -1.0 : 1.0) * (1.0 + (double)mant / 8388608.0) * pow2i((int)expf - 127);
    printf("  反推回去    : %g，和原值相等吗？%s\n\n", v2, (float)v2 == v ? "相等" : "不相等");
}

int main(void)
{
    printf("=== 1. 三个你会天天遇到的 float ===\n\n");
    explain(92.5f);
    explain(100.0f);
    explain(2.5f);

    printf("=== 2. float 是 4 字节，double 是 8 字节 ===\n");
    printf("sizeof(float)  = %zu\n", sizeof(float));
    printf("sizeof(double) = %zu\n", sizeof(double));

    float  f = 92.5f;
    double d = 92.5;

    unsigned char *fb = (unsigned char *)&f;
    unsigned char *db = (unsigned char *)&d;

    printf("92.5 存成 float  (%zu 字节): ", sizeof(f));
    for (size_t i = 0; i < sizeof(f); i++) printf("%02X ", fb[i]);
    printf("\n");
    printf("92.5 存成 double (%zu 字节): ", sizeof(d));
    for (size_t i = 0; i < sizeof(d); i++) printf("%02X ", db[i]);
    printf("\n");
    printf("同一个数、不同长度 -> 搜错了长度，一个都找不到。\n\n");

    printf("=== 3. 扫描到底在做什么 ===\n");
    float hp[8] = { 100.0f, 97.5f, 95.0f, 92.5f, 90.0f, 87.5f, 85.0f, 82.5f };
    unsigned char *raw = (unsigned char *)hp;

    float want = 92.5f;
    unsigned char pat[4];
    memcpy(pat, &want, 4);

    printf("要找的 float %g -> 字节 %02X %02X %02X %02X\n",
           want, pat[0], pat[1], pat[2], pat[3]);
    printf("在 8 个 float 的数组里扫 %zu 个字节……\n", sizeof(hp));

    for (size_t i = 0; i + 4 <= sizeof(hp); i++) {
        if (memcmp(raw + i, pat, 4) == 0) {
            printf("  命中：字节偏移 %zu (0x%zX)，也就是 hp[%zu]\n",
                   i, i, i / sizeof(float));
        }
    }
    printf("偏移 = 下标 x 4，因为每个 float 占 4 个字节。\n");

    return 0;
}
