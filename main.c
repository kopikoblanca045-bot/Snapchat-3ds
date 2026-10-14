// FilterCam - camera app for (New) 3DS with filters, stickers and beauty mode.
// Built with devkitARM + libctru.
//
// Controls
//   X ............ next filter            Y ........ switch inner/outer camera
//   A ............ next sticker           B ........ sticker on/off
//   L / R ........ beauty - / +           ZL / ZR .. sticker smaller / bigger (New 3DS)
//   D-Pad / touch  move sticker           SELECT ... save photo (sdmc:/FilterCam/)
//   Circle Pad L/R  face effect (Off/Dog/Cat/Bunny)
//   START ........ exit

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define W 400
#define H 240
#define NPIX (W * H)

enum { F_NONE, F_MONO, F_SEPIA, F_WARM, F_COOL, F_VIVID, F_NEGATIVE, F_POSTER, F_COUNT };
static const char *filterNames[F_COUNT] = {
    "None", "Mono", "Sepia", "Warm", "Cool", "Vivid", "Negative", "Poster"};

enum { S_HEART, S_STAR, S_GLASSES, S_COUNT };
static const char *stickerNames[S_COUNT] = {"Heart", "Star", "Sunglasses"};

static u16 camBuf[NPIX];            // raw RGB565 from camera (row-major, 400x240)
static u8 rgb[NPIX * 3];            // working frame, RGB888
static u8 tmp[NPIX * 3];
static u8 blur[NPIX * 3];

static inline u8 clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

// ---------------------------------------------------------------- camera ----
static u32 maxBytes;
static bool useInner = false;

static Result camStart(bool inner) {
    u32 sel = inner ? SELECT_IN1 : SELECT_OUT1;
    CAMU_SetSize(sel, SIZE_CTR_TOP_LCD, CONTEXT_A);
    CAMU_SetOutputFormat(sel, OUTPUT_RGB_565, CONTEXT_A);
    CAMU_SetFrameRate(sel, FRAME_RATE_30);
    CAMU_SetNoiseFilter(sel, true);
    CAMU_SetAutoExposure(sel, true);
    CAMU_SetAutoWhiteBalance(sel, true);
    CAMU_SetTrimming(PORT_CAM1, false);
    CAMU_GetMaxBytes(&maxBytes, W, H);
    CAMU_SetTransferBytes(PORT_CAM1, maxBytes, W, H);
    CAMU_Activate(sel);
    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2);
    return CAMU_StartCapture(PORT_CAM1);
}

static void camStop(void) {
    CAMU_StopCapture(PORT_CAM1);
    bool busy = false;
    while (R_SUCCEEDED(CAMU_IsBusy(&busy, PORT_CAM1)) && busy) svcSleepThread(1000000);
    CAMU_Activate(SELECT_NONE);
}

static bool camGrab(void) {
    Handle ev = 0;
    if (R_FAILED(CAMU_SetReceiving(&ev, camBuf, PORT_CAM1, sizeof(camBuf), (s16)maxBytes)))
        return false;
    Result r = svcWaitSynchronization(ev, 300000000LL);
    svcCloseHandle(ev);
    return R_SUCCEEDED(r);
}

// ------------------------------------------------------------ image ops -----
static void unpack565(void) {
    for (int i = 0; i < NPIX; i++) {
        u16 p = camBuf[i];
        u8 r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
        rgb[i * 3 + 0] = (r << 3) | (r >> 2);
        rgb[i * 3 + 1] = (g << 2) | (g >> 4);
        rgb[i * 3 + 2] = (b << 3) | (b >> 2);
    }
}

static inline bool isSkin(int r, int g, int b) {
    return r > 95 && g > 40 && b > 20 && r > g && r > b && (r - g) > 12;
}

