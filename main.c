// SerpentIO - jogo de cobra estilo "io" para PSP (homebrew, PSPSDK)
// Controles: analogico = direcao | X ou R = acelerar | X (morto) = reiniciar
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspge.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

PSP_MODULE_INFO("SerpentIO", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define SW 480
#define SH 272
#define BUFW 512
#define FRAME_BYTES (BUFW * SH * 4)

#define WORLD 3000.0f
#define MAXSEG 200
#define NSNAKE 12
#define FOOD_MAX 800
#define FOOD_AMBIENT 450
#define SPACING 6.0f
#define PI 3.14159265f

#define RGB(r, g, b) (0xFF000000u | ((b) << 16) | ((g) << 8) | (r))

typedef struct {
    float x[MAXSEG], y[MAXSEG];
    int len, alive;
    float ang, score;
    unsigned int col;
    int boost, tick;
    float wander;
} Snake;

typedef struct {
    float x, y;
    int active, value;
    unsigned int col;
} Food;

static Snake snakes[NSNAKE]; // snakes[0] = jogador
static Food foods[FOOD_MAX];
static int frameIdx = 0;
static unsigned int *draw;
static int best = 0;

static const unsigned int palette[] = {
    RGB(255, 80, 80), RGB(80, 200, 255), RGB(120, 255, 120), RGB(255, 220, 60),
    RGB(255, 140, 40), RGB(200, 100, 255), RGB(255, 100, 200), RGB(60, 255, 200)};

static float frand(void) { return (float)rand() / (float)RAND_MAX; }
static float radiusOf(const Snake *s) { return 5.0f + s->len / 30.0f; }

// ---------- desenho ----------
static void pixel(int x, int y, unsigned int c) {
    if ((unsigned)x < SW && (unsigned)y < SH) draw[y * BUFW + x] = c;
}

static void fillCircle(int cx, int cy, int r, unsigned int c) {
    if (cx + r < 0 || cx - r >= SW || cy + r < 0 || cy - r >= SH) return;
    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        if ((unsigned)y >= SH) continue;
        int w = (int)sqrtf((float)(r * r - dy * dy));
        int x0 = cx - w, x1 = cx + w;
        if (x0 < 0) x0 = 0;
        if (x1 >= SW) x1 = SW - 1;
        for (int x = x0; x <= x1; x++) draw[y * BUFW + x] = c;
    }
}

static void fillRect(int x, int y, int w, int h, unsigned int c) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) pixel(i, j, c);
}

static const unsigned short digits[10] = { // fonte 3x5
    0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF};

static void drawNumber(int n, int x, int y, int scale, unsigned int c) {
    char buf[12];
    int len = 0;
    if (n <= 0) buf[len++] = 0;
    while (n > 0) { buf[len++] = n % 10; n /= 10; }
    for (int k = len - 1; k >= 0; k--) {
        unsigned short g = digits[(int)buf[k]];
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < 3; col++)
                if (g & (1 << (14 - (row * 3 + col))))
                    fillRect(x + col * scale, y + row * scale, scale, scale, c);
        x += 4 * scale;
    }
}

// ---------- logica ----------
static void spawnFood(Food *f, int value, float x, float y, unsigned int col) {
    f->active = 1;
    f->value = value;
    f->x = x;
    f->y = y;
    f->col = col;
}

static void spawnAmbient(Food *f) {
    spawnFood(f, 1, 20 + frand() * (WORLD - 40), 20 + frand() * (WORLD - 40),
              palette[rand() % 8]);
}

static void dropFood(float x, float y, int value, unsigned int col) {
    for (int i = 0; i < FOOD_MAX; i++)
        if (!foods[i].active) {
            spawnFood(&foods[i], value, x + frand() * 8 - 4, y + frand() * 8 - 4, col);
            return;
        }
}

static void respawn(Snake *s, int isPlayer) {
    float x = 300 + frand() * (WORLD - 600), y = 300 + frand() * (WORLD - 600);
    s->len = 10;
    s->score = 0;
    s->ang = frand() * 2 * PI;
    s->alive = 1;
    s->boost = 0;
    s->tick = 0;
    s->wander = 0;
    s->col = isPlayer ? RGB(90, 255, 140) : palette[rand() % 8];
    for (int i = 0; i < MAXSEG; i++) {
        s->x[i] = x - cosf(s->ang) * i * SPACING;
        s->y[i] = y - sinf(s->ang) * i * SPACING;
    }
}

static void killSnake(Snake *s) {
    s->alive = 0;
    for (int i = 0; i < s->len; i += 2) dropFood(s->x[i], s->y[i], 3, s->col);
}

