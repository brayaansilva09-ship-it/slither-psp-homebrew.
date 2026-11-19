// SerpentIO v2 - jogo de cobra estilo "io" para PSP (homebrew, PSPSDK)
//
// MENU : cima/baixo escolhe | X confirma | esq/dir troca skin
//        em NOME: X edita; cima/baixo troca a letra; esq/dir move o cursor; X termina
// JOGO : analogico (ou direcional) = direcao | X ou R = acelerar | START = menu
// MORTO: X = jogar de novo | TRIANGULO = menu

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspge.h>
#include <psputils.h>
#include <psppower.h>
#include <stdio.h>
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
#define MAXSEG 240
#define NSNAKE 12
#define FOOD_MAX 1000
#define FOOD_AMBIENT 500
#define SPACING 6.0f
#define NAMELEN 10
#define NSKIN 8
#define PI 3.14159265f

#define RGB(r, g, b) (0xFF000000u | ((b) << 16) | ((g) << 8) | (r))

// ------------------------------------------------------------ tipos
typedef struct {
    const char *name;
    int n;
    unsigned int c[6];
    int run;
} Skin;

typedef struct {
    float x[MAXSEG], y[MAXSEG];
    int len, alive, skin, boost, tick, respawnTimer;
    float ang, desired, score, rad, wander, aggr;
    char name[NAMELEN + 1];
} Snake;

typedef struct {
    float x, y;
    int active, value;
    unsigned int col;
} Food;

typedef struct {
    char magic[4];
    char name[NAMELEN + 1];
    int skin;
    int best;
} Save;

enum { ST_MENU, ST_PLAY };

// ------------------------------------------------------------ dados
static const Skin skins[NSKIN] = {
    {"VERDE", 2, {RGB(60, 220, 90), RGB(30, 160, 60)}, 3},
    {"FOGO", 3, {RGB(230, 50, 40), RGB(255, 140, 30), RGB(255, 220, 60)}, 3},
    {"GELO", 2, {RGB(80, 200, 255), RGB(220, 245, 255)}, 3},
    {"ARCO-IRIS", 6, {RGB(255, 70, 70), RGB(255, 160, 40), RGB(250, 230, 60),
                      RGB(70, 220, 90), RGB(70, 140, 255), RGB(180, 90, 255)}, 2},
    {"ZEBRA", 2, {RGB(240, 240, 240), RGB(70, 70, 85)}, 4},
    {"ROXO", 2, {RGB(170, 80, 255), RGB(255, 100, 200)}, 3},
    {"ABELHA", 2, {RGB(250, 210, 40), RGB(50, 50, 50)}, 4},
    {"NEON", 2, {RGB(60, 255, 200), RGB(30, 120, 255)}, 2},
};

static const char *botNames[] = {
    "NOOB", "VIPER", "MAMBA", "COBRA", "PYTHON", "ZIGZAG", "WORMY", "NOODLE",
    "TWISTER", "SHADOW", "BLAZE", "NINJA", "TURBO", "PIXEL", "GHOST", "RAPTOR",
    "COMET", "ZEUS", "LUNA", "BRUNO", "LUCAS", "JOAO", "MIA", "KIKO", "TITAN",
    "ROCKET", "FOXY", "DRAGAO", "BOLT", "SPIKE"};
#define NBOTNAMES 30

static const char CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
#define NCHARS 37

static Snake snakes[NSNAKE]; // snakes[0] = jogador
static Food foods[FOOD_MAX];

static unsigned int *draw;
static int frameIdx = 0;
static int frame = 0;
static int state = ST_MENU;
static int best = 0;

static char playerName[NAMELEN + 1] = "PLAYER    ";
static int skinSel = 0;
static int menuSel = 0, editing = 0, cursorPos = 0;

static float camX = 0, camY = 0, zoom = 1.0f;
#define SX(wx) ((int)(((wx) - camX) * zoom + SW / 2))
#define SY(wy) ((int)(((wy) - camY) * zoom + SH / 2))

static float frand(void) { return (float)rand() / (float)RAND_MAX; }

// ------------------------------------------------------------ desenho
static void fillRect(int x, int y, int w, int h, unsigned int c) {
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > SW) x1 = SW;
    if (y1 > SH) y1 = SH;
    for (int j = y; j < y1; j++) {
        unsigned int *row = draw + j * BUFW;
        for (int i = x; i < x1; i++) row[i] = c;
    }
}

