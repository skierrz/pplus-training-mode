"""Screenshot checks for the tests: a minimal PNG reader (standard library
only) and a test for the debug overlay's box in the top-left corner."""
import struct
import zlib


def read_png(path):
    """(width, height, rows) of an 8-bit RGB or RGBA PNG; rows[y] is a bytes
    object of width * channels values."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")
    pos, idat, width = 8, [], None
    while pos < len(data):
        length, kind = struct.unpack_from(">I4s", data, pos)
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or color not in (2, 6) or interlace:
                raise ValueError(f"{path}: only 8-bit RGB/RGBA, non-interlaced PNGs are supported")
            channels = 3 if color == 2 else 4
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
        pos += 12 + length
    raw = zlib.decompress(b"".join(idat))
    stride = width * channels
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(height):
        kind, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b = prev[x]
            c = prev[x - channels] if x >= channels else 0
            if kind == 1:
                line[x] = (line[x] + a) & 0xFF
            elif kind == 2:
                line[x] = (line[x] + b) & 0xFF
            elif kind == 3:
                line[x] = (line[x] + (a + b) // 2) & 0xFF
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        rows.append(bytes(line))
        prev = line
    return width, height, channels, rows


# The overlay's text area as fractions of the frame: x 20-150, y 20-56 of
# the game's 640x480 2D space, inside the box drawn behind the text.
TEXT_BOX = (20 / 640, 20 / 480, 150 / 640, 56 / 480)


def overlay_score(path):
    """Fraction of near-white pixels in the overlay's text area. The overlay's
    white text on its dark box gives a few percent; the game's own Final
    Destination background there gives none."""
    width, height, channels, rows = read_png(path)
    x0, y0, x1, y1 = (int(TEXT_BOX[0] * width), int(TEXT_BOX[1] * height),
                      int(TEXT_BOX[2] * width), int(TEXT_BOX[3] * height))
    white = total = 0
    for y in range(y0, y1):
        row = rows[y]
        for x in range(x0, x1):
            p = x * channels
            if row[p] > 200 and row[p + 1] > 200 and row[p + 2] > 200:
                white += 1
            total += 1
    return white / total


OVERLAY_MIN_SCORE = 0.03


def overlay_visible(path):
    return overlay_score(path) >= OVERLAY_MIN_SCORE


# The version watermark at the bottom right (src/main/ui/watermark.cpp), as
# fractions of the frame with a margin: x 470-640, y 436-468 of the game's
# 640x480 2D space, for 4:3 and 16:9 alike.
WATERMARK_RECT = (470 / 640, 436 / 480, 1.0, 468 / 480)


def watermark_score(path):
    """Fraction of light text pixels and of dark shadow/background pixels in
    the watermark's rectangle: native single-strike light-slate text
    (0xF1F5F9FF) with a drop shadow gives ~2% bright pixels vs 0% hidden."""
    width, height, channels, rows = read_png(path)
    x0, y0, x1, y1 = (int(WATERMARK_RECT[0] * width), int(WATERMARK_RECT[1] * height),
                      int(WATERMARK_RECT[2] * width), int(WATERMARK_RECT[3] * height))
    white = dark = total = 0
    for y in range(y0, y1):
        row = rows[y]
        for x in range(x0, x1):
            p = x * channels
            lo, hi = min(row[p:p + 3]), max(row[p:p + 3])
            white += lo > 200
            dark += hi < 16
            total += 1
    return white / total, dark / total


WATERMARK_MIN_SCORE = 0.01


def watermark_visible(path):
    white, dark = watermark_score(path)
    return white >= WATERMARK_MIN_SCORE and dark >= WATERMARK_MIN_SCORE


# Where Training's pause menu draws its grey panel and white option boxes,
# as fractions of the frame (Final Destination, Mario vs Fox).
PAUSE_MENU_BOX = (0.12, 0.05, 0.36, 0.85)


def pause_menu_score(path, box=PAUSE_MENU_BOX):
    """Fraction of light, unsaturated pixels where Training's pause menu is
    drawn (or in the part `box` of it): about 0.25-0.30 with the menu on
    screen, under 0.01 without it, and under 0.01 behind the lab menu's
    panel."""
    width, height, channels, rows = read_png(path)
    x0, y0, x1, y1 = (int(box[0] * width), int(box[1] * height), int(box[2] * width), int(box[3] * height))
    light = total = 0
    for y in range(y0, y1):
        row = rows[y]
        for x in range(x0, x1):
            p = x * channels
            lo, hi = min(row[p], row[p + 1], row[p + 2]), max(row[p], row[p + 1], row[p + 2])
            if lo > 180 and hi - lo < 40:
                light += 1
            total += 1
    return light / total


PAUSE_MENU_SHOWN = 0.15
PAUSE_MENU_GONE = 0.05


def pause_menu_behind_panel(xscale):
    """The part of PAUSE_MENU_BOX the lab menu's Draft C rail covers and draws
    no text on (empty right gutter of the rail, x 190-195, strictly clear of
    kRailTextX + kRailTextMaxW = 189.0), as fractions of the frame. On 16:9 the
    panel is narrower than the screen, so Training's menu shows beside it;
    behind it the menu's white boxes must be dimmed."""
    def fx(gx):
        return (320 + (gx - 320) * xscale) / 640
    return (max(PAUSE_MENU_BOX[0], fx(190)), 63 / 480, min(PAUSE_MENU_BOX[2], fx(195)),
            min(PAUSE_MENU_BOX[3], 368 / 480))