// Beauty: skin-aware soft blur (5x5 box, separable) + slight brightening.
static void applyBeauty(int level) {  // level 0..5
    if (level <= 0) return;
    // horizontal pass
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int s[3] = {0, 0, 0};
            for (int k = -2; k <= 2; k++) {
                int xx = x + k; xx = xx < 0 ? 0 : (xx >= W ? W - 1 : xx);
                const u8 *p = &rgb[(y * W + xx) * 3];
                s[0] += p[0]; s[1] += p[1]; s[2] += p[2];
            }
            u8 *o = &tmp[(y * W + x) * 3];
            o[0] = s[0] / 5; o[1] = s[1] / 5; o[2] = s[2] / 5;
        }
    }
    // vertical pass
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int s[3] = {0, 0, 0};
            for (int k = -2; k <= 2; k++) {
                int yy = y + k; yy = yy < 0 ? 0 : (yy >= H ? H - 1 : yy);
                const u8 *p = &tmp[(yy * W + x) * 3];
                s[0] += p[0]; s[1] += p[1]; s[2] += p[2];
            }
            u8 *o = &blur[(y * W + x) * 3];
            o[0] = s[0] / 5; o[1] = s[1] / 5; o[2] = s[2] / 5;
        }
    }
    int amt = level * 44;           // blend weight out of 256 (max 220)
    int lift = level * 3;           // brightness
    for (int i = 0; i < NPIX; i++) {
        u8 *p = &rgb[i * 3];
        if (!isSkin(p[0], p[1], p[2])) continue;   // keep eyes/hair/edges sharp
        for (int c = 0; c < 3; c++) {
            int v = p[c] + (((int)blur[i * 3 + c] - p[c]) * amt >> 8) + lift;
            p[c] = clamp8(v);
        }
    }
}

static void applyFilter(int f) {
    if (f == F_NONE) return;
    for (int i = 0; i < NPIX; i++) {
        u8 *p = &rgb[i * 3];
        int r = p[0], g = p[1], b = p[2];
        int l = (r * 77 + g * 151 + b * 28) >> 8;
        switch (f) {
        case F_MONO:  r = g = b = l; break;
        case F_SEPIA: {
            int nr = (r * 100 + g * 197 + b * 48) >> 8;   // ~0.393/0.769/0.189
            int ng = (r * 89 + g * 175 + b * 43) >> 8;
            int nb = (r * 69 + g * 136 + b * 33) >> 8;
            r = nr; g = ng; b = nb; break;
        }
        case F_WARM:  r += 22; g += 6; b -= 20; break;
        case F_COOL:  r -= 18; g += 2; b += 24; break;
        case F_VIVID:
            r = l + ((r - l) * 3 >> 1);
            g = l + ((g - l) * 3 >> 1);
            b = l + ((b - l) * 3 >> 1);
            r = (r - 128) * 9 / 8 + 128; g = (g - 128) * 9 / 8 + 128; b = (b - 128) * 9 / 8 + 128;
            break;
        case F_NEGATIVE: r = 255 - r; g = 255 - g; b = 255 - b; break;
        case F_POSTER:
            r = (r >> 6) * 85; g = (g >> 6) * 85; b = (b >> 6) * 85; break;
        }
        p[0] = clamp8(r); p[1] = clamp8(g); p[2] = clamp8(b);
    }
}

// -------------------------------------------------------------- stickers ----
static inline void put(int x, int y, u8 r, u8 g, u8 b) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    u8 *p = &rgb[(y * W + x) * 3];
    p[0] = r; p[1] = g; p[2] = b;
}

static void drawHeart(int cx, int cy, int s) {
    for (int dy = -s; dy <= s; dy++)
        for (int dx = -s; dx <= s; dx++) {
            float x = (float)dx / s * 1.3f, y = -(float)dy / s * 1.3f;
            float a = x * x + y * y - 1.0f;
            if (a * a * a - x * x * y * y * y < 0) {
                int shade = 40 * dy / s;
                put(cx + dx, cy + dy, clamp8(235 - shade), 30, clamp8(80 - shade));
            }
        }
}