static void fillCircle(int cx, int cy, int r, unsigned int c) {
    if (cx + r < 0 || cx - r >= SW || cy + r < 0 || cy - r >= SH) return;
    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= SH) continue;
        int w = (int)sqrtf((float)(r * r - dy * dy));
        int x0 = cx - w, x1 = cx + w;
        if (x0 < 0) x0 = 0;
        if (x1 >= SW) x1 = SW - 1;
        unsigned int *row = draw + y * BUFW;
        for (int x = x0; x <= x1; x++) row[x] = c;
    }
}

static unsigned int shade(unsigned int c, int num, int den) {
    unsigned int r = (c & 255) * num / den, g = ((c >> 8) & 255) * num / den,
                 b = ((c >> 16) & 255) * num / den;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return RGB(r, g, b);
}

// fonte 3x5 (cada linha = 3 bits)
#define G(a, b, c, d, e) (((a) << 12) | ((b) << 9) | ((c) << 6) | ((d) << 3) | (e))
static const unsigned short fontDigits[10] = {
    G(7, 5, 5, 5, 7), G(2, 6, 2, 2, 7), G(7, 1, 7, 4, 7), G(7, 1, 7, 1, 7), G(5, 5, 7, 1, 1),
    G(7, 4, 7, 1, 7), G(7, 4, 7, 5, 7), G(7, 1, 1, 1, 1), G(7, 5, 7, 5, 7), G(7, 5, 7, 1, 7)};
static const unsigned short fontLetters[26] = {
    G(2, 5, 7, 5, 5), G(6, 5, 6, 5, 6), G(3, 4, 4, 4, 3), G(6, 5, 5, 5, 6), G(7, 4, 6, 4, 7),
    G(7, 4, 6, 4, 4), G(3, 4, 5, 5, 3), G(5, 5, 7, 5, 5), G(7, 2, 2, 2, 7), G(1, 1, 1, 5, 2),
    G(5, 5, 6, 5, 5), G(4, 4, 4, 4, 7), G(5, 7, 7, 5, 5), G(6, 5, 5, 5, 5), G(2, 5, 5, 5, 2),
    G(6, 5, 6, 4, 4), G(2, 5, 5, 6, 3), G(6, 5, 6, 5, 5), G(3, 4, 2, 1, 6), G(7, 2, 2, 2, 2),
    G(5, 5, 5, 5, 7), G(5, 5, 5, 5, 2), G(5, 5, 7, 7, 5), G(5, 5, 2, 5, 5), G(5, 5, 2, 2, 2),
    G(7, 1, 2, 4, 7)};

static unsigned short glyph(char ch) {
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch >= 'A' && ch <= 'Z') return fontLetters[ch - 'A'];
    if (ch >= '0' && ch <= '9') return fontDigits[ch - '0'];
    switch (ch) {
    case '-': return G(0, 0, 7, 0, 0);
    case '<': return G(1, 2, 4, 2, 1);
    case '>': return G(4, 2, 1, 2, 4);
    case ':': return G(0, 2, 0, 2, 0);
    case '.': return G(0, 0, 0, 0, 2);
    case '/': return G(1, 1, 2, 4, 4);
    }
    return 0;
}

static void drawText(const char *t, int x, int y, int sc, unsigned int c) {
    int x0 = x;
    for (int pass = 0; pass < 2; pass++) {
        x = x0;
        for (const char *p = t; *p; p++, x += 4 * sc) {
            unsigned short g = glyph(*p);
            if (!g) continue;
            for (int row = 0; row < 5; row++)
                for (int col = 0; col < 3; col++)
                    if (g & (1 << (14 - (row * 3 + col)))) {
                        if (pass == 0)
                            fillRect(x + col * sc + 1, y + row * sc + 1, sc, sc, 0xFF000000u);
                        else
                            fillRect(x + col * sc, y + row * sc, sc, sc, c);
                    }
        }
    }
}

static void drawTextC(const char *t, int cx, int y, int sc, unsigned int c) {
    drawText(t, cx - (int)strlen(t) * 4 * sc / 2, y, sc, c);
}

// ------------------------------------------------------------ salvar/carregar
static void saveData(void) {
    Save sv;
    memset(&sv, 0, sizeof sv);
    memcpy(sv.magic, "SPT1", 4);
    memcpy(sv.name, playerName, NAMELEN + 1);
    sv.skin = skinSel;
    sv.best = best;
    SceUID fd = sceIoOpen("serpentio.sav", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, &sv, sizeof sv);
        sceIoClose(fd);
    }
}

static void loadData(void) {
    Save sv;
    SceUID fd = sceIoOpen("serpentio.sav", PSP_O_RDONLY, 0777);
    if (fd < 0) return;
    int n = sceIoRead(fd, &sv, sizeof sv);
    sceIoClose(fd);
    if (n != (int)sizeof sv || memcmp(sv.magic, "SPT1", 4) != 0) return;
    for (int i = 0; i < NAMELEN; i++)
        playerName[i] = (sv.name[i] != 0 && strchr(CHARS, sv.name[i])) ? sv.name[i] : ' ';
    playerName[NAMELEN] = 0;
    if (sv.skin >= 0 && sv.skin < NSKIN) skinSel = sv.skin;
    if (sv.best > 0 && sv.best < 1000000) best = sv.best;
}