# ---- the lab menu's geometry ---------------------------------------------------
#
# Expected positions and colours match src/main/ui/style.h (Draft C Category
# Rail default) in the game's 640x480 2D space. A screenshot is mapped back
# to that space with the game's horizontal squeeze (0.75 on 16:9, 1 on 4:3):
# the plugin keeps every element's 4:3 proportions, centred on x 320.

PANEL = {"left": 36.0, "right": 604.0, "top": 26.0, "bottom": 432.0}
BORDER = (0, 0, 0)
TITLE_TOP, TITLE_BOT, TITLE_Y, TITLE_ALPHA = (26, 57, 70), (14, 33, 42), (29.0, 57.0), 0xF2 / 255.0
RAIL_TOP, RAIL_BOT, RAIL_Y, RAIL_ALPHA = (14, 29, 37), (8, 17, 23), (58.0, 373.0), 0xF5 / 255.0
ROWS_TOP, ROWS_BOT, ROWS_Y, BAND_ALPHA = (16, 32, 41), (9, 19, 25), (58.0, 373.0), 0xEE / 255.0
ROW_BOX_TOP, ROW_BOX_BOT, BOX_ALPHA = (30, 40, 66), (17, 23, 40), 1.0
ROW_BOX_ON_TOP, ROW_BOX_ON_BOT, BOX_ON_ALPHA = (40, 174, 60), (20, 108, 34), 1.0
ROW_BOX, ROW_BOX_ON = (24, 32, 53), (30, 141, 47)
GOLD_TOP, GOLD_BOT, GOLD_ALPHA = (255, 226, 74), (212, 147, 0), 1.0
SCROLL_TRACK = (36, 66, 76)
THUMB, THUMB_ALPHA = (43, 183, 171), 1.0
VISIBLE_ROWS = 11
ROW_TOP, ROW_PITCH, ROW_H = 63.0, 28.0, 23.0
NAME_X, ROW_CAP = 216.0, 10.5
TITLE_X, TITLE_CAP_TOP, TITLE_CAP = 48.0, 36.5, 13.0
HIGHLIGHT_X = (203.0, 581.0)
BOX_X = (410.0, 577.0)
SCROLL_X, SCROLL_W, SCROLL_TOP, SCROLL_H = 586.0, 8.0, 63.0, 303.0
VALUE_CENTRE = 493.5
TEXT_DISABLED = (122, 134, 153)