static void drawStar(int cx, int cy, int s) {
    for (int dy = -s; dy <= s; dy++)
        for (int dx = -s; dx <= s; dx++) {
            float ang = atan2f((float)dy, (float)dx);
            float rad = sqrtf((float)(dx * dx + dy * dy)) / s;
            float edge = 0.45f + 0.55f * powf(fabsf(cosf(2.5f * (ang + 1.5708f))), 3.0f);
            if (rad < edge) put(cx + dx, cy + dy, 255, clamp8(200 + (int)(rad * -60)), 30);
        }
}

static void drawGlasses(int cx, int cy, int s) {
    int lensR = s / 2, gap = s / 2 + lensR / 2;
    for (int side = -1; side <= 1; side += 2) {
        int lx = cx + side * gap;
        for (int dy = -lensR; dy <= lensR; dy++)
            for (int dx = -lensR; dx <= lensR; dx++) {
                if (dx * dx + dy * dy <= lensR * lensR) {
                    bool glint = (dx + dy > lensR / 2 - 4) && (dx + dy < lensR / 2 + 2) && dy < 0;
                    if (glint) put(lx + dx, cy + dy, 120, 130, 160);
                    else put(lx + dx, cy + dy, 15, 15, 25);
                }
            }
    }
    for (int x = cx - gap + lensR - 2; x <= cx + gap - lensR + 2; x++)   // bridge
        for (int t = -1; t <= 1; t++) put(x, cy - lensR / 3 + t, 15, 15, 25);
}

static void drawSticker(int type, int x, int y, int s) {
    if (type == S_HEART) drawHeart(x, y, s);
    else if (type == S_STAR) drawStar(x, y, s);
    else drawGlasses(x, y, s);
}

// ---------------------------------------------------------- face effects ----
enum { FX_OFF, FX_DOG, FX_CAT, FX_BUNNY, FX_COUNT };
static const char *fxNames[FX_COUNT] = {"Off", "Dog", "Cat", "Bunny"};

#define CELL 8
#define GW (W / CELL)
#define GH (H / CELL)
static float faceX = W / 2, faceY = H / 2, faceW = 130, faceH = 170;
static int faceLost = 999;   // frames since a face-like region was last found

