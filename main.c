#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspctrl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

PSP_MODULE_INFO("Slither PSP Homebrew", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCREEN_W 480
#define SCREEN_H 272
#define MAX_SNAKES 8
#define MAX_SEGMENTS 180
#define MAX_FOOD 220
#define PI 3.14159265f

typedef struct {
    float x;
    float y;
} Vec2;

typedef struct {
    Vec2 p[MAX_SEGMENTS];
    int len;
    float angle;
    float speed;
    unsigned int color;
    int alive;
    int bot;
    float think;
} Snake;

typedef struct {
    Vec2 p;
    int active;
} Food;

static unsigned int __attribute__((aligned(16))) displayList[262144];
static Snake snakes[MAX_SNAKES];
static Food food[MAX_FOOD];
static int running = 1;

static const unsigned int snakeColors[] = {
    0xFF35C9FF, 0xFF55DD55, 0xFFFFAA33, 0xFFDD55DD,
    0xFF55FFFF, 0xFFFF5577, 0xFFBBBBFF, 0xFFFFFF55
};

static float randomFloat(float min, float max) {
    return min + ((float)rand() / (float)RAND_MAX) * (max - min);
}

static float distanceSquared(Vec2 a, Vec2 b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

static void initializeSnake(Snake *snake, int index, int isBot) {
    memset(snake, 0, sizeof(Snake));

    snake->len = 22;
    snake->angle = randomFloat(0.0f, 2.0f * PI);
    snake->speed = isBot ? 1.25f : 1.55f;
    snake->color = snakeColors[index % 8];
    snake->alive = 1;
    snake->bot = isBot;
    snake->think = randomFloat(0.3f, 1.5f);

    snake->p[0].x = randomFloat(-650.0f, 650.0f);
    snake->p[0].y = randomFloat(-650.0f, 650.0f);

    for (int i = 1; i < snake->len; i++) {
        snake->p[i].x = snake->p[0].x - cosf(snake->angle) * i * 7.0f;
        snake->p[i].y = snake->p[0].y - sinf(snake->angle) * i * 7.0f;
    }
}

static void spawnFood(void) {
    for (int i = 0; i < MAX_FOOD; i++) {
        if (!food[i].active) {
            food[i].active = 1;
            food[i].p.x = randomFloat(-1100.0f, 1100.0f);
            food[i].p.y = randomFloat(-1100.0f, 1100.0f);
            return;
        }
    }
}

static void resetGame(void) {
    memset(food, 0, sizeof(food));

    for (int i = 0; i < MAX_SNAKES; i++) {
        initializeSnake(&snakes[i], i, i != 0);
    }

    for (int i = 0; i < MAX_FOOD; i++) {
        spawnFood();
    }
}

static void updateSnake(Snake *snake, float dt, SceCtrlData *pad) {
    if (!snake->alive) {
        return;
    }

    if (!snake->bot) {
        float x = 0.0f;
        float y = 0.0f;

        if (pad->Lx > 150) x += 1.0f;
        if (pad->Lx < 105) x -= 1.0f;
        if (pad->Ly > 150) y += 1.0f;
        if (pad->Ly < 105) y -= 1.0f;

        if (x != 0.0f || y != 0.0f) {
            float target = atan2f(y, x);
            float diff = target - snake->angle;

            while (diff > PI) diff -= 2.0f * PI;
            while (diff < -PI) diff += 2.0f * PI;

            snake->angle += diff * 4.0f * dt;
        }
    } else {
        snake->think -= dt;

        if (snake->think <= 0.0f) {
            snake->angle += randomFloat(-1.15f, 1.15f);
            snake->think = randomFloat(0.4f, 1.8f);
        }

        /* Bots gently turn toward nearby food. */
        for (int i = 0; i < MAX_FOOD; i++) {
            if (food[i].active &&
                distanceSquared(snake->p[0], food[i].p) < 15000.0f) {
                float target = atan2f(
                    food[i].p.y - snake->p[0].y,
                    food[i].p.x - snake->p[0].x
                );
                float diff = target - snake->angle;

                while (diff > PI) diff -= 2.0f * PI;
                while (diff < -PI) diff += 2.0f * PI;

                snake->angle += diff * 0.35f * dt;
                break;
            }
        }
    }

    Vec2 next;
    next.x = snake->p[0].x + cosf(snake->angle) * snake->speed * 60.0f * dt;
    next.y = snake->p[0].y + sinf(snake->angle) * snake->speed * 60.0f * dt;

    for (int i = snake->len - 1; i > 0; i--) {
        snake->p[i] = snake->p[i - 1];
    }

    snake->p[0] = next;
}

static void updateGame(float dt, SceCtrlData *pad) {
    for (int i = 0; i < MAX_SNAKES; i++) {
        updateSnake(&snakes[i], dt, pad);
    }

    for (int i = 0; i < MAX_SNAKES; i++) {
        if (!snakes[i].alive) {
            continue;
        }

        /* Eat food and grow. */
        for (int f = 0; f < MAX_FOOD; f++) {
            if (food[f].active &&
                distanceSquared(snakes[i].p[0], food[f].p) < 170.0f) {
                food[f].active = 0;

                if (snakes[i].len < MAX_SEGMENTS - 1) {
                    snakes[i].p[snakes[i].len] =
                        snakes[i].p[snakes[i].len - 1];
                    snakes[i].len++;
                }

                spawnFood();
            }
        }

        /* Simple body collision. A collided snake respawns. */
        int collided = 0;

        for (int j = 0; j < MAX_SNAKES && !collided; j++) {
            if (!snakes[j].alive) {
                continue;
            }

            int firstSegment = (i == j) ? 12 : 0;

            for (int k = firstSegment; k < snakes[j].len; k++) {
                if (distanceSquared(snakes[i].p[0], snakes[j].p[k]) < 55.0f) {
                    collided = 1;
                    break;
                }
            }
        }

        if (collided) {
            initializeSnake(&snakes[i], i, i != 0);
        }
    }
}

/*
 * Draw a square using GU_SPRITES.
 * GU_SPRITES uses two vertices: top-left and bottom-right.
 * This replaces the unsupported sceGuPointSize() call.
 */
static void drawPoint(float x, float y, unsigned int color, int size) {
    ScePspFVector2 vertices[2];

    vertices[0].x = x - (float)size * 0.5f;
    vertices[0].y = y - (float)size * 0.5f;
    vertices[1].x = x + (float)size * 0.5f;
    vertices[1].y = y + (float)size * 0.5f;

    sceGuColor(color);
    sceGuDrawArray(
        GU_SPRITES,
        GU_VERTEX_32BITF | GU_TRANSFORM_2D,
        2,
        NULL,
        vertices
    );
}

static void drawScene(void) {
    sceGuStart(GU_DIRECT, displayList);

    sceGuClearColor(0xFF101820);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuShadeModel(GU_FLAT);

    Vec2 camera = snakes[0].p[0];

    /* Arena grid. */
    sceGuColor(0xFF1C2A32);

    for (int x = -1200; x <= 1200; x += 100) {
        ScePspFVector2 line[2];

        line[0].x = SCREEN_W / 2.0f + x - camera.x;
        line[0].y = SCREEN_H / 2.0f - camera.y - 1200.0f;
        line[1].x = SCREEN_W / 2.0f + x - camera.x;
        line[1].y = SCREEN_H / 2.0f - camera.y + 1200.0f;

        sceGuDrawArray(
            GU_LINES,
            GU_VERTEX_32BITF | GU_TRANSFORM_2D,
            2, NULL, line
        );
    }

    for (int y = -1200; y <= 1200; y += 100) {
        ScePspFVector2 line[2];

        line[0].x = SCREEN_W / 2.0f - camera.x - 1200.0f;
        line[0].y = SCREEN_H / 2.0f + y - camera.y;
        line[1].x = SCREEN_W / 2.0f - camera.x + 1200.0f;
        line[1].y = SCREEN_H / 2.0f + y - camera.y;

        sceGuDrawArray(
            GU_LINES,
            GU_VERTEX_32BITF | GU_TRANSFORM_2D,
            2, NULL, line
        );
    }

    /* Food. */
    for (int i = 0; i < MAX_FOOD; i++) {
        if (food[i].active) {
            float x = SCREEN_W / 2.0f + food[i].p.x - camera.x;
            float y = SCREEN_H / 2.0f + food[i].p.y - camera.y;

            if (x > -5.0f && x < SCREEN_W + 5.0f &&
                y > -5.0f && y < SCREEN_H + 5.0f) {
                drawPoint(x, y, 0xFF55EEDD, 5);
            }
        }
    }

    /* Snakes: tail first, head last. */
    for (int i = 0; i < MAX_SNAKES; i++) {
        if (!snakes[i].alive) {
            continue;
        }

        for (int k = snakes[i].len - 1; k >= 0; k--) {
            float x = SCREEN_W / 2.0f + snakes[i].p[k].x - camera.x;
            float y = SCREEN_H / 2.0f + snakes[i].p[k].y - camera.y;

            if (x > -12.0f && x < SCREEN_W + 12.0f &&
                y > -12.0f && y < SCREEN_H + 12.0f) {
                drawPoint(
                    x, y, snakes[i].color,
                    (k == 0) ? 13 : 10
                );
            }
        }
    }

    /* Player head marker. */
    drawPoint(SCREEN_W / 2.0f, SCREEN_H / 2.0f, 0xFFFFFFFF, 5);

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

int main(void) {
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    sceGuInit();
    sceGuStart(GU_DIRECT, displayList);

    sceGuDrawBuffer(GU_PSM_8888, (void *)0, 512);
    sceGuDispBuffer(SCREEN_W, SCREEN_H, (void *)0x88000, 512);
    sceGuOffset(2048 - SCREEN_W / 2, 2048 - SCREEN_H / 2);
    sceGuViewport(2048, 2048, SCREEN_W, SCREEN_H);
    sceGuDepthRange(0xC350, 0x2710);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    srand(12345);
    resetGame();

    SceCtrlData pad;
    unsigned int lastTime = sceKernelGetSystemTimeLow();

    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);

        if (pad.Buttons & PSP_CTRL_START) {
            running = 0;
        }

        if (pad.Buttons & PSP_CTRL_SELECT) {
            resetGame();
        }

        unsigned int currentTime = sceKernelGetSystemTimeLow();
        float dt = (currentTime - lastTime) / 1000000.0f;
        lastTime = currentTime;

        if (dt > 0.05f) {
            dt = 0.05f;
        }

        updateGame(dt, &pad);
        drawScene();
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
