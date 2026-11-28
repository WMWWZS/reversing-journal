# gen_lab_diagrams.py -- 为 02 / 04 / 05 三篇各生成一张概念图
#   find-offset.png : 在几万字节里找到那 4 个字节
#   pointer-chain.png: 从"永远在"的槽位走到"每次都变"的字段
#   aob-scan.png    : 游戏更新后，找指令而不是找变量
from PIL import Image, ImageDraw, ImageFont
import math

F_B = "C:/Windows/Fonts/msyhbd.ttc"
F_R = "C:/Windows/Fonts/msyh.ttc"
F_M = "C:/Windows/Fonts/consola.ttf"
INK = (28, 30, 36)
MUTED = (108, 114, 126)
BLUE = (214, 234, 255)
GREEN = (214, 245, 226)
AMBER = (255, 238, 204)
RED = (255, 222, 218)
GREY = (240, 241, 244)
LINE = (200, 205, 215)


def f(p, s):
    return ImageFont.truetype(p, s)


def arrow(d, x1, y1, x2, y2, color=(90, 96, 110), w=3):
    d.line([x1, y1, x2, y2], fill=color, width=w)
    a = math.atan2(y2 - y1, x2 - x1)
    L = 12
    d.polygon([(x2, y2),
               (x2 + L * math.cos(a + math.radians(155)), y2 + L * math.sin(a + math.radians(155))),
               (x2 + L * math.cos(a - math.radians(155)), y2 + L * math.sin(a - math.radians(155)))],
              fill=color)


def box(d, x, y, w, h, lines, fill=(255, 255, 255), fonts=None, colors=None):
    d.rounded_rectangle([x, y, x + w, y + h], radius=10, fill=fill, outline=INK, width=2)
    yy = y + 8
    for i, ln in enumerate(lines):
        fo = fonts[i]
        co = colors[i] if colors and i < len(colors) else INK
        d.text((x + 12, yy), ln, font=fo, fill=co)
        yy += fo.size + 5


# ======================================================= 02
W, H = 1520, 900
img = Image.new("RGB", (W, 800), (252, 252, 250))
d = ImageDraw.Draw(img)
h1, h2, body, small, mono, tin = f(F_B, 36), f(F_B, 23), f(F_R, 19), f(F_R, 17), f(F_M, 17), f(F_R, 15)

d.text((50, 30), "在几万字节里找到那 4 个字节", font=h1, fill=INK)
d.text((50, 78), "游戏把 92.5 存成 4 个字节 00 00 B9 42；你要做的，是在整块内存里把这段字节找出来", font=small, fill=MUTED)

# 内存条
strip_y = 130
d.text((50, strip_y - 30), "内存（一小段）", font=h2, fill=MUTED)
cells = [
    ("…07030", "00"), ("07031", "00"), ("07040", "01"), ("07041", "00"),
    ("07048", "00"), ("07050", "00"), ("07051", "00"), ("07052", "B9"), ("07053", "42"),
    ("07054", "00"), ("07058", "'b'"), ("07059", "'o'"), ("0705A", "'t'"), ("0705B", "'-'"),
]
x = 50
for addr, val in cells:
    col = AMBER if addr in ("07050", "07051", "07052", "07053") else GREY
    d.rounded_rectangle([x, strip_y, x + 96, strip_y + 74], radius=8, fill=col, outline=LINE, width=1)
    d.text((x + 8, strip_y + 8), addr, font=tin, fill=MUTED)
    d.text((x + 30, strip_y + 38), val, font=mono, fill=INK)
    x += 100

arrow(d, 470, strip_y + 100, 470, strip_y + 128, (180, 40, 30))
box(d, 330, strip_y + 132, 620, 62,
    ["命中！这 4 个格子 = 00 00 B9 42，92.5 的 IEEE754 编码"], AMBER, [body], [(150, 30, 20)])

# 左边 dump
dump_x, dump_y = 50, 400
d.text((dump_x, dump_y - 34), "命中点前后长这样（真实 dump）", font=h2, fill=MUTED)
dump = [
    "07040 | 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 | id = 1",
    "07050 | 00 00 B9 42 00 00 00 00 62 6F 74 2D 31 00 00 00 | hp, team, 'bot-1'",
    "07068 | 02 00 00 00 ...                                    | next entity id = 2",
]
for i, line in enumerate(dump):
    d.text((dump_x, dump_y + i * 34), line, font=mono, fill=INK)

# 右边 5 条证据
ev_x = 820
d.text((ev_x, dump_y - 34), "为什么敢说「就是它」：五条互相印证", font=h2, fill=MUTED)
ev = [
    "① 命中值正好是 92.5 的 4 个字节",
    "② 往回 0x10 处是 01 00 00 00（id = 1）",
    "③ 偏移 0x7050-0x7040 = 0x10，和 offsetof 一致",
    "④ 往后 8 字节是 'bot-1'（name 字段）",
    "⑤ 下一个实体在 +0x28，id 变成 2",
]
for i, e in enumerate(ev):
    d.text((ev_x, dump_y + i * 40), e, font=small, fill=INK)

d.text((50, 716), "一个数字叫猜测；五条互相对得上，才叫证据。", font=f(F_B, 24), fill=(180, 40, 30))
d.text((50, 762), "如果只凭「搜到了」就下结论，你会在别的游戏里被野数据骗到怀疑人生。", font=small, fill=MUTED)
img.save("../../02-find-offset/find-offset.png")

# ======================================================= 04
img = Image.new("RGB", (W, 860), (252, 252, 250))
d = ImageDraw.Draw(img)
d.text((50, 30), "指针链：从「永远在」的槽位，走到「每次都变」的字段", font=h1, fill=INK)
d.text((50, 78), "同一个程序启动两次。看哪些数字不变、哪些数字变了——这就是为什么必须走链", font=small, fill=MUTED)