class Shot:
    """A screenshot read into memory, addressed in the game's 2D space."""

    def __init__(self, path, xscale):
        self.path = path
        self.w, self.h, self.ch, self.rows = read_png(path)
        self.xs = xscale

    def sx(self, gx):
        return int(round((320 + (gx - 320) * self.xs) * self.w / 640))

    def sy(self, gy):
        return int(round(gy * self.h / 480))

    def gx(self, sx):
        return 320 + (sx * 640 / self.w - 320) / self.xs

    def gy(self, sy):
        return sy * 480 / self.h

    def px(self, sx, sy):
        p = sx * self.ch
        r = self.rows[sy]
        return r[p], r[p + 1], r[p + 2]

    def area(self, gx0, gy0, gx1, gy1):
        """Screenshot pixels (sx, sy) inside a game-space rectangle."""
        for y in range(self.sy(gy0), self.sy(gy1)):
            for x in range(self.sx(gx0), self.sx(gx1)):
                yield x, y

    def mean(self, gx0, gy0, gx1, gy1):
        t, n = [0, 0, 0], 0
        for x, y in self.area(gx0, gy0, gx1, gy1):
            p = self.px(x, y)
            for c in range(3):
                t[c] += p[c]
            n += 1
        return tuple(v / max(n, 1) for v in t)


def over(colour, alpha, below):
    return tuple(alpha * c + (1 - alpha) * b for c, b in zip(colour, below))


def black(p, limit=12):
    return max(p) <= limit


def grad_colour(top_col, bot_col, y0, y1, y):
    """Linear vertical gradient colour at game-space `y` between `y0` and `y1`."""
    t = max(0.0, min(1.0, (y - y0) / max(1e-6, y1 - y0)))
    return tuple(top_col[c] + t * (bot_col[c] - top_col[c]) for c in range(3))


def fit_alpha_grad(closed, opened, top_col, bot_col, band_y, rect):
    """Alpha of a vertical-gradient layer (`top_col` at band_y[0] to `bot_col`
    at band_y[1]) drawn over the closed picture inside `rect`, by least
    squares over every pixel and channel, and the rms error."""
    num = den = 0.0
    pairs = []
    for x, y in opened.area(*rect):
        bg, out = closed.px(x, y), opened.px(x, y)
        col = grad_colour(top_col, bot_col, band_y[0], band_y[1], opened.gy(y + 0.5))
        pairs.append((bg, out, col))
        for c in range(3):
            num += (out[c] - bg[c]) * (col[c] - bg[c])
            den += (col[c] - bg[c]) ** 2
    a = num / den if den else 0.0
    err = 0.0
    for bg, out, col in pairs:
        for c in range(3):
            err += (out[c] - (a * col[c] + (1 - a) * bg[c])) ** 2
    return a, (err / max(1, 3 * len(pairs))) ** 0.5


def fit_alpha(closed, opened, colour, rect):
    """Alpha of a layer of `colour` drawn over the closed picture inside
    `rect`, by least squares over every pixel and channel, and the rms error
    of the fitted model."""
    num = den = 0.0
    pairs = []
    for x, y in opened.area(*rect):
        bg, out = closed.px(x, y), opened.px(x, y)
        pairs.append((bg, out))
        for c in range(3):
            num += (out[c] - bg[c]) * (colour[c] - bg[c])
            den += (colour[c] - bg[c]) ** 2
    a = num / den if den else 0.0
    err = 0.0
    for bg, out in pairs:
        for c in range(3):
            err += (out[c] - (a * colour[c] + (1 - a) * bg[c])) ** 2
    return a, (err / max(1, 3 * len(pairs))) ** 0.5


