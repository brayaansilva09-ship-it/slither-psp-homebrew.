// SerpentIO - jogo de cobra estilo "io" para PSP (homebrew, PSPSDK)
//
// MENU     : cima/baixo escolhe | X confirma
//            NOME: X edita; cima/baixo troca a letra; esq/dir move o cursor; X termina
//            SKIN: abre a grade de skins | CONFIG: idioma, bots, comida, nomes
// JOGO     : analogico (ou direcional) = direcao | X ou R = acelerar | START = menu
// MORREU   : mostra o resultado e volta ao menu

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

#define WORLD 5000.0f
#define WR (WORLD / 2.0f) // arena redonda: centro (WR,WR), raio WR
#define MAXSEG 240
#define NSNAKE 24
#define FOOD_MAX 2200
#define FOOD_AMBIENT 1300
#define SPACING 6.0f
#define NAMELEN 10
#define NSKIN 32 // precisa ser multiplo de 8 (grade 8 colunas)
#define SKIN_COLS 8
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
    int len, alive, skin, boost, tick, respawnTimer, burst, cool;
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
    int skin, best, lang, bots, food, names;
} Save;

enum { ST_MENU, ST_PLAY, ST_SKINS, ST_SETTINGS };

// ------------------------------------------------------------ textos (3 idiomas)
enum {
    S_PLAY, S_NAME, S_SKIN, S_SETTINGS, S_RECORD, S_HELP_MENU, S_HELP_EDIT, S_LEADER,
    S_LENGTH, S_RANK, S_KILLS, S_DIED, S_BACKMENU, S_LANG, S_BOTS, S_FOOD, S_NAMES,
    S_BACK, S_HELP_SKINS, S_HELP_SET, S_LOW, S_NORMAL, S_HIGH, S_ON, S_OFF, NSTR
};

