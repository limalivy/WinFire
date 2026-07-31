"""
render_winfire_icons.py — 从 SVG 母版参数重绘方案A「对称几何火」
输出：
  16/24/32/48/256 各尺寸 PNG（32-bit RGBA，256×256 启用 PNG 压缩）
  winfire.ico — 打包以上 5 个尺寸的 Windows 组合图标

依赖：Pillow、numpy（都已经在本机验证可用）。
坐标体系与 SVG 母版一致：viewBox = 128×128，按渲染尺寸线性缩放。
"""
from __future__ import annotations

import os
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

# ---------- 常量（与 winfire_icon.svg 保持一致） ----------
VIEW_BOX = 128
BG_RECT = (8, 8, 112, 112)          # x, y, w, h in viewBox units
BG_RADIUS = 28                       # corner radius in viewBox units

# 底板对角渐变：(0,0)→(1,1)  红→橙→黄
BG_STOPS = [(0.00, (0xE5, 0x39, 0x35)),
            (0.55, (0xFB, 0x8C, 0x00)),
            (1.00, (0xFD, 0xD8, 0x35))]

# 字形垂直渐变：底部→顶部  深黄→浅黄
GLYPH_STOPS = [(0.0, (0xFF, 0xEB, 0x3B)),
               (1.0, (0xFF, 0xF9, 0xC4))]

# 内焰芯：橙色椭圆  cx,cy,rx,ry  + opacity
INNER = (64, 82, 10, 14)
INNER_RGB = (0xFF, 0x6F, 0x00)
INNER_ALPHA = int(0.85 * 255)

# 外焰路径：按 SVG 中 d= 拆成 12 段三次贝塞尔
# 每段 = (c1x, c1y, c2x, c2y, ex, ey)；起点是上一段终点（首段 = M64,28）
GLYPH_BEZIERS: list[tuple[float, float, float, float, float, float]] = [
    (56, 40,  44, 50,  44, 66),  # C1: (64,28) -> (44,66)
    (44, 74,  48, 80,  48, 80),  # C2: (44,66) -> (48,80)
    (40, 78,  32, 72,  32, 62),  # C3: (48,80) -> (32,62)
    (32, 56,  34, 52,  34, 52),  # C4: (32,62) -> (34,52)
    (24, 66,  22, 82,  32, 92),  # C5: (34,52) -> (32,92)
    (42, 102, 58, 102, 64, 96),  # C6: (32,92) -> (64,96)
    (70, 102, 86, 102, 96, 92),  # C7: (64,96) -> (96,92)
    (106, 82, 104, 66, 94, 52),  # C8: (96,92) -> (94,52)
    (94, 52,  96, 56,  96, 62),  # C9: (94,52) -> (96,62)
    (96, 72,  88, 78,  80, 80),  # C10: (96,62) -> (80,80)
    (80, 80,  84, 74,  84, 66),  # C11: (80,80) -> (84,66)
    (84, 50,  72, 40,  64, 28),  # C12: (84,66) -> (64,28) 闭合
]
GLYPH_START = (64, 28)

TARGET_SIZES = [16, 24, 32, 48, 256]
OUT_DIR = Path(__file__).resolve().parent


# ---------- 几何工具 ----------
def lerp_color(stops: list[tuple[float, tuple[int, int, int]]], t: np.ndarray
               ) -> np.ndarray:
    """多色停线性插值。t 为 [0,1] 浮点数组（任意形状），返回同形状 RGB 数组。"""
    stops = sorted(stops, key=lambda s: s[0])
    out = np.zeros(t.shape + (3,), dtype=np.float64)
    # 对每两个相邻色停，按 mask 覆盖区间
    for i in range(len(stops) - 1):
        t0, c0 = stops[i]
        t1, c1 = stops[i + 1]
        if t1 == t0:
            continue
        mask = (t >= t0) & (t <= t1)
        if i == 0:
            mask |= t < t0      # 首段左侧夹持到 c0
        if i == len(stops) - 2:
            mask |= t > t1      # 末段右侧夹持到 c1
        local = np.clip((t - t0) / (t1 - t0), 0.0, 1.0)
        for ch in range(3):
            ch_arr = c0[ch] + (c1[ch] - c0[ch]) * local
            out[..., ch] = np.where(mask, ch_arr, out[..., ch])
    return np.clip(out, 0, 255).astype(np.uint8)


