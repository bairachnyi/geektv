import os
import math
import struct
import numpy as np
from PIL import Image, ImageDraw

OUTPUT_DIR = "space_gifs"
os.makedirs(OUTPUT_DIR, exist_ok=True)
W, H = 240, 240

def lzw_encode_10bit(indices, min_code_size=8, max_dict_code=1000):
    clear_code = 1 << min_code_size
    eoi_code = clear_code + 1
    next_code = eoi_code + 1
    code_size = min_code_size + 1

    out_bytes = bytearray()
    bit_buf = 0
    bit_cnt = 0

    def emit(code, csize):
        nonlocal bit_buf, bit_cnt
        bit_buf |= (code << bit_cnt)
        bit_cnt += csize
        while bit_cnt >= 8:
            out_bytes.append(bit_buf & 0xFF)
            bit_buf >>= 8
            bit_cnt -= 8

    emit(clear_code, code_size)
    table = {}
    if len(indices) == 0:
        emit(eoi_code, code_size)
    else:
        w = bytes([indices[0]])
        for k in indices[1:]:
            wk = w + bytes([k])
            if wk in table:
                w = wk
            else:
                emit(w[0] if len(w) == 1 else table[w], code_size)
                table[wk] = next_code
                next_code += 1
                if next_code > (1 << code_size) and code_size < 12:
                    code_size += 1
                if next_code >= max_dict_code:
                    emit(clear_code, code_size)
                    table.clear()
                    next_code = eoi_code + 1
                    code_size = min_code_size + 1
                w = bytes([k])
        emit(w[0] if len(w) == 1 else table[w], code_size)
        emit(eoi_code, code_size)

    if bit_cnt > 0:
        out_bytes.append(bit_buf & 0xFF)
    return bytes(out_bytes)