static const char *T[NSTR][3] = {
    {"JOGAR", "PLAY", "JUGAR"},
    {"NOME", "NAME", "NOMBRE"},
    {"SKIN", "SKIN", "SKIN"},
    {"CONFIG", "SETTINGS", "AJUSTES"},
    {"RECORDE", "BEST", "RECORD"},
    {"CIMA/BAIXO ESCOLHER   X CONFIRMAR", "UP/DOWN SELECT   X CONFIRM",
     "ARRIBA/ABAJO ELEGIR   X OK"},
    {"CIMA/BAIXO LETRA  ESQ/DIR POS  X OK", "UP/DOWN LETTER  L/R MOVE  X OK",
     "ARRIBA/ABAJO LETRA  IZQ/DER  X OK"},
    {"PLACAR", "LEADERBOARD", "CLASIF."},
    {"TAMANHO", "LENGTH", "LONGITUD"},
    {"RANK", "RANK", "RANGO"},
    {"ABATES", "KILLS", "BAJAS"},
    {"VOCE MORREU", "YOU DIED", "HAS MUERTO"},
    {"VOLTANDO AO MENU...", "BACK TO MENU...", "VOLVIENDO AL MENU..."},
    {"IDIOMA", "LANGUAGE", "IDIOMA"},
    {"BOTS", "BOTS", "BOTS"},
    {"COMIDA", "FOOD", "COMIDA"},
    {"NOMES", "NAMES", "NOMBRES"},
    {"VOLTAR", "BACK", "VOLVER"},
    {"DIRECIONAL MOVER  X OK  O VOLTAR", "D-PAD MOVE  X OK  O BACK", "CRUZ MOVER  X OK  O VOLVER"},
    {"CIMA/BAIXO ESCOLHER  ESQ/DIR MUDAR", "UP/DOWN SELECT  L/R CHANGE",
     "ARRIBA/ABAJO  IZQ/DER CAMBIAR"},
    {"POUCA", "LOW", "POCA"},
    {"NORMAL", "NORMAL", "NORMAL"},
    {"MUITA", "HIGH", "MUCHA"},
    {"SIM", "ON", "SI"},
    {"NAO", "OFF", "NO"},
};
static int lang = 0;
#define TXT(id) (T[id][lang])
static const char *langNames[3] = {"PORTUGUES", "ENGLISH", "ESPANOL"};

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
    {"AZUL", 2, {RGB(50, 110, 255), RGB(30, 70, 200)}, 3},
    {"VERMELHO", 2, {RGB(235, 50, 50), RGB(170, 30, 30)}, 3},
    {"LARANJA", 2, {RGB(255, 150, 40), RGB(220, 105, 20)}, 3},
    {"ROSA", 2, {RGB(255, 120, 190), RGB(230, 80, 150)}, 3},
    {"AMARELO", 2, {RGB(255, 235, 70), RGB(225, 190, 30)}, 3},
    {"BRASIL", 3, {RGB(30, 170, 70), RGB(255, 220, 40), RGB(40, 90, 200)}, 4},
    {"EUA", 3, {RGB(220, 50, 60), RGB(245, 245, 250), RGB(50, 80, 170)}, 4},
    {"ITALIA", 3, {RGB(30, 150, 70), RGB(245, 245, 245), RGB(210, 40, 50)}, 4},
    {"ALEMANHA", 3, {RGB(60, 60, 70), RGB(220, 40, 40), RGB(255, 205, 40)}, 4},
    {"FRANCA", 3, {RGB(50, 80, 190), RGB(245, 245, 245), RGB(220, 50, 60)}, 4},
    {"JAPAO", 2, {RGB(245, 245, 245), RGB(220, 40, 60)}, 5},
    {"ARGENTINA", 2, {RGB(120, 190, 255), RGB(245, 245, 245)}, 4},
    {"CANDY", 2, {RGB(255, 80, 110), RGB(255, 245, 245)}, 2},
    {"OCEANO", 3, {RGB(20, 90, 170), RGB(40, 170, 200), RGB(150, 230, 230)}, 3},
    {"LAVA", 3, {RGB(90, 20, 20), RGB(230, 70, 30), RGB(255, 190, 50)}, 3},
    {"FLORESTA", 3, {RGB(20, 90, 40), RGB(70, 150, 60), RGB(150, 200, 80)}, 3},
    {"AURORA", 3, {RGB(70, 120, 255), RGB(150, 90, 255), RGB(255, 120, 200)}, 3},
    {"AREIA", 2, {RGB(235, 200, 140), RGB(200, 160, 100)}, 3},
    {"LIMAO", 2, {RGB(200, 255, 70), RGB(255, 240, 80)}, 3},
    {"TURQUESA", 2, {RGB(30, 200, 190), RGB(20, 140, 150)}, 3},
    {"UVA", 2, {RGB(120, 50, 190), RGB(80, 30, 140)}, 3},
    {"CEREJA", 2, {RGB(210, 30, 70), RGB(255, 120, 150)}, 2},
    {"MENTA", 2, {RGB(150, 255, 200), RGB(90, 210, 160)}, 3},
    {"CARVAO", 2, {RGB(150, 150, 160), RGB(70, 70, 85)}, 3},
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
static int deadTimer = 0;
static int playerKills = 0;

static char playerName[NAMELEN + 1] = "PLAYER    ";
static int skinSel = 0, skinCursor = 0;
static int menuSel = 0, editing = 0, cursorPos = 0;

// configuracoes
static int botCount = 22; // 0..NSNAKE-1
static int foodLevel = 1; // 0 pouca, 1 normal, 2 muita
static int showNames = 1;
static int ambientFood = FOOD_AMBIENT;
static int setSel = 0;
static const int foodPct[3] = {60, 100, 130};

static float camX = 0, camY = 0, zoom = 1.0f;
#define SX(wx) ((int)(((wx) - camX) * zoom + SW / 2))
#define SY(wy) ((int)(((wy) - camY) * zoom + SH / 2))

static float frand(void) { return (float)rand() / (float)RAND_MAX; }
static void applyFood(void) { ambientFood = FOOD_AMBIENT * foodPct[foodLevel] / 100; }

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

