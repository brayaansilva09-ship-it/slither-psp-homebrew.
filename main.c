/*
 * SLITHER PSP - v2.0
 * Homebrew independente para PSP / PPSSPP.
 * Inspirado na mecanica de cobras, comida e crescimento de jogos .io.
 * Controles: analógico/D-pad = virar; X = acelerar; START = pausar/voltar;
 * SELECT = reiniciar partida.
 */

#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

PSP_MODULE_INFO("Slither PSP", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SW 480
#define SH 272
#define BUF_W 512
#define PI 3.14159265358979323846f
#define MAX_FOOD 180
#define MAX_SNAKES 17
#define PATH_SIZE 1024
#define MAX_BODY 105
#define FOOD_R 3.0f

typedef struct { float x, y; } Vec2;
typedef struct { unsigned int color; short x, y, z; } Vertex;
typedef struct {
    float x, y, a, speed;
    int length, alive, bot, skin, pathAt;
    Vec2 path[PATH_SIZE];
} Snake;
typedef struct { float x, y; int active; unsigned int color; } Food;

static unsigned int __attribute__((aligned(16))) list[262144];
static Vertex circleVerts[40];
static Snake snakes[MAX_SNAKES];
static Food foods[MAX_FOOD];
static int screenMode = 0; /* 0 menu, 1 jogo, 2 game over, 3 pausa */
static int selectedSkin = 0;
static int menuOption = 0; /* 0 start, 1 skin, 2 nome, 3 idioma */
static int language = 0; /* 0 portugues, 1 ingles */
static int nameMode = 0;
static int nameCursor = 0;
static char playerName[13] = "PLAYER";
static const char nameChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
static int score = 0;
static int bestScore = 0;
static int frameNo = 0;
static int boostFlash = 0;
static unsigned int skins[] = {
    0xFF32D7FF, 0xFFFF4B4B, 0xFF48E080, 0xFFFFD34D,
    0xFFB56BFF, 0xFFFF8B35, 0xFF45E5D0, 0xFFF4F4F4
};
static const char *skinNames[] = {
    "AZUL", "VERMELHA", "VERDE", "AMARELA",
    "ROXA", "LARANJA", "TURQUESA", "BRANCA"
};

static float frand(float maxv) { return ((float)rand() / (float)RAND_MAX) * maxv; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float wrapAngle(float a) {
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}
static float dist2(float x1, float y1, float x2, float y2) {
    float dx=x1-x2, dy=y1-y2; return dx*dx+dy*dy;
}
static Vec2 worldToScreen(float x, float y) {
    Vec2 p = { x - snakes[0].x + SW*0.5f, y - snakes[0].y + SH*0.5f };
    return p;
}

static void drawPoly(Vertex *v, int n, int prim) {
    sceGuDrawArray(prim, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
                   n, 0, v);
}
static void rect(float x, float y, float w, float h, unsigned int c) {
    Vertex v[4] = {
        {c,(short)x,(short)y,0}, {c,(short)(x+w),(short)y,0},
        {c,(short)x,(short)(y+h),0}, {c,(short)(x+w),(short)(y+h),0}
    };
    drawPoly(v,4,GU_TRIANGLE_STRIP);
}
static void circle(float x, float y, float r, unsigned int c) {
    int i;
    circleVerts[0].color=c; circleVerts[0].x=(short)x;
    circleVerts[0].y=(short)y; circleVerts[0].z=0;
    for(i=0;i<=16;i++) {
        float a=(2.0f*PI*(float)i)/16.0f;
        circleVerts[i+1].color=c;
        circleVerts[i+1].x=(short)(x+cosf(a)*r);
        circleVerts[i+1].y=(short)(y+sinf(a)*r);
        circleVerts[i+1].z=0;
    }
    drawPoly(circleVerts,18,GU_TRIANGLE_FAN);
}
static void line(float x1,float y1,float x2,float y2,unsigned int c) {
    Vertex v[2]={{c,(short)x1,(short)y1,0},{c,(short)x2,(short)y2,0}};
    drawPoly(v,2,GU_LINES);
}
static void textAt(int x,int y,const char *s) {
    pspDebugScreenSetXY(x,y);
    pspDebugScreenPrintf("%s",s);
}
static void textCenter(int y,const char *s) {
    int n=(int)strlen(s);
    int x=(SW/8-n)/2;
    if(x<0)x=0;
    textAt(x,y,s);
}
static void drawBackground(void) {
    int x,y;
    rect(0,0,SW,SH,0xFF17121D);
    for(x=-((int)snakes[0].x%40);x<SW;x+=40)
        line((float)x,0,(float)x,SH,0xFF292333);
    for(y=-((int)snakes[0].y%40);y<SH;y+=40)
        line(0,(float)y,SW,(float)y,0xFF292333);
    /* pequenas marcas luminosas no campo */
    for(x=20;x<SW;x+=80) for(y=20;y<SH;y+=80)
        circle((float)x,(float)y,1.2f,0xFF3B3347);
}
static void spawnFood(int i,float cx,float cy) {
    foods[i].x=cx+frand(1250.0f)-625.0f;
    foods[i].y=cy+frand(1250.0f)-625.0f;
    foods[i].active=1;
    {
        static unsigned int fc[]={0xFFFFD84A,0xFFFF6E9D,0xFF55E9D0,
                                  0xFF8C78FF,0xFF7BE36B,0xFFFF9A42};
        foods[i].color=fc[rand()%6];
    }
}
static void initSnake(Snake *s,int bot,int skin,float x,float y) {
    int i;
    memset(s,0,sizeof(Snake));
    s->x=x; s->y=y; s->a=frand(2.0f*PI); s->speed=1.65f;
    s->length=bot?12:12; s->alive=1; s->bot=bot; s->skin=skin;
    s->pathAt=0;
    for(i=0;i<PATH_SIZE;i++) {
        s->path[i].x=x-cosf(s->a)*(float)i*1.8f;
        s->path[i].y=y-sinf(s->a)*(float)i*1.8f;
    }
}
static void resetGame(void) {
    int i;
    score=0; frameNo=0;
    initSnake(&snakes[0],0,selectedSkin,0,0);
    for(i=1;i<MAX_SNAKES;i++)
        initSnake(&snakes[i],1,(i+2)%8,frand(1900)-950,frand(1900)-950);
    for(i=0;i<MAX_FOOD;i++) spawnFood(i,0,0);
    screenMode=1;
}
static void addLength(Snake *s,int amount) {
    s->length+=amount;
    if(s->length>MAX_BODY)s->length=MAX_BODY;
}
static void steerToward(Snake *s,float targetX,float targetY,float turnRate) {
    float wanted=atan2f(targetY-s->y,targetX-s->x);
    float diff=wrapAngle(wanted-s->a);
    s->a=wrapAngle(s->a+clampf(diff,-turnRate,turnRate));
}
static void updateBot(Snake *s,int index) {
    int i;
    float tx=s->x+cosf(s->a)*180.0f, ty=s->y+sinf(s->a)*180.0f;
    float best=1.0e20f;
    /* procura comida visivel */
    for(i=0;i<MAX_FOOD;i++) if(foods[i].active) {
        float d=dist2(s->x,s->y,foods[i].x,foods[i].y);
        if(d<best && d<430.0f*430.0f) {
            best=d; tx=foods[i].x; ty=foods[i].y;
        }
    }
    /* tenta evitar cabecas e corpos proximos */
    for(i=0;i<MAX_SNAKES;i++) if(i!=index && snakes[i].alive) {
        float d=dist2(s->x,s->y,snakes[i].x,snakes[i].y);
        if(d<115.0f*115.0f) {
            float away=atan2f(s->y-snakes[i].y,s->x-snakes[i].x);
            tx=s->x+cosf(away)*230.0f;
            ty=s->y+sinf(away)*230.0f;
        }
    }
    steerToward(s,tx,ty,0.055f);
}
static void updateSnake(Snake *s,int isPlayer,int index,int boosting) {
    float speed=s->speed;
    if(isPlayer && boosting && s->length>12) speed=2.55f;
    if(!isPlayer) speed=1.35f+(float)(index%3)*0.12f;
    s->x+=cosf(s->a)*speed;
    s->y+=sinf(s->a)*speed;
    s->pathAt=(s->pathAt+1)%PATH_SIZE;
    s->path[s->pathAt].x=s->x;
    s->path[s->pathAt].y=s->y;
    if(isPlayer && boosting && s->length>12 && frameNo%9==0) s->length--;
}
static Vec2 getBodyPoint(Snake *s,int segment) {
    int back=(int)(segment*2.25f);
    int at=(s->pathAt-back)%PATH_SIZE;
    if(at<0)at+=PATH_SIZE;
    return s->path[at];
}
static void drawSnake(Snake *s,int isPlayer) {
    int i, count=s->length;
    unsigned int base=skins[s->skin];
    for(i=count-1;i>=0;i--) {
        Vec2 p=getBodyPoint(s,i);
        Vec2 q=worldToScreen(p.x,p.y);
        float r=(i==0)?9.0f:6.4f;
        unsigned int col=base;
        if(i%4==0) col=(base&0xFF000000)|(((base&0x00FEFEFE)>>1)+0x00202020);
        if(q.x>-20&&q.x<SW+20&&q.y>-20&&q.y<SH+20) {
            circle(q.x,q.y,r+1.4f,0xFF08080D);
            circle(q.x,q.y,r,col);
            if(i>0 && i%4==1) circle(q.x-1.0f,q.y-1.0f,1.5f,0x66FFFFFF);
            if(i==0) {
                float ex=q.x+cosf(s->a+0.75f)*4.0f;
                float ey=q.y+sinf(s->a+0.75f)*4.0f;
                circle(ex,ey,2.0f,0xFFFFFFFF);
                circle(q.x+cosf(s->a-0.75f)*4.0f,
                       q.y+sinf(s->a-0.75f)*4.0f,2.0f,0xFFFFFFFF);
            }
        }
    }
    if(isPlayer) {
        Vec2 p=worldToScreen(s->x,s->y);
        circle(p.x,p.y,13.0f,0x5533FFFF);
    }
}
static void drawFood(void) {
    int i;
    for(i=0;i<MAX_FOOD;i++) if(foods[i].active) {
        Vec2 p=worldToScreen(foods[i].x,foods[i].y);
        if(p.x>-8&&p.x<SW+8&&p.y>-8&&p.y<SH+8) {
            circle(p.x,p.y,5.0f,0x55333333);
            circle(p.x,p.y,FOOD_R,foods[i].color);
            circle(p.x-1.0f,p.y-1.0f,1.1f,0xAAFFFFFF);
        }
    }
}
static void eatFood(void) {
    int i,j;
    for(i=0;i<MAX_SNAKES;i++) if(snakes[i].alive) {
        for(j=0;j<MAX_FOOD;j++) if(foods[j].active) {
            if(dist2(snakes[i].x,snakes[i].y,foods[j].x,foods[j].y)<18.0f*18.0f) {
                foods[j].active=0;
                addLength(&snakes[i],1);
                if(i==0)score+=10;
                spawnFood(j,snakes[0].x,snakes[0].y);
            }
        }
    }
}
static int hitOtherBody(Snake *s,int selfIndex) {
    int i,j;
    for(i=0;i<MAX_SNAKES;i++) if(i!=selfIndex && snakes[i].alive) {
        for(j=3;j<snakes[i].length;j+=2) {
            Vec2 p=getBodyPoint(&snakes[i],j);
            if(dist2(s->x,s->y,p.x,p.y)<12.5f*12.5f)return 1;
        }
    }
    return 0;
}
static void collisions(void) {
    int i;
    for(i=1;i<MAX_SNAKES;i++) if(snakes[i].alive) {
        if(hitOtherBody(&snakes[i],i)) {
            snakes[i].alive=0;
        }
    }
    if(hitOtherBody(&snakes[0],0)) {
        if(score>bestScore)bestScore=score;
        screenMode=0; /* ao morrer, volta direto ao menu */
        nameMode=0;
    }
    for(i=1;i<MAX_SNAKES;i++) if(!snakes[i].alive && frameNo%30==0) {
        int j;
        for(j=0;j<snakes[i].length;j+=2) {
            Vec2 p=getBodyPoint(&snakes[i],j);
            int k=(j*7+i*13)%MAX_FOOD;
            foods[k].x=p.x; foods[k].y=p.y; foods[k].active=1;
            foods[k].color=0xFFFFD84A;
        }
        initSnake(&snakes[i],1,(i+2)%8,
                  snakes[0].x+frand(1500)-750,
                  snakes[0].y+frand(1500)-750);
    }
}
static void drawHud(void) {
    char buf[80];
    rect(8,8,148,34,0xAA090A12);
    sprintf(buf,"PONTOS: %d",score);
    textAt(2,1,buf);
    sprintf(buf,"TAMANHO: %d",snakes[0].length);
    textAt(2,2,buf);
    textAt(45,1,playerName);
    textAt(2,32,"X: TURBO   START: PAUSAR");
}
static void drawMenu(void) {
    int i;
    rect(0,0,SW,SH,0xFF11101A);
    for(i=0;i<16;i++) {
        float x=(float)((i*71+frameNo)%520)-20;
        float y=(float)((i*43+frameNo/2)%300)-10;
        circle(x,y,2.0f,0xFF302B40);
    }
    /* pequenas cobras decorativas */
    for(i=0;i<7;i++) circle(90.0f+i*9.0f,83.0f+sinf((float)i*0.7f)*5.0f,5.0f,skins[(i+selectedSkin)%8]);
    for(i=0;i<7;i++) circle(330.0f+i*9.0f,95.0f+cosf((float)i*0.7f)*5.0f,5.0f,skins[(i+3)%8]);
    rect(112,8,256,30,0xCC080812);
    rect(105,218,270,34,0xCC080812);
}
static void drawMenuText(void) {
    char b[80];
    textCenter(2,"SlitherPSP");
    textCenter(5, language ? "CHOOSE AN OPTION" : "ESCOLHA UMA OPCAO");
    sprintf(b,"%c START",menuOption==0?'>':' '); textCenter(12,b);
    sprintf(b,"%c SKIN: %s",menuOption==1?'>':' ',skinNames[selectedSkin]); textCenter(15,b);
    sprintf(b,"%c NOME: %s",menuOption==2?'>':' ',playerName); textCenter(18,b);
    sprintf(b,"%c IDIOMA: %s",menuOption==3?'>':' ',language?"ENGLISH":"PORTUGUES"); textCenter(21,b);
    if(nameMode) {
        textCenter(25,language?"EDIT NAME: LEFT/RIGHT, UP/DOWN, X OK":"EDITAR NOME: ESQ/DIR, CIMA/BAIXO, X OK");
    } else {
        textCenter(26,language?"ARROWS: SELECT   X: CONFIRM":"SETAS: ESCOLHER   X: CONFIRMAR");
    }
    textCenter(29,"Beta V2.2");
}
static void drawGameOver(void) {
    rect(75,65,330,140,0xEE080810);
    textCenter(11,"FIM DE JOGO!");
    {
        char b[80];
        sprintf(b,"PONTUACAO: %d",score); textCenter(15,b);
        sprintf(b,"RECORDE: %d",bestScore); textCenter(17,b);
    }
    textCenter(21,"X - JOGAR NOVAMENTE");
    textCenter(23,"START - MENU");
}
static void drawPause(void) {
    rect(105,80,270,105,0xEE080810);
    textCenter(13,"PAUSADO");
    textCenter(17,"START - CONTINUAR");
    textCenter(20,"SELECT - REINICIAR");
}
static int running=1;
int exitCallback(int arg1,int arg2,void *common) { running=0; return 0; }
int callbackThread(SceSize args,void *argp) {
    int cbid=sceKernelCreateCallback("Exit Callback",exitCallback,0);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}
int setupCallbacks(void) {
    int thid=sceKernelCreateThread("Callback Thread",callbackThread,0x11,0xFA0,0,0);
    if(thid>=0)sceKernelStartThread(thid,0,0);
    return thid;
}

int main(void) {
    SceCtrlData pad,old;
    int i;
    srand(0x51A7);
    setupCallbacks();
    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    sceGuInit();
    sceGuStart(GU_DIRECT,list);
    sceGuDrawBuffer(GU_PSM_8888,(void*)0,BUF_W);
    sceGuDispBuffer(SW,SH,(void*)0x88000,BUF_W);
    sceGuDepthBuffer((void*)0x110000,BUF_W);
    sceGuOffset(2048-(SW/2),2048-(SH/2));
    sceGuViewport(2048,2048,SW,SH);
    sceGuDepthRange(65535,0);
    sceGuScissor(0,0,SW,SH);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_SMOOTH);
    sceGuClearColor(0xFF17121D);
    sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);
    sceGuFinish();
    sceGuSync(0,0);
    sceGuDisplay(GU_TRUE);

    memset(&old,0,sizeof(old));
    for(i=0;i<MAX_FOOD;i++)spawnFood(i,0,0);

    while(running) {
        int pressed,boost=0;
        sceCtrlPeekBufferPositive(&pad,1);
        pressed=pad.Buttons & ~old.Buttons;
        frameNo++;

        if(screenMode==0) {
            if(nameMode) {
                int n=(int)strlen(playerName), ci=0, k;
                if(pressed&PSP_CTRL_LEFT) { if(nameCursor>0)nameCursor--; }
                if(pressed&PSP_CTRL_RIGHT) { if(nameCursor<n && n<12)nameCursor++; }
                if(pressed&PSP_CTRL_UP) {
                    char c= nameCursor<n ? playerName[nameCursor] : 'A';
                    for(k=0;nameChars[k];k++)if(nameChars[k]==c){ci=k;break;}
                    ci=(ci+1)%(int)(sizeof(nameChars)-1);
                    if(nameCursor<n)playerName[nameCursor]=nameChars[ci];
                    else if(n<12){playerName[n]=nameChars[ci];playerName[n+1]=0;}
                }
                if(pressed&PSP_CTRL_DOWN) {
                    char c= nameCursor<n ? playerName[nameCursor] : 'A';
                    for(k=0;nameChars[k];k++)if(nameChars[k]==c){ci=k;break;}
                    ci=(ci+(int)sizeof(nameChars)-2)%(int)(sizeof(nameChars)-1);
                    if(nameCursor<n)playerName[nameCursor]=nameChars[ci];
                    else if(n<12){playerName[n]=nameChars[ci];playerName[n+1]=0;}
                }
                if(pressed&PSP_CTRL_CROSS)nameMode=0;
                if(pressed&PSP_CTRL_CIRCLE) { if(n>0){playerName[n-1]=0;if(nameCursor>n-1)nameCursor=n-1;} }
            } else {
                if(pressed&PSP_CTRL_UP)menuOption=(menuOption+3)%4;
                if(pressed&PSP_CTRL_DOWN)menuOption=(menuOption+1)%4;
                if((pressed&PSP_CTRL_LEFT)||(pressed&PSP_CTRL_RIGHT)) {
                    if(menuOption==1)selectedSkin=(selectedSkin+1)%8;
                    if(menuOption==3)language=1-language;
                }
                if(pressed&PSP_CTRL_CROSS) {
                    if(menuOption==0)resetGame();
                    else if(menuOption==2){nameMode=1;nameCursor=(int)strlen(playerName);}
                    else if(menuOption==1)selectedSkin=(selectedSkin+1)%8;
                    else if(menuOption==3)language=1-language;
                }
            }
        } else if(screenMode==1) {
            float ax=((float)pad.Lx-128.0f)/128.0f;
            float ay=((float)pad.Ly-128.0f)/128.0f;
            if(fabsf(ax)>0.16f || fabsf(ay)>0.16f)
                steerToward(&snakes[0],snakes[0].x+ax*220.0f,
                            snakes[0].y+ay*220.0f,0.095f);
            else {
                if(pad.Buttons&PSP_CTRL_LEFT)snakes[0].a=wrapAngle(snakes[0].a-0.065f);
                if(pad.Buttons&PSP_CTRL_RIGHT)snakes[0].a=wrapAngle(snakes[0].a+0.065f);
                if(pad.Buttons&PSP_CTRL_UP)snakes[0].a=wrapAngle(snakes[0].a-0.045f);
                if(pad.Buttons&PSP_CTRL_DOWN)snakes[0].a=wrapAngle(snakes[0].a+0.045f);
            }
            boost=(pad.Buttons&PSP_CTRL_CROSS)!=0;
            updateSnake(&snakes[0],1,0,boost);
            for(i=1;i<MAX_SNAKES;i++)if(snakes[i].alive) {
                updateBot(&snakes[i],i);
                updateSnake(&snakes[i],0,i,0);
            }
            eatFood();
            collisions();
            if(pressed&PSP_CTRL_START)screenMode=3;
            if(pressed&PSP_CTRL_SELECT)resetGame();
        } else if(screenMode==2) {
            if(pressed&PSP_CTRL_CROSS)resetGame();
            if(pressed&PSP_CTRL_START)screenMode=0;
        } else if(screenMode==3) {
            if(pressed&PSP_CTRL_START)screenMode=1;
            if(pressed&PSP_CTRL_SELECT)resetGame();
        }

        sceGuStart(GU_DIRECT,list);
        sceGuClearColor(0xFF17121D);
        sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);

        if(screenMode==0) {
            drawMenu();
        } else {
            drawBackground();
            drawFood();
            for(i=1;i<MAX_SNAKES;i++)if(snakes[i].alive)drawSnake(&snakes[i],0);
            drawSnake(&snakes[0],1);
            sceGuFinish();
            sceGuSync(0,0);
            sceDisplayWaitVblankStart();
            sceGuSwapBuffers();
            pspDebugScreenSetBase((void*)((frameNo&1)?0:0x88000));
            pspDebugScreenClear();
            drawHud();
            if(screenMode==2)drawGameOver();
            if(screenMode==3)drawPause();
            old=pad;
            continue;
        }

        sceGuFinish();
        sceGuSync(0,0);
        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
        pspDebugScreenSetBase((void*)((frameNo&1)?0:0x88000));
        pspDebugScreenClear();
        if(screenMode==0) drawMenuText();
        old=pad;
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