static void botThink(Snake *s) {
    // vagueia
    s->wander += (frand() - 0.5f) * 0.1f;
    if (s->wander > 0.05f) s->wander = 0.05f;
    if (s->wander < -0.05f) s->wander = -0.05f;
    float target = s->ang + s->wander;

    // procura comida proxima
    float bd = 160.0f * 160.0f;
    for (int i = 0; i < FOOD_MAX; i += 3) {
        if (!foods[i].active) continue;
        float dx = foods[i].x - s->x[0], dy = foods[i].y - s->y[0];
        float d = dx * dx + dy * dy;
        if (d < bd) { bd = d; target = atan2f(dy, dx); }
    }
    // foge de paredes
    float m = 150.0f;
    if (s->x[0] < m || s->x[0] > WORLD - m || s->y[0] < m || s->y[0] > WORLD - m)
        target = atan2f(WORLD / 2 - s->y[0], WORLD / 2 - s->x[0]);
    // desvia de outras cobras
    float fx = s->x[0] + cosf(s->ang) * 45, fy = s->y[0] + sinf(s->ang) * 45;
    for (int k = 0; k < NSNAKE; k++) {
        Snake *o = &snakes[k];
        if (o == s || !o->alive) continue;
        for (int j = 0; j < o->len; j += 3) {
            float dx = o->x[j] - fx, dy = o->y[j] - fy;
            if (dx * dx + dy * dy < 35 * 35) { target = s->ang + 1.5f; goto done; }
        }
    }
done:;
    float diff = target - s->ang;
    while (diff > PI) diff -= 2 * PI;
    while (diff < -PI) diff += 2 * PI;
    if (diff > 0.07f) diff = 0.07f;
    if (diff < -0.07f) diff = -0.07f;
    s->ang += diff;
}

static void updateSnake(Snake *s, int isPlayer) {
    float speed = 1.8f;
    if (s->boost && s->len > 10) {
        speed = 3.4f;
        s->score -= 0.12f;
        if (++s->tick % 12 == 0)
            dropFood(s->x[s->len - 1], s->y[s->len - 1], 1, s->col);
    }
    s->x[0] += cosf(s->ang) * speed;
    s->y[0] += sinf(s->ang) * speed;
    s->len = 10 + (int)(s->score / 3);
    if (s->len > MAXSEG) s->len = MAXSEG;
    if (s->len < 10) s->len = 10;

    for (int i = 1; i < MAXSEG; i++) {
        float dx = s->x[i - 1] - s->x[i], dy = s->y[i - 1] - s->y[i];
        float d = sqrtf(dx * dx + dy * dy);
        if (d > SPACING) {
            s->x[i] += dx / d * (d - SPACING);
            s->y[i] += dy / d * (d - SPACING);
        }
    }
    // comer
    float r = radiusOf(s) + 6;
    for (int i = 0; i < FOOD_MAX; i++) {
        if (!foods[i].active) continue;
        float dx = foods[i].x - s->x[0], dy = foods[i].y - s->y[0];
        if (dx * dx + dy * dy < r * r) {
            s->score += foods[i].value;
            foods[i].active = 0;
        }
    }
    // paredes
    if (s->x[0] < 0 || s->y[0] < 0 || s->x[0] > WORLD || s->y[0] > WORLD) killSnake(s);
    (void)isPlayer;
}

static void collisions(void) {
    for (int a = 0; a < NSNAKE; a++) {
        Snake *s = &snakes[a];
        if (!s->alive) continue;
        for (int b = 0; b < NSNAKE; b++) {
            Snake *o = &snakes[b];
            if (a == b || !o->alive) continue;
            float lim = radiusOf(s) + radiusOf(o) - 2;
            lim *= lim;
            for (int j = 0; j < o->len; j++) {
                float dx = o->x[j] - s->x[0], dy = o->y[j] - s->y[0];
                if (dx * dx + dy * dy < lim) { killSnake(s); goto next; }
            }
        }
    next:;
    }
}