// escurece uma area (painel translucido)
static void dimRect(int x, int y, int w, int h) {
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > SW) x1 = SW;
    if (y1 > SH) y1 = SH;
    for (int j = y; j < y1; j++) {
        unsigned int *row = draw + j * BUFW;
        for (int i = x; i < x1; i++) row[i] = ((row[i] >> 1) & 0x007F7F7Fu) | 0xFF000000u;
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

// segmento do corpo com contorno escuro (efeito de escamas)
static void drawSeg(int x, int y, int r, unsigned int c) {
    if (r >= 4) {
        fillCircle(x, y, r, shade(c, 2, 3));
        fillCircle(x, y, r - (r >= 7 ? 2 : 1), c);
    } else {
        fillCircle(x, y, r, c);
    }
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

static void drawTextR(const char *t, int rx, int y, int sc, unsigned int c) {
    drawText(t, rx - (int)strlen(t) * 4 * sc, y, sc, c);
}

// ------------------------------------------------------------ salvar/carregar
static void saveData(void) {
    Save sv;
    memset(&sv, 0, sizeof sv);
    memcpy(sv.magic, "SPT2", 4);
    memcpy(sv.name, playerName, NAMELEN + 1);
    sv.skin = skinSel;
    sv.best = best;
    sv.lang = lang;
    sv.bots = botCount;
    sv.food = foodLevel;
    sv.names = showNames;
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
    if (n != (int)sizeof sv || memcmp(sv.magic, "SPT2", 4) != 0) return;
    for (int i = 0; i < NAMELEN; i++)
        playerName[i] = (sv.name[i] != 0 && strchr(CHARS, sv.name[i])) ? sv.name[i] : ' ';
    playerName[NAMELEN] = 0;
    if (sv.skin >= 0 && sv.skin < NSKIN) skinSel = sv.skin;
    if (sv.best > 0 && sv.best < 1000000) best = sv.best;
    if (sv.lang >= 0 && sv.lang < 3) lang = sv.lang;
    if (sv.bots >= 0 && sv.bots < NSNAKE) botCount = sv.bots;
    if (sv.food >= 0 && sv.food < 3) foodLevel = sv.food;
    showNames = sv.names ? 1 : 0;
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
    float a = frand() * 2 * PI, d = sqrtf(frand()) * (WR - 30);
    spawnFood(f, v, WR + cosf(a) * d, WR + sinf(a) * d, pal[rand() % 8]);
}

static void dropFood(float x, float y, int value, unsigned int col) {
    for (int i = 0; i < FOOD_MAX; i++)
        if (!foods[i].active) {
            spawnFood(&foods[i], value, x + frand() * 10 - 5, y + frand() * 10 - 5, col);
            return;
        }
}

static void respawn(Snake *s, int isPlayer) {
    float x = WR, y = WR;
    for (int t = 0; t < 8; t++) {
        float a = frand() * 2 * PI, d = sqrtf(frand()) * (WR - 350);
        x = WR + cosf(a) * d;
        y = WR + sinf(a) * d;
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
    s->burst = 0;
    s->cool = 120 + rand() % 240;
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
    for (int i = 0; i < s->len; i++)
        dropFood(s->x[i], s->y[i], 2, skinColor(s->skin, i));
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

    float cdx = hx - WR, cdy = hy - WR;
    if (cdx * cdx + cdy * cdy > (WR - 230.0f) * (WR - 230.0f)) {
        goal = atan2f(-cdy, -cdx);
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
            float qx = px - WR, qy = py - WR;
            if (qx * qx + qy * qy > (WR - 30.0f) * (WR - 30.0f)) sc -= 6.0f * w;
            if (dangerAt(idx, px, py, s->rad)) sc -= 8.0f * w;
        }
        if (sc > bestScore) {
            bestScore = sc;
            bestA = a;
        }
    }
    s->desired = bestA;

    // boost so em rajadas curtas, com pausa depois (bots andam na velocidade normal)
    if (s->cool > 0) s->cool -= 3;
    if (boost && s->cool <= 0 && s->score > 30) {
        s->burst += 3;
        if (s->burst > 45) {
            s->burst = 0;
            s->cool = 300;
            boost = 0;
        }
    } else {
        boost = 0;
        if (s->burst > 0) s->burst -= 1;
    }
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
    int dead[NSNAKE], killer[NSNAKE];
    memset(dead, 0, sizeof dead);
    for (int q = 0; q < NSNAKE; q++) killer[q] = -1;
    for (int a = 0; a < NSNAKE; a++) {
        Snake *s = &snakes[a];
        if (!s->alive) continue;
        float wdx = s->x[0] - WR, wdy = s->y[0] - WR;
        if (wdx * wdx + wdy * wdy > WR * WR) {
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
                    killer[a] = b;
                    break;
                }
            }
        }
    }
    for (int a = 0; a < NSNAKE; a++)
        if (dead[a]) {
            if (killer[a] == 0) playerKills++;
            killSnake(&snakes[a]);
        }
}

