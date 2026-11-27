/* crackme01.c —— 第 3–4 周练手靶子：定位并读懂校验逻辑
 *
 * 编译方式见 build.ps1。
 * 目标：不读这个文件，只在 Ghidra / x64dbg 里找出能让程序打印 OK 的那 8 个字符。
 *
 * 提示：程序里有一条字符串是假的，别被它骗了。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEY_LEN 8

/* 这里存的是密文，不是明文 */
static const unsigned char g_enc[KEY_LEN] = {
    0x4D, 0x5A, 0x12, 0x53, 0x5E, 0x5D, 0x0B, 0x0D
};

static int verify(const char *in)
{
    size_t i;

    if (strlen(in) != KEY_LEN) {
        return 0;
    }
    for (i = 0; i < KEY_LEN; i++) {
        if (((unsigned char)in[i] ^ 0x3F) != g_enc[i]) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    char line[64];

    /* 诱饵：只有设置了 CRACKME_DEBUG 才会走到这里 */
    if (getenv("CRACKME_DEBUG") != NULL) {
        printf("expected: %s\n", "ghidra42");
    }

    printf("key> ");
    if (fgets(line, sizeof line, stdin) == NULL) {
        return 1;
    }
    line[strcspn(line, "\r\n")] = '\0';

    if (verify(line)) {
        puts("OK");
        return 0;
    }
    puts("NO");
    return 1;
}
