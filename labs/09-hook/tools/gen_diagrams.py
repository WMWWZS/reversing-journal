# gen_diagrams.py -- 为 lab09 生成两张图：
#   game-mapping.png : 游戏里的概念 -> 程序里的实体 -> 你要动的字节
#   hook-steps.png   : 改数值 vs 改指令，以及五步补丁流程
from PIL import Image, ImageDraw, ImageFont
import math

W, H = 1560, 940
BG = (252, 252, 250)
INK = (28, 30, 36)
MUTED = (108, 114, 126)
BLUE = (214, 234, 255)
GREEN = (214, 245, 226)
AMBER = (255, 238, 204)
RED = (255, 222, 218)
GREY = (240, 241, 244)

F_B = "C:/Windows/Fonts/msyhbd.ttc"
F_R = "C:/Windows/Fonts/msyh.ttc"
F_M = "C:/Windows/Fonts/consola.ttf"


def f(p, s):
    return ImageFont.truetype(p, s)


def arrow(d, x1, y1, x2, y2, color=(90, 96, 110), w=3):
    d.line([x1, y1, x2, y2], fill=color, width=w)
    a = math.atan2(y2 - y1, x2 - x1)
    L = 13
    d.polygon([(x2, y2),
               (x2 + L * math.cos(a + math.radians(155)), y2 + L * math.sin(a + math.radians(155))),
               (x2 + L * math.cos(a - math.radians(155)), y2 + L * math.sin(a - math.radians(155)))],
              fill=color)


def box(d, x, y, w, h, lines, fill, fonts, colors=None):
    d.rounded_rectangle([x, y, x + w, y + h], radius=12, fill=fill, outline=INK, width=2)
    yy = y + 10
    for i, ln in enumerate(lines):
        fo = fonts[i] if i < len(fonts) else fonts[-1]
        co = colors[i] if colors and i < len(colors) else INK
        d.text((x + 14, yy), ln, font=fo, fill=co)
        yy += fo.size + 6


# ---------------------------------------------------------------- 图 1
img = Image.new("RGB", (W, 900), BG)
d = ImageDraw.Draw(img)
h1, h2, body, mono, small = f(F_B, 38), f(F_B, 24), f(F_R, 20), f(F_M, 18), f(F_R, 17)

d.text((50, 34), "游戏里的一枪，落到内存和代码上是哪几样东西", font=h1, fill=INK)
d.text((50, 86), "左边是你玩的时候看到的东西；右边是它们在进程里的真身；最右是你要读/写的字节", font=small, fill=MUTED)

cols = [(60, 300), (400, 330), (770, 360), (1170, 340)]
heads = ["游戏里你看到的", "进程里的真身", "大小 / 偏移", "你要做的动作"]
for (x, w), t in zip(cols, heads):
    d.text((x + 6, 140), t, font=h2, fill=MUTED)

rows = [
    ("整个游戏世界", "g_world 指针", "模块 .data 里 8 字节", "读 8 字节 → 拿到堆地址", BLUE),
    ("场景里的怪物列表", "World->units 指针", "堆上 +0x00 处 8 字节", "读 8 字节 → 数组首地址", GREEN),
    ("第 k 只怪", "Entity 结构", "每个 0x28 字节，第 k 个 = +k*0x28", "算偏移：units + k*0x28", AMBER),
    ("它的血量", "hp 字段", "实体 +0x10，float 4 字节", "读/写 4 字节", RED),
    ("每帧扣 2.5 血这条规则", "tick() 里的 subss 指令", "代码段 4 字节：F3 0F 5C C1", "改代码：换成 4 个 90", GREY),
]
y = 186
for game, real, size, act, col in rows:
    box(d, cols[0][0], y, cols[0][1], 86, [game], col, [body])
    arrow(d, 372, y + 43, 396, y + 43)
    box(d, cols[1][0], y, cols[1][1], 86, [real], (255, 255, 255), [f(F_R, 19)])
    arrow(d, 736, y + 43, 762, y + 43)
    box(d, cols[2][0], y, cols[2][1], 86, [size], (255, 255, 255), [small])
    arrow(d, 1136, y + 43, 1162, y + 43)
    box(d, cols[3][0], y, cols[3][1], 86, [act], (255, 255, 255), [small])
    y += 104

d.text((60, 742), "前四行是「数据」：改它 → 下一帧被重算 → 只能循环写（hold）。", font=body, fill=MUTED)
d.text((60, 778), "最后一行是「规则」：改它 → 扣血这件事不再发生 → 一次就够，不用循环。", font=body, fill=(180, 40, 30))
d.text((60, 830), "所以 09 的本质：你不是在改血量，你是在改「扣血」这条规则本身。", font=f(F_B, 22), fill=INK)
img.save("game-mapping.png")