static void startGame(void) {
    memset(foods, 0, sizeof foods);
    applyFood();
    for (int i = 0; i < ambientFood; i++) spawnAmbient(&foods[i]);
    trimName(snakes[0].name);
    snakes[0].skin = skinSel;
    for (int i = 0; i < NSNAKE; i++) snakes[i].alive = 0;
    respawn(&snakes[0], 1);
    for (int i = 1; i < NSNAKE; i++) {
        if (i <= botCount) respawn(&snakes[i], 0);
        else snakes[i].respawnTimer = 0x7fffffff;
    }
    zoom = 1.0f;
    playerKills = 0;
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
        if (--deadTimer <= 0 || (pressed & (PSP_CTRL_CROSS | PSP_CTRL_TRIANGLE))) {
            state = ST_MENU;
            return;
        }
    }

    int wasAlive = p->alive;
    for (int i = 0; i < NSNAKE; i++) {
        Snake *s = &snakes[i];
        if (!s->alive) {
            if (i > 0 && i <= botCount && --s->respawnTimer <= 0) respawn(s, 0);
            continue;
        }
        if (i > 0 && ((frame + i) % 3 == 0)) botThink(i);
        updateSnake(s);
    }
    collisions();

    if (p->alive && displayLen(p) > best) best = displayLen(p);
    if (wasAlive && !p->alive) {
        saveData();
        deadTimer = 120;
    }

    int active = 0;
    for (int i = 0; i < FOOD_MAX; i++) active += foods[i].active;
    for (int n = 0; active < ambientFood && n < 2; n++)
        for (int i = 0; i < FOOD_MAX; i++)
            if (!foods[i].active) {
                spawnAmbient(&foods[i]);
                active++;
                break;
            }
}

// ------------------------------------------------------------ cenas de fundo / previas
static void drawMenuBackground(void) {
    fillRect(0, 0, SW, SH, RGB(14, 16, 26));
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 9; c++) {
            int x = c * 60 + ((r & 1) ? 30 : 0), y = r * 45 + 10;
            fillCircle(x, y, 3, RGB(26, 30, 46));
        }
    for (int i = 0; i < 40; i++) {
        int x = (i * 137 + frame / (2 + i % 3)) % SW, y = (i * 71) % SH;
        unsigned int c = (i % 3 == 0) ? RGB(255, 120, 120) : (i % 3 == 1) ? RGB(110, 220, 255)
                                                                         : RGB(140, 255, 150);
        fillCircle(x, y, 5, shade(c, 1, 5));
        fillCircle(x, y, 3, shade(c, 3, 4));
    }
}