def predicted(closed, rect, layers):
    """Mean colour of `layers` ((colour, alpha), bottom first) drawn over the
    closed picture inside `rect`."""
    t, n = [0.0, 0.0, 0.0], 0
    for x, y in closed.area(*rect):
        p = closed.px(x, y)
        for colour, alpha in layers:
            p = over(colour, alpha, p)
        for c in range(3):
            t[c] += p[c]
        n += 1
    return tuple(v / max(n, 1) for v in t)


def close(a, b, tol):
    return all(abs(x - y) <= tol for x, y in zip(a, b))


def rounded(c):
    return tuple(round(v) for v in c)


def row_rect(row, gx0, gx1, inset=4.0):
    top = ROW_TOP + row * ROW_PITCH
    return (gx0, top + inset, gx1, top + ROW_H - inset)


def box_on(path, row, xscale):
    """Whether the value box of visible menu row `row` is the green On state
    (src/main/ui/style.h kLabPillOn), and its mean (red, green): green well
    above red inside the right margin clear of the text and arrow."""
    s = Shot(path, xscale)
    red, green, _ = s.mean(*row_rect(row, 535, 560))
    return green - red > 45, (round(red), round(green))


def text_box(shot, rect, lo=200):
    """Bounding box (game space) of near-white pixels in `rect`, or None."""
    xs, ys = [], []
    for x, y in shot.area(*rect):
        if min(shot.px(x, y)) >= lo:
            xs.append(x)
            ys.append(y)
    if not xs:
        return None
    return shot.gx(min(xs)), shot.gy(min(ys)), shot.gx(max(xs) + 1), shot.gy(max(ys) + 1)


def dark_text_box(shot, rect, hi=80):
    """Bounding box (game space) of dark-ink pixels in `rect` (for selected row
    labels drawn in dark ink on the metallic gold highlight bar), or None."""
    xs, ys = [], []
    for x, y in shot.area(*rect):
        if max(shot.px(x, y)) <= hi:
            xs.append(x)
            ys.append(y)
    if not xs:
        return None
    return shot.gx(min(xs)), shot.gy(min(ys)), shot.gx(max(xs) + 1), shot.gy(max(ys) + 1)


def is_gold_bar(p):
    """True for pixels belonging to the beveled metallic gold highlight bar."""
    return p[0] >= 115 and p[1] >= 75 and p[0] - p[2] >= 70


# Brawl's match timer (src/main/ui/match_hud.cpp) in the picture's own
# 640x480 space (a Shot with xscale 1), by the game's screen setting: box
# around its near-white digits, measured on Mario against Fox on Final
# Destination.
MATCH_HUD_RECTS = {"16:9": {"timer": (270.0, 38.0, 370.0, 68.0), "counter": (100.0, 48.0, 160.0, 80.0)},
                   "4:3": {"timer": (250.0, 38.0, 385.0, 66.0), "counter": (50.0, 50.0, 100.0, 72.0)}}


def match_hud_pixels(path, aspect, piece, lo=200):
    """Near-white pixels in the place of `piece` ("timer" or "counter"), or
    None for a screen setting not measured."""
    if aspect not in MATCH_HUD_RECTS:
        return None
    s = Shot(path, 1.0)
    return sum(1 for x, y in s.area(*MATCH_HUD_RECTS[aspect][piece]) if min(s.px(x, y)) >= lo)


def brightest(shot, rect):
    """The pixel with the highest minimum channel in `rect`."""
    best = (0, 0, 0)
    for x, y in shot.area(*rect):
        p = shot.px(x, y)
        if min(p) > min(best):
            best = p
    return best


def darkest(shot, rect):
    """The pixel with the lowest maximum channel in `rect`."""
    best = (255, 255, 255)
    for x, y in shot.area(*rect):
        p = shot.px(x, y)
        if max(p) < max(best):
            best = p
    return best