static void trimName(char *dst) {
    memcpy(dst, playerName, NAMELEN);
    dst[NAMELEN] = 0;
    for (int i = NAMELEN - 1; i >= 0 && dst[i] == ' '; i--) dst[i] = 0;
    if (dst[0] == 0) strcpy(dst, "PLAYER");
}

// ------------------------------------------------------------ jogo
static unsigned int skinColor(int skin, int i) {
    const Skin *k = &skins[skin];
    return k->c[(i / k->run) % k->n];
}

static void updateSize(Snake *s) {
    int l = 10 + (int)(s->score / 3);
    if (l > MAXSEG) l = MAXSEG;
    if (l < 10) l = 10;
    s->len = l;
    float r = 5.0f + sqrtf(s->score > 0 ? s->score : 0) * 0.3f;
    if (r > 15.0f) r = 15.0f;
    s->rad = r;
}

static int displayLen(const Snake *s) { return 10 + (int)s->score; }

static void spawnFood(Food *f, int value, float x, float y, unsigned int col) {
    f->active = 1;
    f->value = value;
    f->x = x;
    f->y = y;
    f->col = col;
}

static void spawnAmbient(Food *f) {
    int v = 1;
    int roll = rand() % 10;
    if (roll == 0) v = 3;
    else if (roll < 3) v = 2;
    static const unsigned int pal[8] = {
        RGB(255, 90, 90), RGB(90, 210, 255), RGB(130, 255, 130), RGB(255, 225, 70),
        RGB(255, 150, 50), RGB(205, 110, 255), RGB(255, 110, 205), RGB(70, 255, 210)};
    spawnFood(f, v, 20 + frand() * (WORLD - 40), 20 + frand() * (WORLD - 40), pal[rand() % 8]);
}

static void dropFood(float x, float y, int value, unsigned int col) {
    for (int i = 0; i < FOOD_MAX; i++)
        if (!foods[i].active) {
            spawnFood(&foods[i], value, x + frand() * 10 - 5, y + frand() * 10 - 5, col);
            return;
        }
}

static void respawn(Snake *s, int isPlayer) {
    float x = WORLD / 2, y = WORLD / 2;
    for (int t = 0; t < 8; t++) {
        x = 250 + frand() * (WORLD - 500);
        y = 250 + frand() * (WORLD - 500);
        int ok = 1;
        for (int k = 0; k < NSNAKE; k++) {
            Snake *o = &snakes[k];
            if (o == s || !o->alive) continue;
            float dx = o->x[0] - x, dy = o->y[0] - y;
            if (dx * dx + dy * dy < 250.0f * 250.0f) { ok = 0; break; }
        }
        if (ok) break;
    }
    s->score = isPlayer ? 0.0f : frand() * 50.0f;
    s->ang = frand() * 2 * PI;
    s->desired = s->ang;
    s->alive = 1;
    s->boost = 0;
    s->tick = 0;
    s->wander = 0;
    s->aggr = 0.7f + frand() * 0.6f;
    s->respawnTimer = 0;
    if (!isPlayer) s->skin = rand() % NSKIN;
    updateSize(s);
    for (int i = 0; i < MAXSEG; i++) {
        s->x[i] = x - cosf(s->ang) * i * SPACING;
        s->y[i] = y - sinf(s->ang) * i * SPACING;
    }
}

static void killSnake(Snake *s) {
    s->alive = 0;
    s->respawnTimer = 180 + rand() % 240;
    for (int i = 0; i < s->len; i += 2)
        dropFood(s->x[i], s->y[i], 3, skinColor(s->skin, i));
}

static float normAngle(float d) {
    while (d > PI) d -= 2 * PI;
    while (d < -PI) d += 2 * PI;
    return d;
}

// 1 se o ponto (px,py) colide com algum corpo (ignora o pescoco da propria cobra)
static int dangerAt(int self, float px, float py, float r) {
    for (int k = 0; k < NSNAKE; k++) {
        Snake *o = &snakes[k];
        if (!o->alive) continue;
        float reach = o->len * SPACING + 60.0f;
        float dx = o->x[0] - px, dy = o->y[0] - py;
        if (dx * dx + dy * dy > reach * reach) continue;
        float lim = r + o->rad + 3.0f;
        lim *= lim;
        int j0 = (k == self) ? 10 : 0;
        for (int j = j0; j < o->len; j += 2) {
            dx = o->x[j] - px;
            dy = o->y[j] - py;
            if (dx * dx + dy * dy < lim) return 1;
        }
    }
    return 0;
}