// cobra de previa (cabeca na direita)
static void drawMiniSnake(int skin, int cx, int cy, int r, int n, float sp, float phase) {
    float amp = r * 1.4f;
    for (int k = n - 1; k >= 0; k--) {
        float x = cx + ((n - 1) * 0.5f - k) * sp;
        float y = cy + sinf(phase + k * 0.45f) * amp * (r >= 7 ? 1.0f : 0.5f);
        drawSeg((int)x, (int)y, r, skinColor(skin, k));
        if (k == 0 && r >= 4) {
            int er = r / 3 + 1;
            int ex = (int)x + r / 3;
            fillCircle(ex, (int)y - r / 2, er, RGB(255, 255, 255));
            fillCircle(ex, (int)y + r / 2, er, RGB(255, 255, 255));
            fillCircle(ex + 1, (int)y - r / 2, er / 2 + 0, RGB(0, 0, 0));
            fillCircle(ex + 1, (int)y + r / 2, er / 2 + 0, RGB(0, 0, 0));
        }
    }
}

// ------------------------------------------------------------ menu principal
static void menuUpdate(unsigned int pressed) {
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
    if (pressed & PSP_CTRL_UP) menuSel = (menuSel + 3) % 4;
    if (pressed & PSP_CTRL_DOWN) menuSel = (menuSel + 1) % 4;
    if (pressed & PSP_CTRL_CROSS) {
        if (menuSel == 0) startGame();
        else if (menuSel == 1) editing = 1;
        else if (menuSel == 2) { skinCursor = skinSel; state = ST_SKINS; }
        else { setSel = 0; state = ST_SETTINGS; }
    }
}

static void renderMenu(void) {
    drawMenuBackground();
    drawTextC("SERPENTIO", SW / 2, 14, 6, RGB(90, 255, 140));

    unsigned int sel = RGB(255, 230, 80), norm = RGB(190, 195, 210);
    char buf[48];
    int y0 = 80, dy = 27;

    drawText(menuSel == 0 ? "> " : "  ", 90, y0, 3, sel);
    drawText(TXT(S_PLAY), 114, y0, 3, menuSel == 0 ? sel : norm);

    unsigned int nc = (menuSel == 1) ? sel : norm;
    drawText(menuSel == 1 ? "> " : "  ", 90, y0 + dy, 3, sel);
    drawText(TXT(S_NAME), 114, y0 + dy, 3, nc);
    for (int i = 0; i < NAMELEN; i++) {
        char one[2] = {playerName[i], 0};
        drawText(one, 250 + i * 12, y0 + dy, 3, editing ? RGB(255, 255, 255) : nc);
    }
    if (editing && (frame / 20) % 2 == 0)
        fillRect(250 + cursorPos * 12, y0 + dy + 17, 9, 3, RGB(255, 230, 80));

    drawText(menuSel == 2 ? "> " : "  ", 90, y0 + 2 * dy, 3, sel);
    drawText(TXT(S_SKIN), 114, y0 + 2 * dy, 3, menuSel == 2 ? sel : norm);
    drawText(skins[skinSel].name, 250, y0 + 2 * dy, 3, menuSel == 2 ? sel : norm);

    drawText(menuSel == 3 ? "> " : "  ", 90, y0 + 3 * dy, 3, sel);
    drawText(TXT(S_SETTINGS), 114, y0 + 3 * dy, 3, menuSel == 3 ? sel : norm);

    drawMiniSnake(skinSel, SW / 2, 206, 8, 45, 6.5f, frame * 0.06f);

    snprintf(buf, sizeof buf, "%s: %d", TXT(S_RECORD), best);
    drawText(buf, 8, 8, 2, RGB(255, 230, 80));
    drawTextC(editing ? TXT(S_HELP_EDIT) : TXT(S_HELP_MENU), SW / 2, 241, 2,
              RGB(140, 145, 165));
    drawTextC("SlitherPSP v3 beta", SW / 2, 262, 1, RGB(95, 100, 120));
}