// No face-detection API exists on 3DS, so we look for the biggest skin-coloured
// blob and estimate head position/size from it. Rough, but works in good light.
static void trackFace(void) {
    static u8 good[GW * GH], seen[GW * GH];
    static short stack[GW * GH];
    for (int gy = 0; gy < GH; gy++)
        for (int gx = 0; gx < GW; gx++) {
            int cnt = 0;
            for (int sy = 0; sy < CELL; sy += 2)
                for (int sx = 0; sx < CELL; sx += 2) {
                    const u8 *p = &rgb[((gy * CELL + sy) * W + gx * CELL + sx) * 3];
                    if (isSkin(p[0], p[1], p[2])) cnt++;
                }
            good[gy * GW + gx] = cnt >= 9;   // out of 16 samples
            seen[gy * GW + gx] = 0;
        }

    int best = 0, bi = -1;
    for (int gy = 2; gy < GH - 2; gy++)
        for (int gx = 2; gx < GW - 2; gx++) {
            if (!good[gy * GW + gx]) continue;
            int sum = 0;
            for (int dy = -2; dy <= 2; dy++)
                for (int dx = -2; dx <= 2; dx++) sum += good[(gy + dy) * GW + gx + dx];
            if (sum > best) { best = sum; bi = gy * GW + gx; }
        }
    if (bi < 0 || best < 10) { faceLost++; return; }

    int rowMin[GH], rowMax[GH];
    for (int r = 0; r < GH; r++) { rowMin[r] = GW; rowMax[r] = -1; }
    int minY = GH, maxY = -1, count = 0, sp = 0;
    stack[sp++] = bi; seen[bi] = 1;
    while (sp) {
        int c = stack[--sp], cx = c % GW, cy = c / GW;
        count++;
        if (cx < rowMin[cy]) rowMin[cy] = cx;
        if (cx > rowMax[cy]) rowMax[cy] = cx;
        if (cy < minY) minY = cy;
        if (cy > maxY) maxY = cy;
        if (cx > 0      && good[c - 1]  && !seen[c - 1])  { seen[c - 1] = 1;  stack[sp++] = c - 1; }
        if (cx < GW - 1 && good[c + 1]  && !seen[c + 1])  { seen[c + 1] = 1;  stack[sp++] = c + 1; }
        if (cy > 0      && good[c - GW] && !seen[c - GW]) { seen[c - GW] = 1; stack[sp++] = c - GW; }
        if (cy < GH - 1 && good[c + GW] && !seen[c + GW]) { seen[c + GW] = 1; stack[sp++] = c + GW; }
    }
    if (count < 12) { faceLost++; return; }

    int rows = maxY - minY + 1;
    int topRows = rows * 2 / 5; if (topRows < 2) topRows = 2; if (topRows > rows) topRows = rows;
    int wCells = 0; float cxSum = 0; int cxN = 0;
    for (int r = minY; r < minY + topRows; r++) {
        if (rowMax[r] < 0) continue;
        int w = rowMax[r] - rowMin[r] + 1;
        if (w > wCells) wCells = w;
        cxSum += (rowMin[r] + rowMax[r] + 1) * 0.5f * CELL; cxN++;
    }
    if (!cxN) { faceLost++; return; }

    float fw = wCells * CELL;
    float fh = fw * 1.3f, bh = rows * CELL;
    if (bh < fh) fh = fmaxf(bh * 1.1f, fw);
    float top = minY * CELL - 0.12f * fh;          // skin starts below the hairline
    float nx = cxSum / cxN, ny = top + fh * 0.5f;

    float k = (faceLost > 15) ? 1.0f : 0.3f;       // snap on first detect, then smooth
    faceX += (nx - faceX) * k; faceY += (ny - faceY) * k;
    faceW += (fw - faceW) * k; faceH += (fh - faceH) * k;
    faceLost = 0;
}

static inline void putA(int x, int y, u8 r, u8 g, u8 b, int a) {   // a: 0..256
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    u8 *p = &rgb[(y * W + x) * 3];
    p[0] = (p[0] * (256 - a) + r * a) >> 8;
    p[1] = (p[1] * (256 - a) + g * a) >> 8;
    p[2] = (p[2] * (256 - a) + b * a) >> 8;
}

static void ellipse(float cx, float cy, float rx, float ry, float ang, u8 r, u8 g, u8 b) {
    if (rx < 1) rx = 1; if (ry < 1) ry = 1;
    float c = cosf(ang), s = sinf(ang), R = (rx > ry ? rx : ry) + 2;
    for (int y = (int)(cy - R); y <= (int)(cy + R) + 1; y++)
        for (int x = (int)(cx - R); x <= (int)(cx + R) + 1; x++) {
            float dx = x - cx, dy = y - cy;
            float u = (dx * c + dy * s) / rx, v = (-dx * s + dy * c) / ry;
            float d = u * u + v * v;
            if (d <= 1.0f) putA(x, y, r, g, b, d > 0.8f ? (int)((1.0f - d) * 5.0f * 256) : 256);
        }
}

static void tri(float x0, float y0, float x1, float y1, float x2, float y2, u8 r, u8 g, u8 b) {
    float minx = fminf(x0, fminf(x1, x2)), maxx = fmaxf(x0, fmaxf(x1, x2));
    float miny = fminf(y0, fminf(y1, y2)), maxy = fmaxf(y0, fmaxf(y1, y2));
    for (int y = (int)miny; y <= (int)maxy + 1; y++)
        for (int x = (int)minx; x <= (int)maxx + 1; x++) {
            float w0 = (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0);
            float w1 = (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1);
            float w2 = (x0 - x2) * (y - y2) - (y0 - y2) * (x - x2);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))
                putA(x, y, r, g, b, 256);
        }
}