// IA: escolhe objetivo (fugir > cacar > comida > vagar) e desvia de obstaculos
static void botThink(int idx) {
    Snake *s = &snakes[idx];
    float hx = s->x[0], hy = s->y[0];
    float goal = s->ang;
    int boost = 0;

    float fleeX = 0, fleeY = 0;
    int fleeing = 0, prey = -1;
    float preyD2 = 1e9f;
    for (int k = 0; k < NSNAKE; k++) {
        Snake *o = &snakes[k];
        if (k == idx || !o->alive) continue;
        float dx = o->x[0] - hx, dy = o->y[0] - hy;
        float d2 = dx * dx + dy * dy;
        if (o->score > s->score * 1.3f + 15 && d2 < 170.0f * 170.0f) {
            float inv = 1.0f / (sqrtf(d2) + 1.0f);
            fleeX -= dx * inv;
            fleeY -= dy * inv;
            fleeing = 1;
        } else if (o->score < s->score * (0.55f + 0.25f * s->aggr) && d2 < 380.0f * 380.0f &&
                   d2 < preyD2) {
            preyD2 = d2;
            prey = k;
        }
    }

    if (fleeing) {
        goal = atan2f(fleeY, fleeX);
        if (s->score > 8) boost = 1;
    } else if (prey >= 0) {
        Snake *o = &snakes[prey];
        float d = sqrtf(preyD2);
        float lead = 80.0f + d * 0.35f; // mira na frente da cabeca da presa
        float tx = o->x[0] + cosf(o->ang) * lead, ty = o->y[0] + sinf(o->ang) * lead;
        goal = atan2f(ty - hy, tx - hx);
        if (d < 260 && s->score > 15) boost = 1;
    } else {
        float bestV = 0;
        int found = 0;
        for (int i = idx & 1; i < FOOD_MAX; i += 2) {
            if (!foods[i].active) continue;
            float dx = foods[i].x - hx, dy = foods[i].y - hy;
            float d2 = dx * dx + dy * dy;
            if (d2 > 300.0f * 300.0f) continue;
            float v = foods[i].value / (sqrtf(d2) + 25.0f);
            if (v > bestV) {
                bestV = v;
                goal = atan2f(dy, dx);
                found = 1;
            }
        }
        if (!found) {
            s->wander += (frand() - 0.5f) * 0.12f;
            if (s->wander > 0.06f) s->wander = 0.06f;
            if (s->wander < -0.06f) s->wander = -0.06f;
            goal = s->ang + s->wander * 6.0f;
        }
    }

    float m = 220.0f;
    if (hx < m || hx > WORLD - m || hy < m || hy > WORLD - m) {
        goal = atan2f(WORLD / 2 - hy, WORLD / 2 - hx);
        boost = 0;
    }

    // amostra direcoes e escolhe a mais livre que ainda aponte pro objetivo
    float bestScore = -1e9f, bestA = s->ang;
    for (int k = -3; k <= 3; k++) {
        float a = s->ang + k * 0.4f;
        float ca = cosf(a), sa = sinf(a);
        float sc = cosf(a - goal) * 3.0f - fabsf((float)k) * 0.15f;
        for (int di = 0; di < 3; di++) {
            float d = 28.0f + di * 32.0f;
            float w = (di == 0) ? 3.0f : (di == 1) ? 2.0f : 1.0f;
            float px = hx + ca * d, py = hy + sa * d;
            if (px < 30 || py < 30 || px > WORLD - 30 || py > WORLD - 30) sc -= 6.0f * w;
            if (dangerAt(idx, px, py, s->rad)) sc -= 8.0f * w;
        }
        if (sc > bestScore) {
            bestScore = sc;
            bestA = a;
        }
    }
    s->desired = bestA;
    s->boost = boost;
}

