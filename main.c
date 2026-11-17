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

#define W 480
#define H 272
#define MAX_SNAKES 8
#define MAX_SEG 180
#define MAX_FOOD 220
#define PI 3.14159265f

typedef struct { float x, y; } Vec2;
typedef struct {
    Vec2 p[MAX_SEG];
    int len;
    float angle, speed;
    unsigned int color;
    int alive, bot;
    float think;
} Snake;
typedef struct { Vec2 p; int active; } Food;

static unsigned int __attribute__((aligned(16))) list[262144];
static Snake snakes[MAX_SNAKES];
static Food food[MAX_FOOD];
static int running = 1;
static float worldTime = 0;
static unsigned int colors[] = {
    0xFF35C9FF, 0xFF55DD55, 0xFFFFAA33, 0xFFDD55DD,
    0xFF55FFFF, 0xFFFF5577, 0xFFBBBBFF, 0xFFFFFF55
};

static float rndf(float a, float b) {
    return a + ((float)rand() / (float)RAND_MAX) * (b-a);
}
static float dist2(Vec2 a, Vec2 b) {
    float x=a.x-b.x, y=a.y-b.y; return x*x+y*y;
}
static Vec2 add(Vec2 a, Vec2 b) { Vec2 r={a.x+b.x,a.y+b.y}; return r; }
static Vec2 mul(Vec2 a,float s) { Vec2 r={a.x*s,a.y*s}; return r; }