static void line(float x0, float y0, float x1, float y1, u8 r, u8 g, u8 b) {
    int n = (int)fmaxf(fabsf(x1 - x0), fabsf(y1 - y0)) + 1;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / n;
        putA((int)(x0 + (x1 - x0) * t), (int)(y0 + (y1 - y0) * t), r, g, b, 256);
    }
}

static void rectFill(float x0, float y0, float x1, float y1, u8 r, u8 g, u8 b) {
    for (int y = (int)y0; y <= (int)y1; y++)
        for (int x = (int)x0; x <= (int)x1; x++) putA(x, y, r, g, b, 256);
}

static void fxDog(int t) {
    float fx = faceX, fy = faceY, fw = faceW, fh = faceH;
    float flop = sinf(t * 0.12f) * 0.04f;
    for (int side = -1; side <= 1; side += 2) {            // floppy ears
        float ex = fx + side * 0.55f * fw, ey = fy - 0.12f * fh, ang = -side * 0.28f + flop;
        ellipse(ex, ey, 0.17f * fw, 0.36f * fh, ang, 95, 58, 32);
        ellipse(ex, ey + 0.03f * fh, 0.10f * fw, 0.26f * fh, ang, 140, 90, 55);
    }
    ellipse(fx, fy + 0.20f * fh, 0.22f * fw, 0.14f * fh, 0, 235, 205, 170);   // snout
    ellipse(fx, fy + 0.12f * fh, 0.09f * fw, 0.06f * fh, 0, 35, 28, 28);      // nose
    ellipse(fx - 0.025f * fw, fy + 0.105f * fh, 0.03f * fw, 0.015f * fh, 0, 130, 130, 140);
    float pant = 0.5f + 0.5f * sinf(t * 0.3f);                                // tongue
    float ty = fy + 0.30f * fh + pant * 0.03f * fh;
    ellipse(fx, ty, 0.07f * fw, 0.07f * fh + pant * 0.05f * fh, 0, 240, 90, 120);
    line(fx, fy + 0.27f * fh, fx, ty + 0.05f * fh, 200, 60, 90);
}

static void fxCat(int t) {
    (void)t;
    float fx = faceX, fy = faceY, fw = faceW, fh = faceH;
    for (int side = -1; side <= 1; side += 2) {
        float ax = fx + side * 0.46f * fw, ay = fy - 0.22f * fh;
        float bx = fx + side * 0.12f * fw, by = fy - 0.48f * fh;
        float tx = fx + side * 0.42f * fw, ty = fy - 0.82f * fh;
        tri(ax, ay, bx, by, tx, ty, 90, 90, 100);
        float cx = (ax + bx + tx) / 3, cy = (ay + by + ty) / 3;
        tri(cx + (ax - cx) * 0.55f, cy + (ay - cy) * 0.55f, cx + (bx - cx) * 0.55f, cy + (by - cy) * 0.55f,
            cx + (tx - cx) * 0.55f, cy + (ty - cy) * 0.55f, 240, 150, 170);
        for (int k = -1; k <= 1; k++) {                                        // whiskers
            float x0 = fx + side * 0.15f * fw, y0 = fy + 0.15f * fh;
            float x1 = fx + side * 0.55f * fw, y1 = fy + 0.12f * fh + k * 0.07f * fh;
            line(x0, y0 + 1, x1, y1 + 1, 20, 20, 20);
            line(x0, y0, x1, y1, 245, 245, 245);
        }
    }
    tri(fx - 0.04f * fw, fy + 0.09f * fh, fx + 0.04f * fw, fy + 0.09f * fh, fx, fy + 0.13f * fh, 240, 120, 150);
    line(fx, fy + 0.13f * fh, fx - 0.05f * fw, fy + 0.17f * fh, 60, 30, 40);
    line(fx, fy + 0.13f * fh, fx + 0.05f * fw, fy + 0.17f * fh, 60, 30, 40);
}