def chain(y, tag, slot_val, world, units, hp, note):
    d.text((50, y - 34), tag, font=h2, fill=INK)
    box(d, 50, y, 250, 78, ["模块 .data 槽位", "base + 0x7030"], BLUE, [small, mono])
    arrow(d, 306, y + 39, 356, y + 39)
    d.text((312, y - 22), "读 8 字节", font=tin, fill=MUTED)
    box(d, 360, y, 250, 78, ["World（堆）", world], GREEN, [small, mono])
    arrow(d, 616, y + 39, 666, y + 39)
    d.text((622, y - 22), "再 +0x00 读 8", font=tin, fill=MUTED)
    box(d, 670, y, 250, 78, ["units（堆）", units], GREEN, [small, mono])
    arrow(d, 926, y + 39, 976, y + 39)
    d.text((932, y - 22), "加 0x10", font=tin, fill=MUTED)
    box(d, 980, y, 230, 78, ["hp（float）", hp], RED, [small, mono])
    d.text((1240, y + 18), note, font=tin, fill=(180, 40, 30))


chain(150, "第 1 次启动", "0x140007030", "0x7D1910", "0x7D1190", "82.5", "这一轮的堆地址")
chain(320, "第 2 次启动", "0x140007030", "0x681E60", "0x681300", "82.5", "堆地址全变了")

d.rounded_rectangle([50, 440, 1470, 520], radius=12, fill=AMBER, outline=INK, width=2)
d.text((70, 452), "槽位地址（base + 0x7030）两次完全一样 —— 因为它编译进了 exe 的 .data 段，位置固定。", font=body, fill=INK)
d.text((70, 482), "World 和 units 每次都变 —— 它们是游戏启动时 malloc 出来的堆对象。", font=body, fill=INK)

d.text((50, 560), "所以：写死 0x7D1190 只能活一次；写死「base + 0x7030」这条链，能活到游戏更新。", font=f(F_B, 22), fill=(180, 40, 30))
d.text((50, 605), "每一跳只做两件事：读 8 个字节（当地址用）、加一个偏移。三级指针就是重复三次。", font=small, fill=MUTED)

box(d, 50, 660, 690, 120,
    ["真实寻找方法（chain.exe find）：",
     "1. 全进程私有内存里搜 \"bot-1\" → 拿到 units",
     "2. 搜「谁存着 units」→ World；再搜「谁存着 World」→ 槽位",
     "3. 用推出来的链回头解析，必须精确回到同一个地址"], GREY, [small, small, small, small])
box(d, 780, 660, 690, 120,
    ["为什么不能只扫模块映像：",
     "存着 units 的那个格子躺在堆里的 World 里，",
     "模块映像里根本没有这个值。",
     "（第一版就是这么错的，401 个候选全失败）"], RED, [small, small, small, small])
img.save("../../04-pointer-chain/pointer-chain.png")

# ======================================================= 05
img = Image.new("RGB", (W, 860), (252, 252, 250))
d = ImageDraw.Draw(img)
d.text((50, 30), "游戏更新后偏移废了怎么办：找指令，别找变量", font=h1, fill=INK)
d.text((50, 78), "变量会挪位置，但「访问它的那条指令」形状不变——差值藏在 disp32 里", font=small, fill=MUTED)

d.text((50, 130), "v1（旧版）", font=h2, fill=INK)
box(d, 50, 170, 330, 90, ["base + 0x7030", "-> World 0x7BF370  [OK]"], BLUE, [mono, mono])
box(d, 420, 170, 620, 90,
    ["insn @ base+0x1669 : 48 8B 05 BD 59 00 00", "target = 0x14001669 + 7 + 0x59BD = 0x140007030  [OK]"], GREY, [mono, mono])
d.text((1080, 200), "偏移 0x7030 有效", font=body, fill=(30, 120, 70))

d.text((50, 300), "v2（新版：加了几个全局变量）", font=h2, fill=INK)
box(d, 50, 340, 330, 90, ["base + 0x7030", "-> 0x0  [DEAD]"], RED, [mono, mono])
box(d, 420, 340, 620, 90,
    ["insn @ base+0x168C : 48 8B 05 ?? ?? ?? ??", "disp = 0x59DE -> target = 0x140007050  [OK]"], GREEN, [mono, mono])
d.text((1080, 370), "新偏移 0x7050", font=body, fill=(30, 120, 70))

d.rounded_rectangle([50, 460, 1470, 545], radius=12, fill=AMBER, outline=INK, width=2)
d.text((70, 472), "target = insn + 7 + disp32            (7 = length of this instruction)", font=f(F_M, 20), fill=(150, 30, 20))
d.text((70, 506), "指令地址和 disp 会一起变，但「形状 + 公式」不变——所以它比偏移长寿。", font=body, fill=INK)

d.text((50, 580), "扫描会命中一堆同形状的指令：实测 15 条候选，只有 1 条通过验证。", font=body, fill=INK)
box(d, 50, 620, 690, 150,
    ["怎么验证（缺了就是猜）：",
     "把候选解出的地址当成槽位，走一遍链：",
     "槽位 → World → units → 读名字是不是 \"bot-1\"",
     "能走通才算找到。"], GREY, [small, small, small, small])
box(d, 780, 620, 690, 150,
    ["为什么不干脆重新搜血量：",
     "每次更新都要从头搜一遍，且只能拿到一次性的地址。",
     "特征扫描拿到的是静态偏移，写进配置文件就能长期复用。",
     "（这就是 lab08 配置化要接的东西）"], BLUE, [small, small, small, small])
img.save("../../05-aob-scan/aob-scan.png")

print("saved 3 diagrams")