static void updateSnake(Snake *s) {
    float turnMax = 0.11f / (1.0f + s->len * 0.004f);
    float diff = normAngle(s->desired - s->ang);
    if (diff > turnMax) diff = turnMax;
    if (diff < -turnMax) diff = -turnMax;
    s->ang += diff;

    float speed = 1.8f;
    if (s->boost && s->score > 3) {
        speed = 3.4f;
        s->score -= 0.15f;
        if (++s->tick % 12 == 0)
            dropFood(s->x[s->len - 1], s->y[s->len - 1], 1, skinColor(s->skin, s->len - 1));
    }
    s->x[0] += cosf(s->ang) * speed;
    s->y[0] += sinf(s->ang) * speed;
    updateSize(s);

    for (int i = 1; i < MAXSEG; i++) {
        float dx = s->x[i - 1] - s->x[i], dy = s->y[i - 1] - s->y[i];
        float d = sqrtf(dx * dx + dy * dy);
        if (d > SPACING) {
            s->x[i] += dx / d * (d - SPACING);
            s->y[i] += dy / d * (d - SPACING);
        }
    }

    // comer (comida proxima e atraida)
    float eatR = s->rad + 4.0f, pullR = s->rad + 34.0f;
    for (int i = 0; i < FOOD_MAX; i++) {
        Food *f = &foods[i];
        if (!f->active) continue;
        float dx = f->x - s->x[0], dy = f->y - s->y[0];
        float d2 = dx * dx + dy * dy;
        if (d2 > pullR * pullR) continue;
        if (d2 < eatR * eatR) {
            s->score += f->value;
            f->active = 0;
        } else {
            float d = sqrtf(d2);
            f->x -= dx / d * 3.5f;
            f->y -= dy / d * 3.5f;
        }
    }
}

static void collisions(void) {
    int dead[NSNAKE];
    memset(dead, 0, sizeof dead);
    for (int a = 0; a < NSNAKE; a++) {
        Snake *s = &snakes[a];
        if (!s->alive) continue;
        if (s->x[0] < 0 || s->y[0] < 0 || s->x[0] > WORLD || s->y[0] > WORLD) {
            dead[a] = 1;
            continue;
        }
        for (int b = 0; b < NSNAKE && !dead[a]; b++) {
            Snake *o = &snakes[b];
            if (a == b || !o->alive) continue;
            float reach = o->len * SPACING + 60.0f;
            float dx = o->x[0] - s->x[0], dy = o->y[0] - s->y[0];
            if (dx * dx + dy * dy > reach * reach) continue;
            float lim = s->rad + o->rad - 2.0f;
            lim *= lim;
            for (int j = 0; j < o->len; j++) {
                dx = o->x[j] - s->x[0];
                dy = o->y[j] - s->y[0];
                if (dx * dx + dy * dy < lim) {
                    dead[a] = 1;
                    break;
                }
            }
        }
    }
    for (int a = 0; a < NSNAKE; a++)
        if (dead[a]) killSnake(&snakes[a]);
}

static void startGame(void) {
    memset(foods, 0, sizeof foods);
    for (int i = 0; i < FOOD_AMBIENT; i++) spawnAmbient(&foods[i]);
    trimName(snakes[0].name);
    snakes[0].skin = skinSel;
    for (int i = 0; i < NSNAKE; i++) snakes[i].alive = 0;
    respawn(&snakes[0], 1);
    for (int i = 1; i < NSNAKE; i++) respawn(&snakes[i], 0);
    zoom = 1.0f;
    state = ST_PLAY;
}

static void gameUpdate(const SceCtrlData *pad, unsigned int pressed) {
    Snake *p = &snakes[0];
    if (pressed & PSP_CTRL_START) {
        state = ST_MENU;
        return;
    }
    if (p->alive) {
        float lx = (pad->Lx - 128) / 128.0f, ly = (pad->Ly - 128) / 128.0f;
        if (pad->Buttons & PSP_CTRL_LEFT) lx = -1;
        if (pad->Buttons & PSP_CTRL_RIGHT) lx = 1;
        if (pad->Buttons & PSP_CTRL_UP) ly = -1;
        if (pad->Buttons & PSP_CTRL_DOWN) ly = 1;
        if (lx * lx + ly * ly > 0.12f) p->desired = atan2f(ly, lx);
        p->boost = (pad->Buttons & (PSP_CTRL_CROSS | PSP_CTRL_RTRIGGER)) != 0;
    } else {
        if (pressed & PSP_CTRL_CROSS) { startGame(); return; }
        if (pressed & PSP_CTRL_TRIANGLE) { state = ST_MENU; return; }
    }

    int wasAlive = p->alive;
    for (int i = 0; i < NSNAKE; i++) {
        Snake *s = &snakes[i];
        if (!s->alive) {
            if (i > 0 && --s->respawnTimer <= 0) respawn(s, 0);
            continue;
        }
        if (i > 0 && ((frame + i) % 3 == 0)) botThink(i);
        updateSnake(s);
    }
    collisions();

    if (p->alive && displayLen(p) > best) best = displayLen(p);
    if (wasAlive && !p->alive) saveData();

    int active = 0;
    for (int i = 0; i < FOOD_MAX; i++) active += foods[i].active;
    for (int n = 0; active < FOOD_AMBIENT && n < 2; n++)
        for (int i = 0; i < FOOD_MAX; i++)
            if (!foods[i].active) {
                spawnAmbient(&foods[i]);
                active++;
                break;
            }
}

