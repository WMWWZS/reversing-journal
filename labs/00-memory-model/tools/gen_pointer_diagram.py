# gen_pointer_diagram.py -- 画出 "p 装的就是 &hp" 这件事
#
# 输出：pointer-explained.png（四格漫画）
# 用 bundled python 跑：
#   python gen_pointer_diagram.py
from PIL import Image, ImageDraw, ImageFont
import math

W, H = 1500, 1020
BG = (252, 252, 250)
INK = (30, 32, 38)
MUTED = (110, 116, 128)
HP_COL = (214, 234, 255)
MP_COL = (216, 245, 226)
P_COL = (255, 238, 204)
ARROW = (200, 60, 50)

F_TITLE = "C:/Windows/Fonts/msyhbd.ttc"
F_BODY = "C:/Windows/Fonts/msyh.ttc"
F_MONO = "C:/Windows/Fonts/consola.ttf"


def font(path, size):
    return ImageFont.truetype(path, size)


img = Image.new("RGB", (W, H), BG)
d = ImageDraw.Draw(img)

f_h1 = font(F_TITLE, 40)
f_h2 = font(F_TITLE, 26)
f_b = font(F_BODY, 22)
f_s = font(F_BODY, 19)
f_m = font(F_MONO, 22)
f_ms = font(F_MONO, 19)

d.text((60, 40), "指针到底「装」了什么", font=f_h1, fill=INK)
d.text((60, 96), "p = &hp 不是把 p 和 hp 绑在一起，只是把 hp 的格子编号抄进了 p 那 8 个字节",
       font=f_s, fill=MUTED)


def panel(x, y, w, h, title, sub):
    d.rounded_rectangle([x, y, x + w, y + h], radius=18, outline=(226, 228, 234),
                        width=2, fill=(255, 255, 255))
    d.text((x + 24, y + 18), title, font=f_h2, fill=INK)
    d.text((x + 24, y + 56), sub, font=f_s, fill=MUTED)


def cell(x, y, label, addr, value, color, w=190, h=96):
    d.rounded_rectangle([x, y, x + w, y + h], radius=12, fill=color, outline=INK, width=2)
    d.text((x + 14, y + 10), label, font=f_b, fill=INK)
    d.text((x + 14, y + 40), addr, font=f_ms, fill=MUTED)
    # 值里有中文就用中文字体，否则 Consolas（等宽更好看十六进制）
    vf = f_b if any(ord(c) > 127 for c in value) else f_m
    d.text((x + 14, y + 64), value, font=vf, fill=(150, 30, 20))


def arrow(x1, y1, x2, y2, color=ARROW, width=4):
    d.line([x1, y1, x2, y2], fill=color, width=width)
    ang = math.atan2(y2 - y1, x2 - x1)
    L = 16
    a1 = ang + math.radians(155)
    a2 = ang - math.radians(155)
    d.polygon([(x2, y2),
               (x2 + L * math.cos(a1), y2 + L * math.sin(a1)),
               (x2 + L * math.cos(a2), y2 + L * math.sin(a2))], fill=color)


def label(x, y, text, color=MUTED):
    d.text((x, y), text, font=f_s, fill=color)


# ---------------- 面板 1 ----------------
panel(50, 160, 690, 380, "① 两个格子，本来互不相干",
      "hp 装着数；p 是个 8 字节的空位，还没装任何编号")
cell(90, 300, "hp", "@0x1000", "92", HP_COL)
cell(470, 300, "p", "@0x1008", "? 未初始化", P_COL)
label(90, 420, "名字叫 hp 的那一格，编号是 0x1000，里面装 92")
label(90, 452, "名字叫 p 的那一格，编号是 0x1008，占 8 字节（因为要装编号）", ARROW)

# ---------------- 面板 2 ----------------
panel(760, 160, 690, 380, "② p = &hp ：把编号抄进去", "&hp 这个写法的值，就是 0x1000 这个数")
cell(800, 300, "hp", "@0x1000", "92", HP_COL)
cell(1180, 300, "p", "@0x1008", "0x1000", P_COL)
arrow(1174, 348, 996, 348)
label(1010, 292, "这 8 字节里", MUTED)
label(1010, 314, "存的就是编号", MUTED)
label(800, 420, "p 里没有任何「关于 hp 的信息」，只有一个数", ARROW)
label(800, 452, "你可以把它抄给另一个变量，它就指向同一格")

# ---------------- 面板 3 ----------------
panel(50, 570, 690, 380, "③ *p ：顺着编号去那一格", "读 4 字节就是 hp；写 4 字节改的也是 hp")
cell(90, 710, "hp", "@0x1000", "92", HP_COL)
cell(470, 710, "p", "@0x1008", "0x1000", P_COL)
arrow(464, 758, 286, 758)
label(296, 716, "顺着 0x1000 过去", ARROW)
label(90, 836, "*p 和 hp 指的是同一格：一个改，另一个跟着变", ARROW)

# ---------------- 面板 4 ----------------
panel(760, 570, 690, 380, "④ p = &mp 之后：箭头换人",
      "关系不在「指针」里，在那 8 个字节的数值里")
cell(790, 700, "hp", "@0x1000", "92", HP_COL, w=170)
cell(985, 700, "mp", "@0x1004", "50", MP_COL, w=170)
cell(1240, 700, "p", "@0x1008", "0x1004", P_COL, w=170)
arrow(1234, 748, 1162, 748)
label(790, 830, "p 的值从 0x1000 换成了 0x1004，所以它现在指向 mp；", MUTED)
label(790, 862, "hp 一点没受影响 —— 这说明 p 和 hp 之间从来只是一份拷贝。", MUTED)

# 底部结论
d.rounded_rectangle([50, 970, W - 50, 1000], radius=8, fill=(255, 246, 240))
d.text((70, 970), "一句话：赋值 = 把这个编号抄进那 8 个字节；解引用 = 顺着这个编号回去读/写。没有魔法。",
       font=f_s, fill=(150, 30, 20))

img.save("pointer-explained.png")
print("saved pointer-explained.png", img.size)