def panel_edges(closed, opened):
    """Outer edges of the panel's border (style::kBorder), found where the
    open picture matches BORDER and the closed one does not."""
    def is_border(p):
        return close(p, BORDER, 16)

    def first(points):
        for x, y in points:
            if is_border(opened.px(x, y)) and not is_border(closed.px(x, y)):
                return x, y
        return None

    lefts, rights, tops, bottoms = [], [], [], []
    for gy in (150.0, 200.0, 260.0, 300.0):
        y = opened.sy(gy)
        p = first((x, y) for x in range(opened.sx(20), opened.sx(55)))
        if p:
            lefts.append(opened.gx(p[0]))
        p = first((x, y) for x in range(opened.sx(620), opened.sx(585), -1))
        if p:
            rights.append(opened.gx(p[0] + 1))
    for gx in (300.0, 330.0, 360.0, 420.0):
        x = opened.sx(gx)
        p = first((x, y) for y in range(opened.sy(14), opened.sy(40)))
        if p:
            tops.append(opened.gy(p[1]))
    for gx in (70.0, 100.0, 130.0, 160.0):
        x = opened.sx(gx)
        p = first((x, y) for y in range(opened.sy(444), opened.sy(420), -1))
        if p:
            bottoms.append(opened.gy(p[1] + 1))

    # Each scan runs from outside the panel inwards and stops at its first
    # hit. If the camera moved between the two pictures, something dark in
    # the background can give a hit before the border is reached, but never
    # after it: the innermost hit of the four scans is the border.
    def inner(v, pick):
        return pick(v) if v else None
    return {"left": inner(lefts, max), "right": inner(rights, min), "top": inner(tops, max),
            "bottom": inner(bottoms, min)}