// ------------------------------------------------------------ menu
static void menuUpdate(const SceCtrlData *pad, unsigned int pressed) {
    (void)pad;
    if (editing) {
        if (pressed & PSP_CTRL_LEFT) { if (cursorPos > 0) cursorPos--; }
        if (pressed & PSP_CTRL_RIGHT) { if (cursorPos < NAMELEN - 1) cursorPos++; }
        if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN)) {
            const char *f = strchr(CHARS, playerName[cursorPos]);
            int idx = f ? (int)(f - CHARS) : 0;
            idx += (pressed & PSP_CTRL_UP) ? 1 : NCHARS - 1;
            playerName[cursorPos] = CHARS[idx % NCHARS];
        }
        if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE)) {
            editing = 0;
            saveData();
        }
        return;
    }
    if (pressed & PSP_CTRL_UP) menuSel = (menuSel + 2) % 3;
    if (pressed & PSP_CTRL_DOWN) menuSel = (menuSel + 1) % 3;
    if (menuSel == 2) {
        if (pressed & PSP_CTRL_LEFT) { skinSel = (skinSel + NSKIN - 1) % NSKIN; saveData(); }
        if (pressed & (PSP_CTRL_RIGHT | PSP_CTRL_CROSS)) { skinSel = (skinSel + 1) % NSKIN; saveData(); }
    }
    if (pressed & PSP_CTRL_CROSS) {
        if (menuSel == 0) startGame();
        else if (menuSel == 1) editing = 1;
    }
}

static void renderMenu(void) {
    fillRect(0, 0, SW, SH, RGB(14, 16, 26));
    for (int i = 0; i < 50; i++) {
        int x = (i * 137 + frame / (2 + i % 3)) % SW, y = (i * 71) % SH;
        unsigned int c = (i % 2) ? RGB(40, 46, 70) : RGB(60, 80, 70);
        fillCircle(x, y, 2 + (i % 3), c);
    }
    drawTextC("SERPENTIO", SW / 2, 16, 6, RGB(90, 255, 140));

    unsigned int sel = RGB(255, 230, 80), norm = RGB(190, 195, 210);
    char buf[40];

    drawText(menuSel == 0 ? "> JOGAR" : "  JOGAR", 90, 90, 3, menuSel == 0 ? sel : norm);

    unsigned int nc = (menuSel == 1) ? sel : norm;
    drawText(menuSel == 1 ? "> NOME" : "  NOME", 90, 118, 3, nc);
    for (int i = 0; i < NAMELEN; i++) {
        char one[2] = {playerName[i], 0};
        drawText(one, 250 + i * 12, 118, 3, editing ? RGB(255, 255, 255) : nc);
    }
    if (editing && (frame / 20) % 2 == 0)
        fillRect(250 + cursorPos * 12, 118 + 17, 9, 3, RGB(255, 230, 80));

    snprintf(buf, sizeof buf, "< %s >", skins[skinSel].name);
    drawText(menuSel == 2 ? "> SKIN" : "  SKIN", 90, 146, 3, menuSel == 2 ? sel : norm);
    drawText(buf, 250, 146, 3, menuSel == 2 ? sel : norm);

    // cobra de previa com a skin escolhida
    for (int i = 44; i >= 0; i--) {
        float x = 400.0f - i * 6.5f;
        float y = 213.0f + sinf(frame * 0.06f + i * 0.35f) * 14.0f;
        fillCircle((int)x, (int)y, 8, skinColor(skinSel, i));
    }
    {
        float hy = 213.0f + sinf(frame * 0.06f) * 14.0f;
        fillCircle(404, (int)hy - 4, 3, RGB(255, 255, 255));
        fillCircle(404, (int)hy + 4, 3, RGB(255, 255, 255));
        fillCircle(405, (int)hy - 4, 1, RGB(0, 0, 0));
        fillCircle(405, (int)hy + 4, 1, RGB(0, 0, 0));
    }

    snprintf(buf, sizeof buf, "RECORDE: %d", best);
    drawText(buf, 8, 8, 2, RGB(255, 230, 80));
    drawTextC(editing ? "CIMA/BAIXO LETRA  ESQ/DIR POS  X OK"
                      : "CIMA/BAIXO ESCOLHER   X CONFIRMAR",
              SW / 2, 252, 2, RGB(140, 145, 165));
}