static void render(void) {
    Snake *p = &snakes[0];
    float cx = p->x[0] - SW / 2, cy = p->y[0] - SH / 2;

    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++) {
            float wx = x + cx, wy = y + cy;
            draw[y * BUFW + x] = (wx < 0 || wy < 0 || wx > WORLD || wy > WORLD)
                                     ? RGB(60, 15, 15)
                                     : RGB(18, 20, 30);
        }
    // grade
    int gx0 = ((int)cx / 60) * 60 - 60, gy0 = ((int)cy / 60) * 60 - 60;
    for (int gy = gy0; gy < cy + SH + 60; gy += 60)
        for (int gx = gx0; gx < cx + SW + 60; gx += 60)
            if (gx >= 0 && gy >= 0 && gx <= WORLD && gy <= WORLD)
                fillCircle((int)(gx - cx), (int)(gy - cy), 2, RGB(40, 44, 62));

    for (int i = 0; i < FOOD_MAX; i++)
        if (foods[i].active)
            fillCircle((int)(foods[i].x - cx), (int)(foods[i].y - cy),
                       foods[i].value > 1 ? 4 : 3, foods[i].col);

    for (int k = NSNAKE - 1; k >= 0; k--) {
        Snake *s = &snakes[k];
        if (!s->alive) continue;
        int r = (int)radiusOf(s);
        for (int i = s->len - 1; i >= 0; i--) {
            unsigned int c = s->col;
            if (i % 4 < 2) { // listras
                unsigned int rr = (c & 0xFF) * 3 / 4, gg = ((c >> 8) & 0xFF) * 3 / 4,
                             bb = ((c >> 16) & 0xFF) * 3 / 4;
                c = RGB(rr, gg, bb);
            }
            fillCircle((int)(s->x[i] - cx), (int)(s->y[i] - cy), r, c);
        }
        // olhos
        float ex = -sinf(s->ang) * r * 0.5f, ey = cosf(s->ang) * r * 0.5f;
        float fx = cosf(s->ang) * r * 0.4f, fy = sinf(s->ang) * r * 0.4f;
        int hx = (int)(s->x[0] - cx + fx), hy = (int)(s->y[0] - cy + fy);
        fillCircle(hx + (int)ex, hy + (int)ey, 3, RGB(255, 255, 255));
        fillCircle(hx - (int)ex, hy - (int)ey, 3, RGB(255, 255, 255));
        fillCircle(hx + (int)ex, hy + (int)ey, 1, RGB(0, 0, 0));
        fillCircle(hx - (int)ex, hy - (int)ey, 1, RGB(0, 0, 0));
    }

    // HUD: pontuacao e recorde
    drawNumber((int)p->score, 8, 8, 3, RGB(255, 255, 255));
    drawNumber(best, SW - 70, 8, 2, RGB(255, 220, 60));
    if (!p->alive) {
        for (int y = 100; y < 172; y++)
            for (int x = 120; x < 360; x++) pixel(x, y, RGB(120, 20, 20));
        drawNumber((int)p->score, 200, 118, 8, RGB(255, 255, 255));
    }
}

static int exitCb(int a, int b, void *c) { sceKernelExitGame(); return 0; }
static int cbThread(SceSize args, void *argp) {
    int id = sceKernelCreateCallback("Exit", exitCb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}

int main(void) {
    int th = sceKernelCreateThread("cb", cbThread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    srand(sceKernelGetSystemTimeLow());

    unsigned char *vbase = (unsigned char *)sceGeEdramGetAddr();
    sceDisplaySetMode(0, SW, SH);

    for (int i = 0; i < FOOD_AMBIENT; i++) spawnAmbient(&foods[i]);
    for (int i = 0; i < NSNAKE; i++) respawn(&snakes[i], i == 0);

    SceCtrlData pad;
    for (;;) {
        sceCtrlPeekBufferPositive(&pad, 1);
        Snake *p = &snakes[0];

        if (p->alive) {
            float lx = (pad.Lx - 128) / 128.0f, ly = (pad.Ly - 128) / 128.0f;
            if (pad.Buttons & PSP_CTRL_LEFT) lx = -1;
            if (pad.Buttons & PSP_CTRL_RIGHT) lx = 1;
            if (pad.Buttons & PSP_CTRL_UP) ly = -1;
            if (pad.Buttons & PSP_CTRL_DOWN) ly = 1;
            if (lx * lx + ly * ly > 0.15f) {
                float target = atan2f(ly, lx), diff = target - p->ang;
                while (diff > PI) diff -= 2 * PI;
                while (diff < -PI) diff += 2 * PI;
                if (diff > 0.09f) diff = 0.09f;
                if (diff < -0.09f) diff = -0.09f;
                p->ang += diff;
            }
            p->boost = (pad.Buttons & (PSP_CTRL_CROSS | PSP_CTRL_RTRIGGER)) != 0;
        } else if (pad.Buttons & PSP_CTRL_CROSS) {
            respawn(p, 1);
        }

        for (int i = 0; i < NSNAKE; i++) {
            Snake *s = &snakes[i];
            if (!s->alive) {
                if (i > 0 && rand() % 120 == 0) respawn(s, 0);
                continue;
            }
            if (i > 0) {
                botThink(s);
                s->boost = 0;
            }
            updateSnake(s, i == 0);
        }
        collisions();
        if ((int)p->score > best) best = (int)p->score;

        // mantem comida ambiente
        int active = 0;
        for (int i = 0; i < FOOD_MAX; i++) active += foods[i].active;
        if (active < FOOD_AMBIENT)
            for (int i = 0; i < FOOD_MAX; i++)
                if (!foods[i].active) { spawnAmbient(&foods[i]); break; }

        // double buffer
        int back = 1 - frameIdx;
        draw = (unsigned int *)((unsigned int)(vbase + back * FRAME_BYTES) | 0x40000000);
        render();
        sceDisplayWaitVblankStart();
        sceDisplaySetFrameBuf(vbase + back * FRAME_BYTES, BUFW,
                              PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
        frameIdx = back;
    }
    return 0;
}