// ------------------------------------------------------------ menu de skins (grade)
static void skinsUpdate(unsigned int pressed) {
    if (pressed & PSP_CTRL_LEFT) skinCursor = (skinCursor + NSKIN - 1) % NSKIN;
    if (pressed & PSP_CTRL_RIGHT) skinCursor = (skinCursor + 1) % NSKIN;
    if (pressed & PSP_CTRL_UP)
        skinCursor = (skinCursor >= SKIN_COLS) ? skinCursor - SKIN_COLS : skinCursor + NSKIN - SKIN_COLS;
    if (pressed & PSP_CTRL_DOWN)
        skinCursor = (skinCursor + SKIN_COLS < NSKIN) ? skinCursor + SKIN_COLS : skinCursor - (NSKIN - SKIN_COLS);
    if (pressed & PSP_CTRL_CROSS) {
        skinSel = skinCursor;
        saveData();
        state = ST_MENU;
    }
    if (pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_TRIANGLE)) state = ST_MENU;
}

static void renderSkins(void) {
    drawMenuBackground();
    drawText(TXT(S_SKIN), 16, 8, 3, RGB(90, 255, 140));
    drawTextR(TXT(S_HELP_SKINS), SW - 16, 14, 1, RGB(140, 145, 165));

    int x0 = 16, y0 = 32, cw = 56, ch = 52;
    int rows = NSKIN / SKIN_COLS;
    for (int i = 0; i < NSKIN; i++) {
        int cx = x0 + (i % SKIN_COLS) * cw, cy = y0 + (i / SKIN_COLS) * ch;
        int isSel = (i == skinCursor);
        if (isSel) fillRect(cx + 1, cy + 1, cw - 2, ch - 2, RGB(255, 230, 80));
        fillRect(cx + 3, cy + 3, cw - 6, ch - 6, isSel ? RGB(40, 46, 70) : RGB(22, 26, 40));
        drawMiniSnake(i, cx + cw / 2, cy + ch / 2, 5, 6, 6.5f, isSel ? frame * 0.12f : 0.0f);
        if (i == skinSel) fillCircle(cx + cw - 9, cy + 9, 3, RGB(90, 255, 140));
    }
    (void)rows;
    drawTextC(skins[skinCursor].name, SW / 2, 246, 3, RGB(255, 230, 80));
}

// ------------------------------------------------------------ configuracoes
static void settingsUpdate(unsigned int pressed) {
    if (pressed & PSP_CTRL_UP) setSel = (setSel + 4) % 5;
    if (pressed & PSP_CTRL_DOWN) setSel = (setSel + 1) % 5;
    int d = 0;
    if (pressed & PSP_CTRL_RIGHT) d = 1;
    if (pressed & PSP_CTRL_LEFT) d = -1;
    if ((pressed & PSP_CTRL_CROSS) && setSel < 4) d = 1;
    if (d) {
        if (setSel == 0) lang = (lang + d + 3) % 3;
        else if (setSel == 1) {
            botCount += d;
            if (botCount < 0) botCount = NSNAKE - 1;
            if (botCount > NSNAKE - 1) botCount = 0;
        } else if (setSel == 2) {
            foodLevel = (foodLevel + d + 3) % 3;
            applyFood();
        } else if (setSel == 3) showNames ^= 1;
    }
    if (setSel == 1) {
        if (pressed & PSP_CTRL_LTRIGGER) botCount = botCount - 5 < 0 ? 0 : botCount - 5;
        if (pressed & PSP_CTRL_RTRIGGER)
            botCount = botCount + 5 > NSNAKE - 1 ? NSNAKE - 1 : botCount + 5;
    }
    if (((pressed & PSP_CTRL_CROSS) && setSel == 4) ||
        (pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_TRIANGLE))) {
        saveData();
        state = ST_MENU;
    }
}