// ------------------------------------------------------------ render do jogo
static void drawSnake(const Snake *s) {
    int r = (int)(s->rad * zoom);
    if (r < 2) r = 2;
    for (int i = s->len - 1; i >= 0; i--) {
        int sx = SX(s->x[i]), sy = SY(s->y[i]);
        if (sx < -r || sx > SW + r || sy < -r || sy > SH + r) continue;
        fillCircle(sx, sy, r, skinColor(s->skin, i));
    }
    int hx = SX(s->x[0]), hy = SY(s->y[0]);
    if (hx < -20 || hx > SW + 20 || hy < -20 || hy > SH + 20) return;
    float ex = -sinf(s->ang) * r * 0.55f, ey = cosf(s->ang) * r * 0.55f;
    float fx = cosf(s->ang) * r * 0.35f, fy = sinf(s->ang) * r * 0.35f;
    int ex0 = hx + (int)fx, ey0 = hy + (int)fy;
    int er = r / 3 + 1;
    fillCircle(ex0 + (int)ex, ey0 + (int)ey, er, RGB(255, 255, 255));
    fillCircle(ex0 - (int)ex, ey0 - (int)ey, er, RGB(255, 255, 255));
    int pr = er / 2 > 0 ? er / 2 : 1;
    int px = (int)(cosf(s->ang) * er * 0.4f), py = (int)(sinf(s->ang) * er * 0.4f);
    fillCircle(ex0 + (int)ex + px, ey0 + (int)ey + py, pr, RGB(0, 0, 0));
    fillCircle(ex0 - (int)ex + px, ey0 - (int)ey + py, pr, RGB(0, 0, 0));
    drawTextC(s->name, hx, hy + r + 5, 1, RGB(210, 215, 230));
}

static void renderGame(void) {
    Snake *p = &snakes[0];
    char buf[40];
    camX = p->x[0];
    camY = p->y[0];
    float zt = 1.0f / (1.0f + (p->len - 10) * 0.003f);
    if (zt < 0.55f) zt = 0.55f;
    zoom += (zt - zoom) * 0.04f;

    fillRect(0, 0, SW, SH, RGB(18, 20, 30));
    int left = SX(0.0f), right = SX(WORLD), top = SY(0.0f), bottom = SY(WORLD);
    unsigned int red = RGB(110, 25, 25);
    if (left > 0) fillRect(0, 0, left, SH, red);
    if (right < SW) fillRect(right, 0, SW - right, SH, red);
    if (top > 0) fillRect(0, 0, SW, top, red);
    if (bottom < SH) fillRect(0, bottom, SW, SH - bottom, red);

    // grade hexagonal de pontos
    float wx0 = camX - SW / 2 / zoom, wx1 = camX + SW / 2 / zoom;
    float wy0 = camY - SH / 2 / zoom, wy1 = camY + SH / 2 / zoom;
    int r0 = (int)floorf(wy0 / 50.0f) - 1, r1 = (int)floorf(wy1 / 50.0f) + 1;
    int c0 = (int)floorf(wx0 / 60.0f) - 1, c1 = (int)floorf(wx1 / 60.0f) + 1;
    int dr = (int)(2 * zoom);
    if (dr < 1) dr = 1;
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++) {
            float gx = c * 60.0f + ((r & 1) ? 30.0f : 0.0f), gy = r * 50.0f;
            if (gx < 0 || gy < 0 || gx > WORLD || gy > WORLD) continue;
            fillCircle(SX(gx), SY(gy), dr, RGB(36, 40, 60));
        }

    for (int i = 0; i < FOOD_MAX; i++) {
        Food *f = &foods[i];
        if (!f->active) continue;
        int sx = SX(f->x), sy = SY(f->y);
        if (sx < -8 || sx > SW + 8 || sy < -8 || sy > SH + 8) continue;
        int r = (int)((f->value >= 3 ? 5 : f->value == 2 ? 4 : 3) * zoom);
        if (r < 2) r = 2;
        fillCircle(sx, sy, r + 2, shade(f->col, 1, 3));
        fillCircle(sx, sy, r, f->col);
    }

    for (int k = NSNAKE - 1; k >= 1; k--)
        if (snakes[k].alive) drawSnake(&snakes[k]);
    if (p->alive) drawSnake(p);

    // placar
    int order[NSNAKE], n = 0;
    for (int i = 0; i < NSNAKE; i++) {
        if (!snakes[i].alive) continue;
        int j = n++;
        while (j > 0 && snakes[order[j - 1]].score < snakes[i].score) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = i;
    }
    int lx = SW - 138;
    drawText("PLACAR", lx, 6, 2, RGB(255, 255, 255));
    for (int i = 0; i < n && i < 8; i++) {
        Snake *s = &snakes[order[i]];
        unsigned int c = (order[i] == 0) ? RGB(255, 230, 80) : RGB(215, 220, 232);
        drawText(s->name, lx, 22 + i * 12, 2, c);
        snprintf(buf, sizeof buf, "%d", displayLen(s));
        drawText(buf, lx + 90, 22 + i * 12, 2, c);
    }

    // HUD
    int rank = 1;
    for (int i = 1; i < NSNAKE; i++)
        if (snakes[i].alive && snakes[i].score > p->score) rank++;
    snprintf(buf, sizeof buf, "TAMANHO: %d", displayLen(p));
    drawText(buf, 8, SH - 34, 2, RGB(255, 255, 255));
    snprintf(buf, sizeof buf, "RANK: %d/%d", rank, n > 0 ? n : 1);
    drawText(buf, 8, SH - 20, 2, RGB(200, 205, 220));

    // minimapa
    int mcx = SW - 40, mcy = SH - 40, mr = 32;
    fillCircle(mcx, mcy, mr + 1, RGB(90, 95, 115));
    fillCircle(mcx, mcy, mr, RGB(12, 14, 22));
    for (int i = 1; i < NSNAKE; i++) {
        if (!snakes[i].alive) continue;
        int mx = mcx + (int)((snakes[i].x[0] / WORLD - 0.5f) * 2 * 0.68f * mr);
        int my = mcy + (int)((snakes[i].y[0] / WORLD - 0.5f) * 2 * 0.68f * mr);
        fillRect(mx, my, 2, 2, skinColor(snakes[i].skin, 0));
    }
    if (p->alive) {
        int mx = mcx + (int)((p->x[0] / WORLD - 0.5f) * 2 * 0.68f * mr);
        int my = mcy + (int)((p->y[0] / WORLD - 0.5f) * 2 * 0.68f * mr);
        fillCircle(mx, my, 2, RGB(255, 255, 255));
    }

    if (!p->alive) {
        fillRect(100, 70, 280, 130, RGB(20, 20, 32));
        fillRect(100, 70, 280, 3, RGB(200, 50, 50));
        drawTextC("VOCE MORREU", SW / 2, 86, 4, RGB(255, 90, 90));
        snprintf(buf, sizeof buf, "TAMANHO: %d", displayLen(p));
        drawTextC(buf, SW / 2, 124, 2, RGB(255, 255, 255));
        snprintf(buf, sizeof buf, "RECORDE: %d", best);
        drawTextC(buf, SW / 2, 142, 2, RGB(255, 230, 80));
        drawTextC("X JOGAR DE NOVO", SW / 2, 166, 2, RGB(190, 195, 210));
        drawTextC("TRIANGULO MENU", SW / 2, 180, 2, RGB(190, 195, 210));
    }
}