static void fxBunny(int t) {
    float fx = faceX, fy = faceY, fw = faceW, fh = faceH;
    float sway = sinf(t * 0.1f) * 0.05f;
    for (int side = -1; side <= 1; side += 2) {
        float ex = fx + side * 0.22f * fw, ey = fy - 0.95f * fh, ang = side * 0.08f + sway;
        ellipse(ex, ey, 0.12f * fw, 0.42f * fh, ang, 245, 245, 245);
        ellipse(ex, ey + 0.02f * fh, 0.065f * fw, 0.30f * fh, ang, 250, 170, 190);
    }
    ellipse(fx, fy + 0.11f * fh, 0.04f * fw, 0.03f * fh, 0, 250, 130, 160);
    rectFill(fx - 0.045f * fw, fy + 0.19f * fh, fx - 0.004f * fw, fy + 0.27f * fh, 250, 250, 250);
    rectFill(fx + 0.004f * fw, fy + 0.19f * fh, fx + 0.045f * fw, fy + 0.27f * fh, 250, 250, 250);
}

static void drawFx(int fx, int t) {
    if (fx == FX_DOG) fxDog(t);
    else if (fx == FX_CAT) fxCat(t);
    else if (fx == FX_BUNNY) fxBunny(t);
}

// ---------------------------------------------------------------- output ----
static void blitTop(void) {
    // Top screen framebuffer is column-major and rotated: index = x*240 + (239-y)
    u16 *fb = (u16 *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const u8 *p = &rgb[(y * W + x) * 3];
            fb[x * H + (H - 1 - y)] = ((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3);
        }
    }
}

static bool saveBMP(void) {
    mkdir("sdmc:/FilterCam", 0777);
    char path[64];
    snprintf(path, sizeof(path), "sdmc:/FilterCam/IMG_%08lu.bmp", (unsigned long)(osGetTime() / 1000));
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    u32 rowSize = W * 3;                 // 1200 bytes, already multiple of 4
    u32 dataSize = rowSize * H, fileSize = 54 + dataSize;
    u8 hdr[54] = {'B', 'M'};
    memcpy(hdr + 2, &fileSize, 4);
    u32 off = 54, dib = 40, w = W, h = H, comp = 0;
    u16 planes = 1, bpp = 24;
    memcpy(hdr + 10, &off, 4);  memcpy(hdr + 14, &dib, 4);
    memcpy(hdr + 18, &w, 4);    memcpy(hdr + 22, &h, 4);
    memcpy(hdr + 26, &planes, 2); memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 30, &comp, 4); memcpy(hdr + 34, &dataSize, 4);
    fwrite(hdr, 1, 54, f);
    static u8 row[W * 3];
    for (int y = H - 1; y >= 0; y--) {           // BMP is bottom-up, BGR
        for (int x = 0; x < W; x++) {
            const u8 *p = &rgb[(y * W + x) * 3];
            row[x * 3] = p[2]; row[x * 3 + 1] = p[1]; row[x * 3 + 2] = p[0];
        }
        fwrite(row, 1, rowSize, f);
    }
    fclose(f);
    return true;
}

static void drawUI(int filter, int beauty, int stickerOn, int sticker, int size, int fx, bool found, const char *msg) {
    printf("\x1b[1;1H\x1b[2J");
    printf("=== FilterCam ===\n\n");
    printf("Camera : %s\n", useInner ? "Inner (selfie)" : "Outer");
    printf("Filter : %-10s (X)\n", filterNames[filter]);
    printf("Face FX: %-6s %s (C-Pad L/R)\n", fxNames[fx], fx ? (found ? "[face found]" : "[no face]") : "");
    printf("Beauty : %d/5        (L/R)\n", beauty);
    printf("Sticker: %-10s (A/B)\n", stickerOn ? stickerNames[sticker] : "off");
    printf("Size   : %d      (ZL/ZR)\n\n", size);
    printf("D-Pad / touch : move sticker\n");
    printf("Y : switch camera\n");
    printf("SELECT : save photo\n");
    printf("START  : exit\n\n");
    printf("%s\n", msg);
}