def make_palette_gif(frames, out_path, duration=70):
    # Quantize first frame to 64 colors and share palette across all frames
    pal_img = frames[0].convert("RGB").quantize(colors=64, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    raw_pal = pal_img.getpalette()[:256 * 3]
    if len(raw_pal) < 256 * 3:
        raw_pal = raw_pal + [0] * (256 * 3 - len(raw_pal))
    palette_bytes = bytes(raw_pal)

    gif = bytearray()
    # Header + Logical Screen Descriptor (256-color Global Color Table, 8-bit)
    gif.extend(b"GIF89a")
    gif.extend(struct.pack("<HHBBB", W, H, 0xF7, 0, 0))
    gif.extend(palette_bytes)

    # NETSCAPE2.0 infinite loop extension
    gif.extend(b"\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00")

    delay_cs = max(2, duration // 10)
    for frame in frames:
        q = frame.convert("RGB").quantize(palette=pal_img, dither=Image.Dither.NONE)
        indices = q.tobytes()

        # Graphic Control Extension
        gif.extend(struct.pack("<BBBBHB", 0x21, 0xF9, 4, 0x00, delay_cs, 0) + b"\x00")
        # Image Descriptor
        gif.extend(struct.pack("<BHHHHB", 0x2C, 0, 0, W, H, 0x00))
        # LZW Minimum Code Size = 8
        gif.append(8)
        compressed = lzw_encode_10bit(indices, min_code_size=8, max_dict_code=1000)
        for i in range(0, len(compressed), 255):
            chunk = compressed[i:i + 255]
            gif.append(len(chunk))
            gif.extend(chunk)
        gif.append(0)  # block terminator

    gif.append(0x3B)  # GIF trailer
    with open(out_path, "wb") as f:
        f.write(gif)
    sz = os.path.getsize(out_path)
    print(f"Generated {out_path} ({len(frames)} frames, {sz // 1024} KB)")

# -------------------------------------------------------------
# 1. Hyperspace / Warp Starfield (20 frames, seamless loop)
# -------------------------------------------------------------
def generate_warp():
    num_frames = 20
    num_stars = 70
    np.random.seed(42)
    # stars have (angle, base_radius, speed, brightness)
    angles = np.random.uniform(0, 2 * math.pi, num_stars)
    radii_offsets = np.random.uniform(0, 1, num_stars)
    speeds = np.random.uniform(1.2, 2.5, num_stars)
    colors = [
        (255, 255, 255),
        (200, 230, 255),
        (150, 210, 255),
        (240, 240, 255),
        (100, 180, 255)
    ]
    star_colors = [colors[i % len(colors)] for i in range(num_stars)]

    frames = []
    cx, cy = W // 2, H // 2

    for f in range(num_frames):
        img = Image.new("RGB", (W, H), (6, 8, 18))
        draw = ImageDraw.Draw(img)

        # Subtle dark nebula radial gradient
        for r in range(120, 0, -15):
            alpha = int(25 * (1.0 - r / 120.0))
            draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(12 + alpha, 10 + alpha, 28 + alpha))

        t = f / num_frames
        for i in range(num_stars):
            # distance grows exponentially
            r_norm = (radii_offsets[i] + t) % 1.0
            r_dist = (r_norm ** 2.2) * 160.0
            r_prev = (max(0.0, r_norm - 0.08) ** 2.2) * 160.0

            ang = angles[i]
            x1 = cx + math.cos(ang) * r_dist
            y1 = cy + math.sin(ang) * r_dist
            x0 = cx + math.cos(ang) * r_prev
            y0 = cy + math.sin(ang) * r_prev

            if 0 <= x1 < W and 0 <= y1 < H and r_dist > 4:
                col = star_colors[i]
                fade = min(1.0, r_norm * 1.5)
                fcol = (int(col[0] * fade), int(col[1] * fade), int(col[2] * fade))
                width = 2 if r_dist > 50 else 1
                draw.line([(x0, y0), (x1, y1)], fill=fcol, width=width)
                if r_dist > 70:
                    draw.point((x1, y1), fill=(255, 255, 255))

        # Central warp focal point glow
        draw.ellipse([cx - 3, cy - 3, cx + 3, cy + 3], fill=(220, 240, 255))
        draw.point((cx, cy), fill=(255, 255, 255))
        frames.append(img)

    make_palette_gif(frames, os.path.join(OUTPUT_DIR, "01_space_warp.gif"), duration=65)

# -------------------------------------------------------------
# 2. Rotating Pixel Planet with Atmospheric Glow (24 frames)
# -------------------------------------------------------------
def generate_planet():
    num_frames = 24
    frames = []
    cx, cy = 120, 115
    pr = 52 # planet radius

    np.random.seed(101)
    bg_stars = []
    for _ in range(35):
        bx = np.random.randint(4, W - 4)
        by = np.random.randint(4, H - 4)
        if (bx - cx)**2 + (by - cy)**2 > (pr + 12)**2:
            phase = np.random.uniform(0, math.pi * 2)
            bg_stars.append((bx, by, phase))

    for f in range(num_frames):
        img = Image.new("RGB", (W, H), (8, 6, 20))
        draw = ImageDraw.Draw(img)

        # Twinkling background stars
        t_phase = (f / num_frames) * math.pi * 2
        for bx, by, ph in bg_stars:
            twinkle = 0.5 + 0.5 * math.sin(t_phase + ph)
            bcol = int(140 + 115 * twinkle)
            draw.point((bx, by), fill=(bcol, bcol, int(bcol * 1.1) if bcol < 230 else 255))

        # Atmospheric outer glow
        for r in range(pr + 14, pr, -2):
            glow_intensity = int(45 * (1.0 - (r - pr) / 14.0))
            draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(8 + glow_intensity // 2, 18 + glow_intensity, 40 + glow_intensity * 2))

        # Planet base sphere rendering with procedural latitude bands and rotation
        rot = (f / num_frames) * 2 * math.pi
        for y in range(-pr, pr + 1):
            dx_max = int(math.sqrt(max(0, pr * pr - y * y)))
            for x in range(-dx_max, dx_max + 1):
                # spherical projection
                nx = x / pr
                ny = y / pr
                nz = math.sqrt(max(0.0, 1.0 - nx*nx - ny*ny))

                # longitude with rotation
                lon = math.atan2(nx, nz) + rot
                lat = ny

                # banding + continents pattern
                band = math.sin(lat * 7.0 + math.sin(lon * 3.0) * 0.8)
                surface = math.sin(lon * 4.0 + math.cos(lat * 5.0) * 1.2)

                # Shading / lighting from top-left (light dir = [-0.6, -0.4, 0.7])
                light = max(0.0, -0.5 * nx - 0.4 * ny + 0.75 * nz)
                light = light ** 1.3

                if band + surface * 0.4 > 0.15:
                    # Continents / land (emerald / teal)
                    cr, cg, cb = 38, 145, 120
                elif band < -0.4:
                    # Polar caps / ice clouds
                    cr, cg, cb = 190, 230, 245
                else:
                    # Oceans (deep sapphire blue)
                    cr, cg, cb = 24, 70, 160

                # Apply shading
                pix_r = int(cr * light)
                pix_g = int(cg * light)
                pix_b = int(cb * light)

                # Rim light on upper-left edge
                if nz < 0.35 and (-nx - ny) > 0.3:
                    rim = (1.0 - nz / 0.35) * 0.6
                    pix_r = min(255, int(pix_r + 100 * rim))
                    pix_g = min(255, int(pix_g + 180 * rim))
                    pix_b = min(255, int(pix_b + 240 * rim))

                img.putpixel((cx + x, cy + y), (pix_r, pix_g, pix_b))

        # Orbiting moon
        m_angle = (f / num_frames) * 2 * math.pi
        mx = int(cx + math.cos(m_angle) * (pr + 38))
        my = int(cy + math.sin(m_angle) * (pr + 16) * 0.5)
        # Moon shadow/depth
        draw.ellipse([mx - 4, my - 4, mx + 4, my + 4], fill=(180, 195, 210))
        draw.ellipse([mx - 2, my - 2, mx + 2, my + 2], fill=(225, 235, 245))

        frames.append(img)

    make_palette_gif(frames, os.path.join(OUTPUT_DIR, "02_space_planet.gif"), duration=75)

# -------------------------------------------------------------
# 3. Gargantua Black Hole with Relativistic Accretion Disk (20 frames)
# -------------------------------------------------------------
def generate_blackhole():
    num_frames = 20
    frames = []
    cx, cy = 120, 120
    bh_radius = 28

    np.random.seed(77)
    disk_particles = []
    for _ in range(160):
        r = np.random.uniform(bh_radius + 4, 88)
        ang = np.random.uniform(0, 2 * math.pi)
        speed = 1.0 / (r ** 0.5) * 35.0
        color_t = (r - bh_radius) / 60.0 # 0=hot cyan/white, 1=warm orange/red
        disk_particles.append({'r': r, 'ang': ang, 'speed': speed, 'color_t': color_t})

    for f in range(num_frames):
        img = Image.new("RGB", (W, H), (4, 4, 10))
        draw = ImageDraw.Draw(img)

        # Distant background stars
        for i in range(25):
            sx = (i * 47) % W
            sy = (i * 83) % H
            draw.point((sx, sy), fill=(130, 130, 160))

        # Accretion disk behind the black hole (upper arc, bent by gravity)
        for p in disk_particles:
            cur_ang = p['ang'] + (f / num_frames) * p['speed']
            # upper gravitational lensing arc
            if math.sin(cur_ang) < 0:
                rad = p['r']
                lx = cx + math.cos(cur_ang) * rad
                ly = cy + math.sin(cur_ang) * rad * 0.35 - (1.0 - abs(math.cos(cur_ang))) * 22
                ct = p['color_t']
                col = (int(255 - ct * 50), int(180 - ct * 110), int(90 - ct * 60))
                draw.rectangle([lx - 1, ly - 1, lx + 1, ly + 1], fill=col)

        # Photon ring (glowing intense halo right outside horizon)
        for pr_r in range(bh_radius + 5, bh_radius, -1):
            alpha = int(220 * (1.0 - (pr_r - bh_radius) / 5.0))
            draw.ellipse([cx - pr_r, cy - pr_r, cx + pr_r, cy + pr_r], outline=(alpha, int(alpha * 0.85), int(alpha * 0.4)))

        # Event horizon (pure pitch black void)
        draw.ellipse([cx - bh_radius, cy - bh_radius, cx + bh_radius, cy + bh_radius], fill=(0, 0, 0))

        # Main horizontal accretion disk in front of the black hole
        for p in disk_particles:
            cur_ang = p['ang'] + (f / num_frames) * p['speed']
            if math.sin(cur_ang) >= -0.1: # front
                rad = p['r']
                fx = cx + math.cos(cur_ang) * rad
                fy = cy + math.sin(cur_ang) * rad * 0.30
                ct = p['color_t']
                # Relativistic Doppler beaming: approaching side (left) is brighter/whiter
                doppler = 1.0 - math.cos(cur_ang) * 0.35
                col = (
                    min(255, int((255 - ct * 40) * doppler)),
                    min(255, int((210 - ct * 120) * doppler)),
                    min(255, int((120 - ct * 80) * doppler))
                )
                draw.rectangle([fx - 1, fy - 1, fx + 1, fy + 1], fill=col)

        frames.append(img)

    make_palette_gif(frames, os.path.join(OUTPUT_DIR, "03_space_blackhole.gif"), duration=65)

# -------------------------------------------------------------
# 4. Neon Retro Rocket with Animated Plasma Thruster (20 frames)
# -------------------------------------------------------------
def generate_rocket():
    num_frames = 20
    frames = []

    for f in range(num_frames):
        img = Image.new("RGB", (W, H), (8, 10, 24))
        draw = ImageDraw.Draw(img)

        # Starfield moving left-to-right (rocket moving left) or top-to-bottom
        t = f / num_frames
        for s in range(40):
            sx = int((s * 37 + f * 5) % W)
            sy = int((s * 59) % H)
            b = 100 + (s * 33) % 155
            draw.line([(sx, sy), (min(W - 1, sx + 3), sy)], fill=(b, b, 255), width=1)

        # Rocket body floats gently with micro-bobbing
        bob_y = math.sin((f / num_frames) * 2 * math.pi) * 3.5
        rx, ry = 120, int(115 + bob_y)

        # Thruster exhaust flame (dynamic flicker)
        flame_len = 38 + int(8 * math.sin(f * 1.5))
        flame_pts = [
            (rx - 28, ry),
            (rx - 28 - flame_len, ry - 6 + int(3 * math.cos(f))),
            (rx - 28 - flame_len - 10, ry),
            (rx - 28 - flame_len, ry + 6 + int(3 * math.sin(f))),
        ]
        draw.polygon(flame_pts, fill=(255, 110, 30))
        # Inner blue/white core flame
        core_len = flame_len * 0.6
        core_pts = [
            (rx - 28, ry),
            (rx - 28 - core_len, ry - 3),
            (rx - 28 - core_len - 6, ry),
            (rx - 28 - core_len, ry + 3),
        ]
        draw.polygon(core_pts, fill=(160, 230, 255))

        # Rocket Wings / Fins
        draw.polygon([(rx - 15, ry - 10), (rx - 26, ry - 26), (rx - 6, ry - 10)], fill=(220, 45, 65))
        draw.polygon([(rx - 15, ry + 10), (rx - 26, ry + 26), (rx - 6, ry + 10)], fill=(180, 30, 50))

        # Rocket Fuselage (sleek retro body)
        fuselage = [
            (rx + 36, ry), # nose
            (rx + 18, ry - 12),
            (rx - 25, ry - 11),
            (rx - 28, ry),
            (rx - 25, ry + 11),
            (rx + 18, ry + 12),
        ]
        draw.polygon(fuselage, fill=(240, 244, 250))
        # Fuselage shading
        draw.polygon([(rx + 36, ry), (rx + 18, ry + 12), (rx - 25, ry + 11), (rx - 28, ry)], fill=(195, 205, 220))

        # Red nose cone
        draw.polygon([(rx + 36, ry), (rx + 22, ry - 8), (rx + 22, ry + 8)], fill=(235, 50, 70))

        # Cockpit porthole window
        draw.ellipse([rx + 2, ry - 6, rx + 14, ry + 6], fill=(40, 60, 90))
        draw.ellipse([rx + 4, ry - 4, rx + 12, ry + 4], fill=(70, 190, 255))
        draw.point((rx + 6, ry - 2), fill=(255, 255, 255))

        frames.append(img)

    make_palette_gif(frames, os.path.join(OUTPUT_DIR, "04_space_rocket.gif"), duration=60)

# -------------------------------------------------------------
# 5. Floating Astronaut in Zero-G (20 frames)
# -------------------------------------------------------------
def generate_astronaut():
    num_frames = 20
    frames = []

    for f in range(num_frames):
        img = Image.new("RGB", (W, H), (10, 8, 26))
        draw = ImageDraw.Draw(img)

        # Distant purple/pink nebula clouds
        for nx in range(0, W, 20):
            for ny in range(0, H, 20):
                d = math.sin(nx * 0.03 + f * 0.1) * math.cos(ny * 0.03)
                if d > 0.3:
                    draw.rectangle([nx, ny, nx + 20, ny + 20], fill=(16 + int(d * 20), 10, 36 + int(d * 30)))

        # Stars
        for i in range(30):
            sx = (i * 61) % W
            sy = (i * 73) % H
            draw.point((sx, sy), fill=(220, 220, 255))

        # Astronaut floating motion (subtle rotation & float)
        float_y = math.sin((f / num_frames) * 2 * math.pi) * 6.0
        wave_arm = math.sin((f / num_frames) * 2 * math.pi) * 8.0
        ax, ay = 120, int(112 + float_y)

        # Golden safety tether wire with wave physics
        tether_pts = [(0, H)]
        for tx in range(0, ax - 16, 12):
            tw_y = H - (H - ay) * (tx / (ax - 16)) + math.sin(tx * 0.08 + f * 0.4) * 8
            tether_pts.append((tx, int(tw_y)))
        tether_pts.append((ax - 14, ay + 10))
        draw.line(tether_pts, fill=(240, 190, 50), width=2)

        # Life support backpack
        draw.rectangle([ax - 22, ay - 14, ax - 12, ay + 20], fill=(160, 170, 185))
        draw.rectangle([ax - 20, ay - 12, ax - 14, ay + 18], fill=(120, 130, 145))

        # Body suit
        draw.rounded_rectangle([ax - 14, ay - 8, ax + 14, ay + 22], radius=6, fill=(235, 240, 248))
        draw.rectangle([ax - 6, ay + 2, ax + 6, ay + 12], fill=(60, 90, 130)) # chest control box
        draw.point((ax - 2, ay + 5), fill=(255, 60, 60))
        draw.point((ax + 2, ay + 5), fill=(60, 255, 120))

        # Legs
        draw.rounded_rectangle([ax - 12, ay + 20, ax - 2, ay + 42], radius=3, fill=(210, 220, 235))
        draw.rounded_rectangle([ax + 2, ay + 20, ax + 12, ay + 38], radius=3, fill=(190, 200, 215))

        # Helmet
        draw.ellipse([ax - 15, ay - 32, ax + 15, ay - 4], fill=(245, 248, 255))
        # Visor (reflective gold mirror)
        draw.ellipse([ax - 10, ay - 27, ax + 10, ay - 9], fill=(230, 170, 30))
        draw.ellipse([ax - 8, ay - 25, ax + 8, ay - 11], fill=(255, 205, 60))
        draw.line([(ax - 5, ay - 23), (ax + 2, ay - 23)], fill=(255, 255, 255), width=2) # visor glint

        # Left waving arm
        draw.line([(ax + 12, ay - 2), (ax + 24, int(ay - 14 + wave_arm)), (ax + 30, int(ay - 22 + wave_arm))], fill=(235, 240, 248), width=5)
        draw.ellipse([ax + 27, int(ay - 26 + wave_arm), ax + 35, int(ay - 18 + wave_arm)], fill=(180, 40, 50)) # red glove

        frames.append(img)

    make_palette_gif(frames, os.path.join(OUTPUT_DIR, "05_space_astronaut.gif"), duration=70)

if __name__ == '__main__':
    generate_warp()
    generate_planet()
    generate_blackhole()
    generate_rocket()
    generate_astronaut()
    print("All space GIFs successfully generated in space_gifs/")