static void renderSettings(void) {
    drawMenuBackground();
    drawText(TXT(S_SETTINGS), 16, 8, 3, RGB(90, 255, 140));

    unsigned int sel = RGB(255, 230, 80), norm = RGB(190, 195, 210);
    char buf[32];
    int y0 = 58, dy = 32;
    const char *labels[4] = {TXT(S_LANG), TXT(S_BOTS), TXT(S_FOOD), TXT(S_NAMES)};
    for (int i = 0; i < 4; i++) {
        unsigned int c = (setSel == i) ? sel : norm;
        int y = y0 + i * dy;
        if (setSel == i) dimRect(40, y - 5, SW - 80, 26);
        drawText(setSel == i ? "> " : "  ", 50, y, 3, sel);
        drawText(labels[i], 74, y, 3, c);
        if (i == 0) snprintf(buf, sizeof buf, "< %s >", langNames[lang]);
        else if (i == 1) snprintf(buf, sizeof buf, "< %d >", botCount);
        else if (i == 2) snprintf(buf, sizeof buf, "< %s >", TXT(S_LOW + foodLevel));
        else snprintf(buf, sizeof buf, "< %s >", TXT(showNames ? S_ON : S_OFF));
        drawText(buf, 250, y, 3, c);
    }
    {
        int y = y0 + 4 * dy;
        if (setSel == 4) dimRect(40, y - 5, SW - 80, 26);
        drawText(setSel == 4 ? "> " : "  ", 50, y, 3, sel);
        drawText(TXT(S_BACK), 74, y, 3, setSel == 4 ? sel : norm);
    }
    drawTextC(TXT(S_HELP_SET), SW / 2, 250, 2, RGB(140, 145, 165));
}