// ------------------------------------------------------------------ main ----
int main(void) {
    gfxInitDefault();
    gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
    gfxSetDoubleBuffering(GFX_TOP, true);
    consoleInit(GFX_BOTTOM, NULL);
    aptSetSleepAllowed(false);

    if (R_FAILED(camInit())) {
        printf("Camera init failed.\nPress START to exit.\n");
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysDown() & KEY_START) break;
            gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
        }
        gfxExit();
        return 1;
    }
    camStart(useInner);

    int fx = FX_DOG, frame = 0;
    bool lastFound = false;
    int filter = F_NONE, beauty = 2, sticker = S_HEART, stickerOn = 1, size = 40;
    int sx = W / 2, sy = H / 2;
    char msg[64] = "";
    int msgTimer = 0;
    bool dirty = true;

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown(), held = hidKeysHeld();
        if (down & KEY_START) break;

        if (down & KEY_CPAD_RIGHT) { fx = (fx + 1) % FX_COUNT; dirty = true; }
        if (down & KEY_CPAD_LEFT)  { fx = (fx + FX_COUNT - 1) % FX_COUNT; dirty = true; }
        if (down & KEY_X) { filter = (filter + 1) % F_COUNT; dirty = true; }
        if (down & KEY_A) { sticker = (sticker + 1) % S_COUNT; stickerOn = 1; dirty = true; }
        if (down & KEY_B) { stickerOn = !stickerOn; dirty = true; }
        if ((down & KEY_L) && beauty > 0) { beauty--; dirty = true; }
        if ((down & KEY_R) && beauty < 5) { beauty++; dirty = true; }
        if ((held & KEY_ZL) && size > 12) { size--; dirty = true; }
        if ((held & KEY_ZR) && size < 110) { size++; dirty = true; }
        if (held & KEY_DLEFT)  sx -= 3;
        if (held & KEY_DRIGHT) sx += 3;
        if (held & KEY_DUP)    sy -= 3;
        if (held & KEY_DDOWN)  sy += 3;
        if (held & KEY_TOUCH) {
            touchPosition t; hidTouchRead(&t);
            sx = t.px * W / 320; sy = t.py;
        }
        if (sx < 0) sx = 0; if (sx > W - 1) sx = W - 1;
        if (sy < 0) sy = 0; if (sy > H - 1) sy = H - 1;

        if (down & KEY_Y) {
            camStop();
            useInner = !useInner;
            camStart(useInner);
            dirty = true;
        }

        if (!camGrab()) continue;

        unpack565();
        if (fx) trackFace();
        applyBeauty(beauty);
        applyFilter(filter);
        bool found = faceLost < 12;
        if (fx && found) drawFx(fx, frame);
        if (found != lastFound) { lastFound = found; dirty = true; }
        frame++;
        if (stickerOn) drawSticker(sticker, sx, sy, size);

        if (down & KEY_SELECT) {
            snprintf(msg, sizeof(msg), saveBMP() ? "Saved to sdmc:/FilterCam/" : "Save failed (no SD?)");
            msgTimer = 90; dirty = true;
        }
        if (msgTimer > 0 && --msgTimer == 0) { msg[0] = 0; dirty = true; }

        blitTop();
        if (dirty) { drawUI(filter, beauty, stickerOn, sticker, size, fx, found, msg); dirty = false; }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    camStop();
    camExit();
    aptSetSleepAllowed(true);
    gfxExit();
    return 0;
}