// ------------------------------------------------------------ sistema
static int exitCb(int a, int b, void *c) {
    sceKernelExitGame();
    return 0;
}
static int cbThread(SceSize args, void *argp) {
    int id = sceKernelCreateCallback("Exit", exitCb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}

int main(void) {
    int th = sceKernelCreateThread("cb", cbThread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
    scePowerSetClockFrequency(333, 333, 166);

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    srand(sceKernelGetSystemTimeLow());

    unsigned char *vbase = (unsigned char *)sceGeEdramGetAddr();
    sceDisplaySetMode(0, SW, SH);

    // nomes unicos para os bots
    int perm[NBOTNAMES];
    for (int i = 0; i < NBOTNAMES; i++) perm[i] = i;
    for (int i = NBOTNAMES - 1; i > 0; i--) {
        int j = rand() % (i + 1), t = perm[i];
        perm[i] = perm[j];
        perm[j] = t;
    }
    memset(snakes, 0, sizeof snakes);
    for (int i = 1; i < NSNAKE; i++) {
        strncpy(snakes[i].name, botNames[perm[i - 1]], NAMELEN);
        snakes[i].name[NAMELEN] = 0;
    }
    loadData();

    SceCtrlData pad;
    unsigned int prev = 0;
    for (;;) {
        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~prev;
        prev = pad.Buttons;
        frame++;

        if (state == ST_MENU) menuUpdate(&pad, pressed);
        else gameUpdate(&pad, pressed);

        int back = 1 - frameIdx;
        draw = (unsigned int *)(vbase + back * FRAME_BYTES);
        if (state == ST_MENU) renderMenu();
        else renderGame();

        sceKernelDcacheWritebackAll();
        sceDisplayWaitVblankStart();
        sceDisplaySetFrameBuf(vbase + back * FRAME_BYTES, BUFW, PSP_DISPLAY_PIXEL_FORMAT_8888,
                              PSP_DISPLAY_SETBUF_NEXTFRAME);
        frameIdx = back;
    }
    return 0;
}