# ---------------------------------------------------------------- 图 2
img = Image.new("RGB", (W, H), BG)
d = ImageDraw.Draw(img)
d.text((50, 34), "同一个目的，两种手法：改数值 vs 改规则", font=h1, fill=INK)

# 左：改数值
lx, ly, lw, lh = 60, 130, 660, 330
d.rounded_rectangle([lx, ly, lx + lw, ly + lh], radius=16, outline=(226, 228, 234), width=2, fill=(255, 255, 255))
d.text((lx + 24, ly + 18), "① 改数值（lab03 的 hold）", font=h2, fill=INK)
d.text((lx + 24, ly + 58), "你改的是「血量」这个结果", font=small, fill=MUTED)
box(d, lx + 30, ly + 100, 260, 70, ["目标每帧：hp = hp - 2.5"], (255, 255, 255), [small])
box(d, lx + 340, ly + 100, 280, 70, ["你每 50ms：hp = 9999"], (255, 255, 255), [small])
arrow(d, lx + 300, ly + 135, lx + 336, ly + 135, (180, 40, 30))
arrow(d, lx + 336, ly + 165, lx + 300, ly + 165, (180, 40, 30))
d.text((lx + 30, ly + 195), "互相覆盖，谁后写谁赢 → 你必须一直写", font=body, fill=(180, 40, 30))
d.text((lx + 30, ly + 235), "手停下来的那一刻，血量立刻继续掉", font=small, fill=MUTED)
d.text((lx + 30, ly + 275), "优点：简单、可逆、随时松手", font=small, fill=MUTED)

# 右：改指令
rx = 800
d.rounded_rectangle([rx, ly, rx + 700, ly + lh], radius=16, outline=(226, 228, 234), width=2, fill=(255, 255, 255))
d.text((rx + 24, ly + 18), "② 改规则（lab09 的 hook）", font=h2, fill=INK)
d.text((rx + 24, ly + 58), "你改的是「扣血」这条指令", font=small, fill=MUTED)
box(d, rx + 30, ly + 100, 300, 88, ["原来：F3 0F 5C C1", "subss xmm0, xmm1"], (255, 255, 255), [f(F_R, 19), mono])
box(d, rx + 370, ly + 100, 300, 88, ["补丁：90 90 90 90", "nop nop nop nop"], (214, 245, 226), [f(F_R, 19), mono])
arrow(d, rx + 336, ly + 144, rx + 366, ly + 144, (30, 140, 80))
d.text((rx + 30, ly + 205), "目标还在每帧调用 tick()，但减法不存在了", font=body, fill=(30, 120, 70))
d.text((rx + 30, ly + 245), "它把血量原样存回去 → 血量永远不变", font=small, fill=MUTED)
d.text((rx + 30, ly + 285), "代价：要处理页权限、指令长度、可还原、可被检测", font=small, fill=(180, 40, 30))

# 五步
sy = 500
d.text((60, sy - 40), "改代码的五步（顺序不能换）", font=h2, fill=INK)
steps = [
    ("① 存原字节", "ReadProcessMemory\n拿到 F3 0F 5C C1"),
    ("② 解保护", "VirtualProtectEx\n只读 → 可写"),
    ("③ 写补丁", "WriteProcessMemory\n写 90 90 90 90"),
    ("④ 刷缓存", "FlushInstructionCache\n告诉 CPU 代码变了"),
    ("⑤ 还原权限", "VirtualProtectEx\n可写 → 只读"),
]
bw, gap = 280, 30
for i, (t, s) in enumerate(steps):
    x = 60 + i * (bw + gap)
    d.rounded_rectangle([x, sy, x + bw, sy + 170], radius=14, fill=(255, 255, 255), outline=(210, 214, 222), width=2)
    d.text((x + 16, sy + 14), t, font=f(F_B, 21), fill=INK)
    d.text((x + 16, sy + 52), s, font=small, fill=MUTED, spacing=6)
    if i < 4:
        arrow(d, x + bw + 4, sy + 85, x + bw + gap - 6, sy + 85)

d.text((60, sy + 205), "少任何一步的结果：①没存→不能还原  ②漏了→写入失败(错误5)  ③漏了→什么都没变  ④漏了→可能还在跑旧指令  ⑤漏了→代码段一直可写(检测方一眼看出)",
       font=small, fill=(180, 40, 30))
d.text((60, sy + 250), "判断补丁是否生效，不看 WriteProcessMemory 的返回值，看行为：hp 停没停。", font=body, fill=INK)
img.save("hook-steps.png")
print("saved game-mapping.png hook-steps.png")
