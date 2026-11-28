# gen_projection.py -- lab10 概念图：世界坐标怎么变成屏幕像素
from PIL import Image, ImageDraw, ImageFont
import math

F_B = "C:/Windows/Fonts/msyhbd.ttc"
F_R = "C:/Windows/Fonts/msyh.ttc"
F_M = "C:/Windows/Fonts/consola.ttf"
INK = (28, 30, 36)
MUTED = (108, 114, 126)
BLUE = (214, 234, 255)
GREEN = (214, 245, 226)
RED = (255, 222, 218)
AMBER = (255, 238, 204)
GREY = (240, 241, 244)

W, H = 1520, 900
img = Image.new("RGB", (W, H), (252, 252, 250))
d = ImageDraw.Draw(img)


def f(p, s):
    return ImageFont.truetype(p, s)


h1, h2, body, small, mono, tin = f(F_B, 36), f(F_B, 22), f(F_R, 19), f(F_R, 17), f(F_M, 16), f(F_R, 15)

d.text((46, 26), "世界坐标 -> 屏幕像素：一次投影", font=h1, fill=INK)
d.text((46, 74), "敌人在世界里是一个坐标；在屏幕上是一个像素。中间只隔着一个「相机」", font=small, fill=MUTED)

# ---------- 左侧：俯视图 ----------
cx, cy = 340, 480          # 相机位置（像素）
scale = 34.0               # 1 米 = 34 像素
yaw_deg = 27.0

d.text((46, 120), "俯视图（你从天上往下看）", font=h2, fill=INK)

# 视野楔形
half = math.radians(45)
a1 = math.radians(-yaw_deg) - half
a2 = math.radians(-yaw_deg) + half
pts = [(cx, cy)]
for a in (a1, a2):
    pts.append((cx + 420 * math.cos(a), cy + 420 * math.sin(a)))
d.polygon(pts, fill=(235, 245, 255))

# 相机
d.ellipse([cx - 10, cy - 10, cx + 10, cy + 10], fill=INK)
d.text((cx - 26, cy + 14), "相机", font=tin, fill=INK)
# 朝向箭头（屏幕 y 向下，数学 y 向上 -> 取负）
dx = math.cos(math.radians(-yaw_deg)) * 90
dy = math.sin(math.radians(-yaw_deg)) * 90
d.line([cx, cy, cx + dx, cy + dy], fill=(180, 40, 30), width=3)
d.text((cx + dx + 6, cy + dy - 10), "yaw 27°", font=tin, fill=(180, 40, 30))

# 三个敌人：世界坐标 -> 俯视图像素（x 向右，y 向上 -> 屏幕 y 取负）
enemies = [("enemy-1", 8.0, 0.0, GREEN, "966"),
           ("enemy-2", 7.5, 2.7, GREEN, "721"),
           ("enemy-3", 7.5, -2.7, RED, "画面外")]
for name, ex, ey, col, note in enemies:
    px = cx + ex * scale
    py = cy - ey * scale
    d.ellipse([px - 9, py - 9, px + 9, py + 9], fill=col, outline=INK, width=2)
    d.text((px + 12, py - 8), f"{name} ({ex:.1f},{ey:.1f})", font=tin, fill=INK)
    d.text((px + 12, py + 10), f"-> x={note}", font=tin, fill=MUTED)

# ---------- 右侧：屏幕条 ----------
sx0, sy0, sw, sh = 880, 200, 560, 315     # 1280x720 按比例缩小
d.text((880, 150), "屏幕 1280x720（示意）", font=h2, fill=INK)
d.rectangle([sx0, sy0, sx0 + sw, sy0 + sh], fill=(250, 250, 252), outline=INK, width=2)
d.line([sx0 + sw / 2, sy0, sx0 + sw / 2, sy0 + sh], fill=MUTED, width=1)
d.text((sx0 + sw / 2 - 30, sy0 + sh + 6), "中心 640", font=tin, fill=MUTED)

for name, px_target, py_target, col in [("enemy-1", 966, 360, GREEN),
                                        ("enemy-2", 721, 360, GREEN)]:
    tx = sx0 + px_target / 1280 * sw
    ty = sy0 + py_target / 720 * sh
    d.rectangle([tx - 16, ty - 34, tx + 16, ty + 34], outline=(30, 120, 70), width=2)
    d.text((tx - 26, ty - 52), name, font=tin, fill=INK)

d.text((sx0, sy0 + sh + 26), "enemy-3 在视野外：相对角 |−53°−27°|>45°，整框不画", font=tin, fill=(180, 40, 30))

# ---------- 底部公式 ----------
d.rounded_rectangle([46, 700, 1470, 800], radius=12, fill=AMBER, outline=INK, width=2)
d.text((66, 712), "depth = d . forward          lateral = d . right          (d = enemy - camera)", font=mono, fill=INK)
d.text((66, 740), "x = W/2 - (lateral/depth) * (W/2) / tan(fov/2)          y = H/2 - (dz/horiz) * (H/2)",
       font=mono, fill=(150, 30, 20))
d.text((66, 768), "注意负号：yaw 逆时针为正，敌人在右则相对角为负；符号搞反 = 832 像素偏差（这节课真实踩过的坑）。",
       font=small, fill=INK)

d.text((46, 826), "验证方式：同一个目标用「点积法」和「角度法」各算一遍，逐像素必须相等；再用 tan 手算一次绝对值。",
       font=small, fill=MUTED)
img.save("projection.png")
print("saved projection.png")