// ------------------------------------------------------------ render do jogo
static void drawSnake(const Snake *s) {
    int r = (int)(s->rad * zoom);
    if (r < 2) r = 2;
    if (s->boost && s->score > 3) { // brilho ao acelerar
        for (int i = s->len - 1; i >= 0; i -= 2) {
            int gx = SX(s->x[i]), gy = SY(s->y[i]);
            if (gx < -r - 4 || gx > SW + r + 4 || gy < -r - 4 || gy > SH + r + 4) continue;
            fillCircle(gx, gy, r + 3, shade(skinColor(s->skin, i), 1, 3));
        }
    }
    for (int i = s->len - 1; i >= 0; i--) {
        int sx = SX(s->x[i]), sy = SY(s->y[i]);
        if (sx < -r || sx > SW + r || sy < -r || sy > SH + r) continue;
        drawSeg(sx, sy, r, skinColor(s->skin, i));
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
    if (showNames) drawTextC(s->name, hx, hy + r + 5, 1, RGB(210, 215, 230));
}

static void renderGame(void) {
    Snake *p = &snakes[0];
    char buf[48];
    camX = p->x[0];
    camY = p->y[0];
    float zt = 1.0f / (1.0f + (p->len - 10) * 0.003f);
    if (zt < 0.55f) zt = 0.55f;
    zoom += (zt - zoom) * 0.04f;

    // fundo: borda vermelha brilhante + arena escura
    fillRect(0, 0, SW, SH, RGB(120, 28, 28));
    fillCircle(SX(WR), SY(WR), (int)(WR * zoom) + 8, RGB(210, 55, 55));
    fillCircle(SX(WR), SY(WR), (int)(WR * zoom), RGB(18, 20, 30));

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
            float qx = gx - WR, qy = gy - WR;
            if (qx * qx + qy * qy > (WR - 10.0f) * (WR - 10.0f)) continue;
            fillCircle(SX(gx), SY(gy), dr, RGB(36, 40, 60));
        }

    for (int i = 0; i < FOOD_MAX; i++) {
        Food *f = &foods[i];
        if (!f->active) continue;
        int sx = SX(f->x), sy = SY(f->y);
        if (sx < -8 || sx > SW + 8 || sy < -8 || sy > SH + 8) continue;
        int r = (int)((f->value >= 3 ? 5 : f->value == 2 ? 4 : 3) * zoom);
        if (r < 2) r = 2;
        int tw = (((frame / 12) + i) % 3 == 0) ? 3 : 2; // brilho pulsando
        fillCircle(sx, sy, r + tw, shade(f->col, 1, 4));
        fillCircle(sx, sy, r, f->col);
    }

    for (int k = NSNAKE - 1; k >= 1; k--)
        if (snakes[k].alive) drawSnake(&snakes[k]);
    if (p->alive) drawSnake(p);

    // placar (painel translucido)
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
    dimRect(lx - 6, 2, SW - (lx - 6) - 2, 142);
    drawText(TXT(S_LEADER), lx, 6, 2, RGB(255, 255, 255));
    for (int i = 0; i < n && i < 10; i++) {
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
    dimRect(4, SH - 52, 140, 46);
    snprintf(buf, sizeof buf, "%s: %d", TXT(S_LENGTH), displayLen(p));
    drawText(buf, 8, SH - 34, 2, RGB(255, 255, 255));
    snprintf(buf, sizeof buf, "%s: %d", TXT(S_KILLS), playerKills);
    drawText(buf, 8, SH - 48, 2, RGB(200, 205, 220));
    snprintf(buf, sizeof buf, "%s: %d/%d", TXT(S_RANK), rank, n > 0 ? n : 1);
    drawText(buf, 8, SH - 20, 2, RGB(200, 205, 220));

    // minimapa
    int mcx = SW - 40, mcy = SH - 40, mr = 32;
    fillCircle(mcx, mcy, mr + 1, RGB(90, 95, 115));
    fillCircle(mcx, mcy, mr, RGB(12, 14, 22));
    for (int i = 1; i < NSNAKE; i++) {
        if (!snakes[i].alive) continue;
        int mx = mcx + (int)((snakes[i].x[0] - WR) / WR * 0.95f * mr);
        int my = mcy + (int)((snakes[i].y[0] - WR) / WR * 0.95f * mr);
        fillRect(mx, my, 2, 2, skinColor(snakes[i].skin, 0));
    }
    if (p->alive) {
        int mx = mcx + (int)((p->x[0] - WR) / WR * 0.95f * mr);
        int my = mcy + (int)((p->y[0] - WR) / WR * 0.95f * mr);
        fillCircle(mx, my, 2, RGB(255, 255, 255));
    }

    if (!p->alive) {
        fillRect(100, 70, 280, 130, RGB(20, 20, 32));
        fillRect(100, 70, 280, 3, RGB(200, 50, 50));
        drawTextC(TXT(S_DIED), SW / 2, 86, 4, RGB(255, 90, 90));
        snprintf(buf, sizeof buf, "%s: %d", TXT(S_LENGTH), displayLen(p));
        drawTextC(buf, SW / 2, 124, 2, RGB(255, 255, 255));
        snprintf(buf, sizeof buf, "%s: %d", TXT(S_RECORD), best);
        drawTextC(buf, SW / 2, 142, 2, RGB(255, 230, 80));
        snprintf(buf, sizeof buf, "%s: %d", TXT(S_KILLS), playerKills);
        drawTextC(buf, SW / 2, 160, 2, RGB(200, 205, 220));
        drawTextC(TXT(S_BACKMENU), SW / 2, 180, 2, RGB(190, 195, 210));
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
    applyFood();

    SceCtrlData pad;
    unsigned int prev = 0;
    for (;;) {
        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~prev;
        prev = pad.Buttons;
        frame++;

        if (state == ST_MENU) menuUpdate(pressed);
        else if (state == ST_SKINS) skinsUpdate(pressed);
        else if (state == ST_SETTINGS) settingsUpdate(pressed);
        else gameUpdate(&pad, pressed);

        int back = 1 - frameIdx;
        draw = (unsigned int *)(vbase + back * FRAME_BYTES);
        if (state == ST_MENU) renderMenu();
        else if (state == ST_SKINS) renderSkins();
        else if (state == ST_SETTINGS) renderSettings();
        else renderGame();

        sceKernelDcacheWritebackAll();
        sceDisplayWaitVblankStart();
        sceDisplaySetFrameBuf(vbase + back * FRAME_BYTES, BUFW, PSP_DISPLAY_PIXEL_FORMAT_8888,
                              PSP_DISPLAY_SETBUF_NEXTFRAME);
        frameIdx = back;
    }
    return 0;
}