static void initSnake(Snake *s, int i, int bot) {
    memset(s, 0, sizeof(*s));
    s->len = 22;
    s->angle = rndf(0, 2*PI);
    s->speed = bot ? 1.25f : 1.55f;
    s->color = colors[i % 8];
    s->alive = 1; s->bot = bot; s->think = rndf(0.3f, 1.5f);
    s->p[0].x = rndf(-650,650); s->p[0].y = rndf(-650,650);
    for (int k=1;k<s->len;k++) {
        s->p[k].x = s->p[0].x - cosf(s->angle)*k*7;
        s->p[k].y = s->p[0].y - sinf(s->angle)*k*7;
    }
}
static void spawnFood(void) {
    for (int i=0;i<MAX_FOOD;i++) {
        if (!food[i].active) {
            food[i].active=1;
            food[i].p.x=rndf(-1100,1100);
            food[i].p.y=rndf(-1100,1100);
            return;
        }
    }
}
static void resetGame(void) {
    memset(food,0,sizeof(food));
    for(int i=0;i<MAX_SNAKES;i++) initSnake(&snakes[i],i,i!=0);
    for(int i=0;i<MAX_FOOD;i++) spawnFood();
}
static void updateSnake(Snake *s, float dt, SceCtrlData *pad) {
    if(!s->alive) return;
    if(!s->bot) {
        float tx=0, ty=0;
        if(pad->Lx > 128) tx += 1;
        if(pad->Lx < 128) tx -= 1;
        if(pad->Ly > 128) ty += 1;
        if(pad->Ly < 128) ty -= 1;
        if(tx || ty) {
            float target=atan2f(ty,tx);
            float diff=target-s->angle;
            while(diff>PI) diff-=2*PI;
            while(diff<-PI) diff+=2*PI;
            s->angle += diff * 4.0f * dt;
        }
    } else {
        s->think -= dt;
        if(s->think <= 0) {
            s->angle += rndf(-1.15f,1.15f);
            s->think = rndf(0.4f,1.8f);
        }
        /* Bots gently turn toward nearby food. */
        for(int f=0;f<MAX_FOOD;f++) if(food[f].active &&
            dist2(s->p[0],food[f].p)<15000.0f) {
            float target=atan2f(food[f].p.y-s->p[0].y,food[f].p.x-s->p[0].x);
            float d=target-s->angle;
            while(d>PI)d-=2*PI; while(d<-PI)d+=2*PI;
            s->angle += d*0.35f*dt;
            break;
        }
    }
    Vec2 next={s->p[0].x+cosf(s->angle)*s->speed*60*dt,
               s->p[0].y+sinf(s->angle)*s->speed*60*dt};
    for(int k=s->len-1;k>0;k--) s->p[k]=s->p[k-1];
    s->p[0]=next;
}
static void updateGame(float dt, SceCtrlData *pad) {
    worldTime += dt;
    for(int i=0;i<MAX_SNAKES;i++) updateSnake(&snakes[i],dt,pad);
    for(int i=0;i<MAX_SNAKES;i++) if(snakes[i].alive) {
        for(int f=0;f<MAX_FOOD;f++) if(food[f].active &&
            dist2(snakes[i].p[0],food[f].p)<170.0f) {
            food[f].active=0;
            if(snakes[i].len<MAX_SEG-1) {
                snakes[i].p[snakes[i].len]=snakes[i].p[snakes[i].len-1];
                snakes[i].len++;
            }
            spawnFood();
        }
        /* Simple body collision. Player respawns; bots respawn too. */
        for(int j=0;j<MAX_SNAKES;j++) if(snakes[j].alive) {
            int start=(i==j)?12:0;
            for(int k=start;k<snakes[j].len;k++) {
                if(dist2(snakes[i].p[0],snakes[j].p[k])<55.0f) {
                    if(i==0) initSnake(&snakes[i],i,0);
                    else initSnake(&snakes[i],i,1);
                    break;
                }
            }
        }
    }
}
static void drawPoint(float x,float y,unsigned int color,int size) {
    sceGuColor(color);
    sceGuPointSize(size);
    ScePspFVector2 v={x,y};
    sceGuDrawArray(GU_POINTS, GU_VERTEX_32BITF|GU_TRANSFORM_2D, 1, 0, &v);
}
static void drawScene(void) {
    sceGuStart(GU_DIRECT,list);
    sceGuClearColor(0xFF101820);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuShadeModel(GU_FLAT);

    Vec2 cam=snakes[0].p[0];
    /* Subtle arena grid. */
    sceGuColor(0xFF1C2A32);
    for(int x=-1200;x<=1200;x+=100) {
        ScePspFVector2 line[2]={{W/2.0f+x-cam.x,H/2.0f-cam.y-1200},
                               {W/2.0f+x-cam.x,H/2.0f-cam.y+1200}};
        sceGuDrawArray(GU_LINES,GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,0,line);
    }
    for(int y=-1200;y<=1200;y+=100) {
        ScePspFVector2 line[2]={{W/2.0f-cam.x-1200,H/2.0f+y-cam.y},
                               {W/2.0f-cam.x+1200,H/2.0f+y-cam.y}};
        sceGuDrawArray(GU_LINES,GU_VERTEX_32BITF|GU_TRANSFORM_2D,2,0,line);
    }
    /* Food dots. */
    for(int f=0;f<MAX_FOOD;f++) if(food[f].active) {
        float x=W/2.0f+food[f].p.x-cam.x, y=H/2.0f+food[f].p.y-cam.y;
        if(x>-5&&x<W+5&&y>-5&&y<H+5) drawPoint(x,y,0xFF55EEDD,5);
    }
    /* Snakes: draw tail first, head last. */
    for(int i=0;i<MAX_SNAKES;i++) if(snakes[i].alive) {
        for(int k=snakes[i].len-1;k>=0;k--) {
            float x=W/2.0f+snakes[i].p[k].x-cam.x;
            float y=H/2.0f+snakes[i].p[k].y-cam.y;
            if(x>-12&&x<W+12&&y>-12&&y<H+12)
                drawPoint(x,y,snakes[i].color,k==0?13:10);
        }
        float hx=W/2.0f, hy=H/2.0f;
        drawPoint(hx,hy,0xFFFFFFFF,5);
    }
    sceGuFinish();
    sceGuSync(0,0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}
int main(void) {
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    sceGuInit();
    sceGuStart(GU_DIRECT,list);
    sceGuDrawBuffer(GU_PSM_8888,(void*)0,512);
    sceGuDispBuffer(W,H,(void*)0x88000,512);
    sceGuOffset(2048-W/2,2048-H/2);
    sceGuViewport(2048,2048,W,H);
    sceGuDepthRange(0xc350,0x2710);
    sceGuScissor(0,0,W,H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuFinish(); sceGuSync(0,0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    srand(12345);
    resetGame();
    SceCtrlData pad;
    unsigned int last=sceKernelGetSystemTimeLow();
    while(running) {
        sceCtrlReadBufferPositive(&pad,1);
        if(pad.Buttons & PSP_CTRL_START) running=0;
        if(pad.Buttons & PSP_CTRL_SELECT) resetGame();
        unsigned int now=sceKernelGetSystemTimeLow();
        float dt=(now-last)/1000000.0f; last=now;
        if(dt>0.05f) dt=0.05f;
        updateGame(dt,&pad);
        drawScene();
    }
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