def check_menu(closed_path, open_path, xscale, cursor_row, count, disabled_rows=(), off_box_row=None,
               on_box_row=None, scroll=0, cursor_box=True, tol_px=2.0, tol_colour=10, name_row=0):
    """Compares an open lab menu with the same frame closed. `cursor_row` is
    the highlighted visible row, `count` the page's rows, `disabled_rows`
    visible rows drawn muted, `off_box_row` / `on_box_row` visible rows with
    an Off / On toggle (the cursor row may be one of them), `scroll` the
    first visible row, `cursor_box` whether the cursor row has a value box
    (submenu rows have none), `name_row` the visible row whose name the
    glyph size is measured on (one without descenders).
    Returns (results, failures): each result is (element, expected,
    measured, ok)."""
    closed, opened = Shot(closed_path, xscale), Shot(open_path, xscale)
    out = []

    def add(name, expected, measured, ok):
        out.append((name, expected, measured, bool(ok)))

    # One screenshot pixel is 1/xscale game units wide.
    tol_x = tol_px + 640.0 / opened.w / xscale
    edges = panel_edges(closed, opened)
    for k in ("left", "right", "top", "bottom"):
        m = edges[k]
        tol = tol_x if k in ("left", "right") else tol_px + 1
        add(f"panel {k}", PANEL[k], None if m is None else round(m, 1), m is not None and abs(m - PANEL[k]) <= tol)
    p = opened.px(opened.sx(320.0), opened.sy(PANEL["top"] + 0.5))
    add("border colour", BORDER, p, close(p, BORDER, tol_colour))

    # Sample header, rail, and right-pane gutters guaranteed free of text and highlights.
    for name, top_c, bot_c, band_y, want_alpha, rect in (
        ("title band", TITLE_TOP, TITLE_BOT, TITLE_Y, TITLE_ALPHA, (280, 32, 460, 52)),
        ("rail band", RAIL_TOP, RAIL_BOT, RAIL_Y, RAIL_ALPHA, (44, 354, 192, 370)),
        ("rows band", ROWS_TOP, ROWS_BOT, ROWS_Y, BAND_ALPHA, (582, 250, 585, 365)),
    ):
        a, rms = fit_alpha_grad(closed, opened, top_c, bot_c, band_y, rect)
        add(f"{name} alpha (rms)", round(want_alpha, 3), f"{a:.3f} ({rms:.1f})",
            abs(a - want_alpha) <= 0.05 and rms <= 8)

    top = ROW_TOP + cursor_row * ROW_PITCH
    gold_mid = grad_colour(GOLD_TOP, GOLD_BOT, top, top + ROW_H, top + ROW_H * 0.5)
    if on_box_row == cursor_row:
        pill_mid = grad_colour(ROW_BOX_ON_TOP, ROW_BOX_ON_BOT, top + 1.0, top + 22.0, top + ROW_H * 0.5)
        box_layer = (pill_mid, BOX_ON_ALPHA)
    else:
        pill_mid = grad_colour(ROW_BOX_TOP, ROW_BOX_BOT, top + 1.0, top + 22.0, top + ROW_H * 0.5)
        box_layer = (pill_mid, BOX_ALPHA)
    rect = row_rect(cursor_row, 535, 560)
    layers = [box_layer] if cursor_box else [(gold_mid, GOLD_ALPHA)]
    exp = predicted(closed, rect, layers)
    got = opened.mean(*rect)
    add("value pill on cursor" if cursor_box else "highlight (row without a box)", rounded(exp), rounded(got),
        close(exp, got, tol_colour))

    # Full-row beveled metallic gold highlight bar sampled in the gutter
    # between row name text and value pill (clear of dark text and pill).
    rect = row_rect(cursor_row, 395, 407)
    exp = predicted(closed, rect, [(gold_mid, GOLD_ALPHA)])
    got = opened.mean(*rect)
    add("highlight gold bar", rounded(exp), rounded(got), close(exp, got, tol_colour))
    gold_sx = opened.sx(401.0)
    ys = [y for y in range(opened.sy(top - 4), opened.sy(top + ROW_H + 4))
          if is_gold_bar(opened.px(gold_sx, y))]
    if ys:
        t, h = opened.gy(ys[0]), opened.gy(ys[-1] + 1) - opened.gy(ys[0])
        add("highlight top, height", (round(top, 1), ROW_H), (round(t, 1), round(h, 1)),
            abs(t - top) <= tol_px and abs(h - ROW_H) <= tol_px)
    else:
        add("highlight top, height", (round(top, 1), ROW_H), None, False)

    for name, row, top_c, bot_c, alpha in (
        ("Off value box", off_box_row, ROW_BOX_TOP, ROW_BOX_BOT, BOX_ALPHA),
        ("On value box", on_box_row, ROW_BOX_ON_TOP, ROW_BOX_ON_BOT, BOX_ON_ALPHA),
    ):
        if row is None:
            continue
        rtop = ROW_TOP + row * ROW_PITCH
        rect = row_rect(row, 535, 560)
        pill_mid = grad_colour(top_c, bot_c, rtop + 2.0, rtop + 21.0, rtop + ROW_H * 0.5)
        exp = predicted(closed, rect, [(pill_mid, alpha)])
        got = opened.mean(*rect)
        add(name + (" (highlighted)" if row == cursor_row else ""), rounded(exp), rounded(got),
            close(exp, got, tol_colour))
        if row != cursor_row:
            yy = rtop + 6
            p_in = grad_colour(top_c, bot_c, rtop + 2.0, rtop + 21.0, yy + 4.0)
            b_out = grad_colour(ROWS_TOP, ROWS_BOT, ROWS_Y[0], ROWS_Y[1], yy + 4.0)
            a = opened.mean(BOX_X[0] + 3, yy, BOX_X[0] + 6, yy + 8)
            b = opened.mean(BOX_X[0] - 7, yy, BOX_X[0] - 3, yy + 8)
            e_in = predicted(closed, (BOX_X[0] + 3, yy, BOX_X[0] + 6, yy + 8), [(p_in, alpha)])
            e_out = predicted(closed, (BOX_X[0] - 7, yy, BOX_X[0] - 3, yy + 8), [(b_out, BAND_ALPHA)])
            add(name + " left edge", BOX_X[0], f"inside {rounded(a)} / {rounded(e_in)}, outside {rounded(b)} / {rounded(e_out)}",
                close(a, e_in, tol_colour) and close(b, e_out, tol_colour))
        tb = text_box(opened, row_rect(row, BOX_X[0] + 12, BOX_X[1] - 12, 2))
        if tb:
            centre = (tb[0] + tb[2]) / 2
            add(name + " text centre", VALUE_CENTRE, round(centre, 1), abs(centre - VALUE_CENTRE) <= 3)

    if count > VISIBLE_ROWS:
        inner = SCROLL_H - 2
        length = max(14.0, inner * VISIBLE_ROWS / count)
        start = SCROLL_TOP + 1 + (inner - length) * scroll / (count - VISIBLE_ROWS)
        rect = (SCROLL_X + 2.0, start + 2, SCROLL_X + SCROLL_W - 2.0, start + length - 2)
        exp = predicted(closed, rect, [(THUMB, THUMB_ALPHA)])
        got = opened.mean(*rect)
        add("scroll thumb colour", rounded(exp), rounded(got), close(exp, got, tol_colour))
        teal = [y for y in range(opened.sy(SCROLL_TOP), opened.sy(SCROLL_TOP + SCROLL_H))
                if (lambda q: q[1] - q[0] > 80)(opened.px(opened.sx(SCROLL_X + SCROLL_W / 2), y))]
        m = (opened.gy(teal[-1] + 1) - opened.gy(teal[0])) if teal else 0
        add("scroll thumb length", round(length, 1), round(m, 1), abs(m - length) <= 3)
        if teal:
            add("scroll thumb top", round(start, 1), round(opened.gy(teal[0]), 1), abs(opened.gy(teal[0]) - start) <= 3)
    for row in range(VISIBLE_ROWS):
        rect = row_rect(row, NAME_X, 404, 3)
        want = row < min(count, VISIBLE_ROWS)
        if row in disabled_rows:
            p = brightest(opened, rect)
            add(f"row {row} name muted", TEXT_DISABLED, p, close(p, TEXT_DISABLED, 25))
        elif want and row == cursor_row:
            p = darkest(opened, rect)
            shown = max(p) < 70
            add(f"row {row} dark text on gold", True, (shown, p), shown)
        elif want:
            p = brightest(opened, rect)
            shown = min(p) >= 100
            add(f"row {row} text shown", want, shown, shown == want)
    if name_row == cursor_row:
        tb = dark_text_box(opened, row_rect(name_row, NAME_X, 404, 2.5))
    else:
        tb = text_box(opened, row_rect(name_row, NAME_X, 404, 0))
    if tb:
        top = ROW_TOP + name_row * ROW_PITCH + (ROW_H - ROW_CAP) * 0.5
        add(f"row {name_row} name glyph top, height", (round(top, 1), ROW_CAP), (round(tb[1], 1), round(tb[3] - tb[1], 1)),
            abs(tb[1] - top) <= 2.5 and abs(tb[3] - tb[1] - ROW_CAP) <= 2.5)
        add(f"row {name_row} name left", NAME_X, round(tb[0], 1), abs(tb[0] - NAME_X) <= 3)
    tb = text_box(opened, (TITLE_X, 30, 300, 56))
    if tb:
        th = tb[3] - tb[1]
        add("title glyph top, height", (TITLE_CAP_TOP, TITLE_CAP), (round(tb[1], 1), round(th, 1)),
            abs(tb[1] - TITLE_CAP_TOP) <= 3 and TITLE_CAP - 3 <= th <= TITLE_CAP + 6)
    return out, [r for r in out if not r[3]]