def cubic_bezier(p0: tuple[float, float],
                 seg: tuple[float, float, float, float, float, float],
                 samples: int = 40) -> list[tuple[float, float]]:
    """de Casteljau 采样一段三次贝塞尔，samples 为段内点数（不含起点，含终点）。"""
    c1x, c1y, c2x, c2y, ex, ey = seg
    pts: list[tuple[float, float]] = []
    for i in range(1, samples + 1):
        t = i / samples
        u = 1 - t
        x = (u*u*u*p0[0] + 3*u*u*t*c1x + 3*u*t*t*c2x + t*t*t*ex)
        y = (u*u*u*p0[1] + 3*u*u*t*c1y + 3*u*t*t*c2y + t*t*t*ey)
        pts.append((x, y))
    return pts


def glyph_polygon(scale: float) -> list[tuple[float, float]]:
    """把 12 段贝塞尔连成闭合多边形，坐标乘以 scale（viewBox → 像素）。"""
    poly: list[tuple[float, float]] = []
    p = (GLYPH_START[0] * scale, GLYPH_START[1] * scale)
    poly.append(p)
    for seg in GLYPH_BEZIERS:
        seg_scaled = tuple(v * scale for v in seg)
        pts = cubic_bezier(p, seg_scaled)
        poly.extend(pts)
        p = poly[-1]
    return poly


def rounded_rect_mask(size: int, rect: tuple[float, float, float, float],
                      radius: float) -> np.ndarray:
    """返回 uint8 alpha 掩膜（255=实，0=透明），size×size 像素。"""
    x, y, w, h = rect
    img = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(img)
    draw.rounded_rectangle([x, y, x + w, y + h], radius=radius, fill=255)
    return np.array(img, dtype=np.uint8)


# ---------- 核心渲染 ----------
def render_at(size: int) -> Image.Image:
    """在超采样倍率下绘制，然后返回 size×size 的 RGBA 图像。"""
    # 为了抗锯齿，实际先以 4× 超采样渲染再降采样
    ss = 4 if size < 128 else 1
    S = size * ss
    scale = S / VIEW_BOX

    # 1) 底板渐变（对角）
    xs = np.linspace(0, 1, S, dtype=np.float64)
    ys = np.linspace(0, 1, S, dtype=np.float64)
    xx, yy = np.meshgrid(xs, ys)
    diag_t = (xx + yy) / 2.0               # 对角方向 0→1
    bg_rgb = lerp_color(BG_STOPS, diag_t)  # (S,S,3) uint8

    # 圆角方掩膜
    bg_rect_scaled = tuple(v * scale for v in BG_RECT)
    bg_r_scaled = BG_RADIUS * scale
    bg_mask = rounded_rect_mask(S, bg_rect_scaled, bg_r_scaled)  # (S,S) uint8

    # 组装 background 层 (S,S,4)
    bg_layer = np.zeros((S, S, 4), dtype=np.uint8)
    bg_layer[..., :3] = bg_rgb
    bg_layer[..., 3] = bg_mask

    # 2) 字形层（垂直渐变 + 火焰多边形掩膜）
    vert_t = np.linspace(0, 1, S, dtype=np.float64)[None, :].repeat(S, axis=0).T  # (S,S)
    glyph_rgb = lerp_color(GLYPH_STOPS, vert_t)
    glyph_mask_img = Image.new("L", (S, S), 0)
    poly = glyph_polygon(scale)
    ImageDraw.Draw(glyph_mask_img).polygon(poly, fill=255)
    glyph_mask = np.array(glyph_mask_img, dtype=np.uint8)
    glyph_layer = np.zeros((S, S, 4), dtype=np.uint8)
    glyph_layer[..., :3] = glyph_rgb
    glyph_layer[..., 3] = glyph_mask

    # 3) 内焰芯（橙色椭圆 + 0.85 alpha）
    inner_scaled = tuple(v * scale for v in INNER)
    cx, cy, rx, ry = inner_scaled
    ell_box = [cx - rx, cy - ry, cx + rx, cy + ry]
    inner_mask_img = Image.new("L", (S, S), 0)
    ImageDraw.Draw(inner_mask_img).ellipse(ell_box, fill=INNER_ALPHA)
    inner_mask = np.array(inner_mask_img, dtype=np.uint8)
    inner_layer = np.zeros((S, S, 4), dtype=np.uint8)
    inner_layer[..., 0] = INNER_RGB[0]
    inner_layer[..., 1] = INNER_RGB[1]
    inner_layer[..., 2] = INNER_RGB[2]
    inner_layer[..., 3] = inner_mask

    # 4) 合成：背景 → 字形 → 内焰（alpha over）
    def over(bg: np.ndarray, fg: np.ndarray) -> np.ndarray:
        bg_f = bg.astype(np.float64) / 255.0
        fg_f = fg.astype(np.float64) / 255.0
        fa = fg_f[..., 3:4]
        ba = bg_f[..., 3:4]
        out_a = fa + ba * (1 - fa)
        out_rgb = np.where(out_a > 0,
                           (fg_f[..., :3] * fa + bg_f[..., :3] * ba * (1 - fa)) / out_a,
                           0.0)
        out = np.zeros_like(bg_f)
        out[..., :3] = out_rgb * 255.0
        out[..., 3] = out_a[..., 0] * 255.0
        return np.clip(out, 0, 255).astype(np.uint8)

    canvas = over(bg_layer, glyph_layer)
    canvas = over(canvas, inner_layer)
    img_ss = Image.fromarray(canvas, mode="RGBA")

    # 降采样到目标尺寸（LANCZOS = 高质量）
    if ss != 1:
        img_ss = img_ss.resize((size, size), Image.LANCZOS)
    return img_ss


# ---------- 主流程 ----------
def main() -> None:
    out_dir = OUT_DIR
    out_dir.mkdir(parents=True, exist_ok=True)

    rendered: dict[int, Image.Image] = {}
    for size in TARGET_SIZES:
        img = render_at(size)
        rendered[size] = img

        name = f"winfire_{size}x{size}.png"
        path = out_dir / name
        # PNG 32-bit RGBA；256×256 用最高压缩，其他快速压缩
        opts = {"format": "PNG"}
        if size == 256:
            opts["optimize"] = True
            opts["compress_level"] = 9
        else:
            opts["compress_level"] = 6
        img.save(path, **opts)
        file_kb = path.stat().st_size / 1024.0
        print(f"✔ {name:24s}  {size:>4}×{size:<4}  RGBA={img.mode}  "
              f"{file_kb:6.1f} KB")

    # 打包 ICO：手写 ICO 二进制头，把每个尺寸的 PNG 原样嵌入。
    # 这样做是为了保留 render_at() 逐档 4× 超采样+LANCZOS 降采样的画质；
    # 若使用 PIL save(ICO, sizes=...)，它会从首张图重采样生成，小尺寸抗锯齿较差。
    ico_sizes_sorted = sorted(rendered.items(), key=lambda kv: kv[0])  # 从小到大
    # 先把每个尺寸编码成 PNG bytes
    import io, struct
    png_blobs: list[tuple[int, bytes]] = []
    for size, img in ico_sizes_sorted:
        buf = io.BytesIO()
        opts = {"optimize": True, "compress_level": 9} if size == 256 else {"compress_level": 6}
        img.save(buf, format="PNG", **opts)
        png_blobs.append((size, buf.getvalue()))

    # ICO 文件布局：
    #   ICONDIR(6B) + n × ICONDIRENTRY(16B) + n × 原始 PNG bytes
    ico_path = out_dir / "winfire.ico"
    n = len(png_blobs)
    header = struct.pack("<HHH", 0, 1, n)    # reserved=0, type=1(.ICO), count=n
    dir_entries = bytearray()
    data_offset = 6 + 16 * n                 # 首图偏移 = 头部 + 目录总长
    blob_bufs: list[bytes] = []
    for size, blob in png_blobs:
        w = 0 if size >= 256 else size       # ICO 规范：256 用 0 表示
        h = 0 if size >= 256 else size
        colors = 0                            # 色彩数：0 = 真彩色不指定
        reserved = 0
        planes = 1                            # color planes: 1
        bpp = 32                              # bits per pixel: RGBA=32
        bytes_sz = len(blob)
        dir_entries += struct.pack("<BBBBHHII",
                                   w, h, colors, reserved, planes, bpp,
                                   bytes_sz, data_offset)
        blob_bufs.append(blob)
        data_offset += bytes_sz

    with open(ico_path, "wb") as f:
        f.write(header)
        f.write(bytes(dir_entries))
        for blob in blob_bufs:
            f.write(blob)

    ico_kb = ico_path.stat().st_size / 1024.0
    print(f"✔ winfire.ico              "
          f"{n} sizes packed  {ico_kb:6.1f} KB")

    print(f"\n输出目录：{out_dir}")


if __name__ == "__main__":
    main()
