#include "prg32.h"
#include <stdint.h>

#define W 320
#define H 200
#define FP 16
#define SIDE 6
#define N (SIDE*2)
#define FW (960*FP)
#define FH (136*FP)
#define TOP 46
#define FPS 30

enum {SKY=16,SKY2,STAND0,STAND1,FANB,FANR,FANS,GRASS0,GRASS1,LINE,SHADOW,
 BLUE,BLUE2,BLUED,RED,RED2,REDD,SKIN,HAIR,WHITE,BLACK,YELLOW,BALLD,GOAL,HUD,FLASH};
typedef struct {int x,y,vx,vy;uint8_t team,role,face,anim,drawn,action,timer;} Player;
typedef struct {int x,y,z,vx,vy,vz,spin,owner;} Ball;
static Player p[N];static Ball ball;
static uint32_t frame,last_input;static int score[2],clock_frames,controlled[2];
enum {SCREEN_TITLE,SCREEN_TEAM,SCREEN_MODE,SCREEN_MATCH,SCREEN_RESULT};
static int attract,goal_flash,kickoff,camera_x,networked,screen,menu_cursor,mode,round_no,wins,celebrate;
static const char team_name[4][8]={"NAPOLI","LONDON","TOKYO","RIO"};
static const uint8_t team_rgb[4][3]={{25,91,220},{220,40,47},{242,190,24},{31,166,105}};
static int chosen[2]={0,1};

static int absi(int v){return v<0?-v:v;} static int sgn(int v){return(v>0)-(v<0);}
static int clamp(int v,int a,int b){return v<a?a:(v>b?b:v);}
static uint16_t rgb(int r,int g,int b){return(uint16_t)(((r&248)<<8)|((g&252)<<3)|(b>>3));}
static void box(int x,int y,int w,int h,uint8_t c){if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}if(x+w>W)w=W-x;if(y+h>H)h=H-y;if(w>0&&h>0)prg32_gfx_rect_indexed(x,y,w,h,c);}
static int sx(int x){return x/FP-camera_x;}static int sy(int y){return TOP+y/FP;}
static int distance(int x0,int y0,int x1,int y1){return absi(x1-x0)+absi(y1-y0);}

static void make_palette(void){
 prg32_palette_set(SKY,rgb(28,72,122));prg32_palette_set(SKY2,rgb(89,151,198));prg32_palette_set(STAND0,rgb(19,24,35));prg32_palette_set(STAND1,rgb(58,66,78));
 prg32_palette_set(FANB,rgb(48,133,238));prg32_palette_set(FANR,rgb(235,61,56));prg32_palette_set(FANS,rgb(232,177,127));prg32_palette_set(GRASS0,rgb(20,125,54));prg32_palette_set(GRASS1,rgb(24,146,62));
 prg32_palette_set(LINE,rgb(226,246,222));prg32_palette_set(SHADOW,rgb(11,83,39));prg32_palette_set(BLUE,rgb(25,91,220));prg32_palette_set(BLUE2,rgb(81,175,255));prg32_palette_set(BLUED,rgb(12,42,116));
 prg32_palette_set(RED,rgb(215,42,47));prg32_palette_set(RED2,rgb(255,111,72));prg32_palette_set(REDD,rgb(112,18,28));prg32_palette_set(SKIN,rgb(244,185,125));prg32_palette_set(HAIR,rgb(57,34,20));
 prg32_palette_set(WHITE,rgb(248,252,245));prg32_palette_set(BLACK,rgb(4,8,13));prg32_palette_set(YELLOW,rgb(255,224,48));prg32_palette_set(BALLD,rgb(50,55,57));prg32_palette_set(GOAL,rgb(180,218,224));prg32_palette_set(HUD,rgb(5,12,25));prg32_palette_set(FLASH,rgb(255,247,151));
}
static void formation(void){
 static const uint8_t xx[SIDE]={8,22,34,39,52,64},yy[SIDE]={50,18,78,39,62,50};
 for(int i=0;i<SIDE;i++){Player*a=&p[i],*b=&p[i+SIDE];a->team=0;a->role=i;a->x=xx[i]*FW/100;a->y=yy[i]*FH/100;a->vx=a->vy=0;a->face=1;a->action=a->timer=0;b->team=1;b->role=i;b->x=FW-xx[i]*FW/100;b->y=yy[i]*FH/100;b->vx=b->vy=0;b->face=0;b->action=b->timer=0;}
 ball.x=FW/2;ball.y=FH/2;ball.z=ball.vx=ball.vy=ball.vz=ball.spin=0;ball.owner=((score[0]+score[1])&1)?SIDE:0;controlled[0]=0;controlled[1]=SIDE;kickoff=35;
}
void sensational_soccer_init(void){
 make_palette();frame=last_input=0;score[0]=score[1]=clock_frames=0;attract=0;goal_flash=0;camera_x=320;screen=SCREEN_TITLE;menu_cursor=mode=round_no=wins=celebrate=0;formation();
 /* Regular daytime release volume; use 0 for Central European night tests. */
 prg32_audio_set_master_volume(160);prg32_audio_play_track(0);prg32_multiplayer_init();
 networked=prg32_multiplayer_available()&&prg32_multiplayer_join("sensational-soccer-v2",PRG32_MP_FLAG_ENABLE)==0;
 prg32_band_set_game_info("SENSATIONAL SOCCER / A SHOOT / B PASS");
}
static int nearest(int team,int x,int y){int first=team?SIDE:0,best=first,bd=1<<30;for(int i=first;i<first+SIDE;i++){int d=distance(p[i].x,p[i].y,x,y);if(d<bd){bd=d;best=i;}}return best;}
static void take_ball(int i){ball.owner=i;ball.z=ball.vx=ball.vy=ball.vz=ball.spin=0;}
static void kick(int i,int pass){Player*q=&p[i];int dir=q->team?-1:1,target=FH/2,power=pass?92:154;q->action=2;q->timer=9;if(pass){int mate=-1,best=1<<30;for(int j=q->team?SIDE:0;j<(q->team?N:SIDE);j++)if(j!=i){int f=dir*(p[j].x-q->x),v=distance(q->x,q->y,p[j].x,p[j].y)-f/2;if(f>0&&v<best){best=v;mate=j;}}if(mate>=0)target=p[mate].y;}ball.owner=-1;ball.x=q->x+dir*9*FP;ball.y=q->y;ball.z=4*FP;ball.vx=dir*power;ball.vy=clamp((target-ball.y)/12,-72,72);ball.vz=pass?16:42;ball.spin=q->vy/5;prg32_audio_note(6,6,(uint8_t)(44+power/12),160,(uint32_t)(55+power));prg32_audio_set_channel_pan(6,q->team?42:-42);}
static void move(int i,int ax,int ay){Player*q=&p[i];q->vx+=ax;q->vy+=ay;int speed=absi(q->vx)+absi(q->vy),limit=q->role?62:50;if(speed>limit){q->vx=q->vx*limit/speed;q->vy=q->vy*limit/speed;}q->x=clamp(q->x+q->vx,4*FP,FW-4*FP);q->y=clamp(q->y+q->vy,5*FP,FH-5*FP);q->vx=q->vx*13/16;q->vy=q->vy*13/16;if(ax)q->face=ax>0;if(q->timer)q->timer--;else q->action=ball.owner==i?1:0;q->anim=(absi(q->vx)+absi(q->vy)>8)?(frame>>2)&3:0;}
static void human(int team,uint32_t in,uint32_t pressed){int i=controlled[team],fx=ball.owner>=0?p[ball.owner].x:ball.x,fy=ball.owner>=0?p[ball.owner].y:ball.y;if(ball.owner!=i)controlled[team]=i=nearest(team,fx,fy);move(i,(((in&PRG32_BTN_RIGHT)!=0)-((in&PRG32_BTN_LEFT)!=0))*12,(((in&PRG32_BTN_DOWN)!=0)-((in&PRG32_BTN_UP)!=0))*10);if(ball.owner==i&&(pressed&PRG32_BTN_A))kick(i,0);if(ball.owner==i&&(pressed&PRG32_BTN_B))kick(i,1);}
static void ai(int i){Player*q=&p[i];int dir=q->team?-1:1;if(q->role==0&&ball.owner<0&&absi(ball.x-q->x)<120*FP){q->action=3;q->timer=12;move(i,clamp((ball.x-q->x)/45,-11,11),clamp((ball.y-q->y)/30,-10,10));return;}if(ball.owner==i){move(i,dir*8,sgn(FH/2-q->y)*3);int gd=absi((q->team?0:FW)-q->x);if(gd<175*FP||((frame+i*29)%121)==0)kick(i,gd>175*FP);return;}int chase=nearest(q->team,ball.x,ball.y),tx,ty;if(i==chase){tx=ball.x;ty=ball.y;}else{static const uint8_t lane[SIDE]={50,16,84,34,66,50};tx=clamp((q->team?FW:0)+dir*(55+q->role*72)*FP+(ball.x-FW/2)/3,20*FP,FW-20*FP);ty=lane[q->role]*FH/100;}move(i,clamp((tx-q->x)/90,-8,8),clamp((ty-q->y)/75,-7,7));}
static void collisions(void){for(int i=0;i<N;i++)for(int j=i+1;j<N;j++){int dx=p[j].x-p[i].x,dy=p[j].y-p[i].y;if(absi(dx)<9*FP&&absi(dy)<7*FP){int px=sgn(dx)*3,py=sgn(dy)*2;p[i].x-=px;p[j].x+=px;p[i].y-=py;p[j].y+=py;p[i].vx-=px;p[j].vx+=px;}}}
static void update_ball(void){if(ball.owner>=0){Player*q=&p[ball.owner];ball.x=q->x+(q->face?7:-7)*FP;ball.y=q->y+2*FP;ball.z=2*FP;return;}ball.x+=ball.vx;ball.y+=ball.vy;ball.z+=ball.vz;ball.vy+=ball.spin/20;ball.spin=ball.spin*31/32;ball.vx=ball.vx*63/64;ball.vy=ball.vy*63/64;ball.vz-=5;if(ball.z<0){ball.z=0;ball.vz=absi(ball.vz)>7?-ball.vz*9/16:0;ball.vx=ball.vx*61/64;ball.vy=ball.vy*61/64;}if(ball.y<2*FP||ball.y>FH-2*FP){ball.y=clamp(ball.y,2*FP,FH-2*FP);ball.vy=-ball.vy*3/4;ball.spin=-ball.spin;}if(ball.z<16*FP)for(int i=0;i<N;i++)if(distance(p[i].x,p[i].y,ball.x,ball.y)<11*FP){take_ball(i);controlled[p[i].team]=i;break;}if(ball.x<0||ball.x>FW){if(ball.y>FH/2-27*FP&&ball.y<FH/2+27*FP&&ball.z<28*FP){score[ball.x>FW?0:1]++;goal_flash=90;celebrate=90;for(int n=0;n<4;n++)prg32_audio_note((uint8_t)(4+n),7,(uint8_t)(72+n*3),120,180);}formation();}}
static uint32_t peer_input(void){if(!networked)return 0;prg32_player_state_t peer;int count=prg32_multiplayer_get_peer_count();for(int i=0;i<count;i++)if(prg32_multiplayer_get_peer(i,&peer)==0&&peer.player_id!=prg32_multiplayer_local_player_id())return peer.input;return 0;}
void sensational_soccer_update(void){
 uint32_t local=prg32_input_read(),pressed=local&~last_input,remote=peer_input();frame++;
 if(screen!=SCREEN_MATCH){
  if(screen==SCREEN_TITLE){if(pressed){screen=SCREEN_TEAM;menu_cursor=0;}else if(frame>FPS*8){screen=SCREEN_MATCH;attract=1;prg32_audio_stop_track();}}
  else if(screen==SCREEN_TEAM){if(pressed&PRG32_BTN_LEFT)chosen[0]=(chosen[0]+3)%4;if(pressed&PRG32_BTN_RIGHT)chosen[0]=(chosen[0]+1)%4;if(pressed&PRG32_BTN_UP)chosen[1]=(chosen[1]+3)%4;if(pressed&PRG32_BTN_DOWN)chosen[1]=(chosen[1]+1)%4;if(pressed&PRG32_BTN_A)screen=SCREEN_MODE;}
  else if(screen==SCREEN_MODE){if(pressed&(PRG32_BTN_UP|PRG32_BTN_DOWN))mode=!mode;if(pressed&PRG32_BTN_A){score[0]=score[1]=clock_frames=round_no=wins=0;attract=0;screen=SCREEN_MATCH;formation();prg32_audio_stop_track();}}
  else if(screen==SCREEN_RESULT&&pressed&PRG32_BTN_A){if(mode&&round_no<2){round_no++;score[0]=score[1]=clock_frames=0;screen=SCREEN_MATCH;formation();prg32_audio_stop_track();}else{screen=SCREEN_TITLE;frame=0;prg32_audio_play_track(0);}}
  last_input=local;return;
 }
 /* Gameplay is effects-only. Reassert this invariant so no tracker state can
    survive a menu transition, reconnect, or resumed championship round. */
 prg32_audio_stop_track();
 if(networked){prg32_multiplayer_set_input(local);prg32_multiplayer_set_local_state((int16_t)(p[controlled[0]].x/FP),(int16_t)(p[controlled[0]].y/FP),0,(uint16_t)score[0]);prg32_multiplayer_tick();}
 if(attract&&local){attract=0;score[0]=score[1]=clock_frames=0;formation();}if(pressed&PRG32_BTN_SELECT){screen=SCREEN_TITLE;frame=0;attract=0;prg32_audio_play_track(0);last_input=local;return;}
 if(kickoff)kickoff--;else{if(!attract)human(0,local,pressed);if(!attract&&remote)human(1,remote,remote);for(int i=0;i<N;i++){if(!attract&&i==controlled[0])continue;if(!attract&&remote&&i==controlled[1])continue;ai(i);}collisions();update_ball();clock_frames++;}
 if(goal_flash)goal_flash--;if(celebrate)celebrate--;if(attract&&clock_frames>FPS*75){score[0]=score[1]=clock_frames=0;formation();}if(!attract&&clock_frames>FPS*90){if(score[0]>score[1])wins++;screen=SCREEN_RESULT;prg32_audio_play_track(0);}int target=(ball.owner>=0?p[ball.owner].x:ball.x)/FP-W/2;target=clamp(target,0,FW/FP-W);camera_x+=(target-camera_x)/8;last_input=local;
}
static void stadium(void){prg32_gfx_clear_indexed(SKY);box(0,13,W,8,SKY2);box(0,21,W,23,STAND0);box(0,42,W,4,STAND1);for(int row=0;row<3;row++)for(int i=0;i<38;i++){int x=i*27+row*11-camera_x/3,y=23+row*6,jump=celebrate?((frame+i)&3):0;while(x<-8)x+=342;while(x>W)x-=342;uint8_t shirt=((i+row*5)&3)==0?FANR:(((i*3+row)&3)==0?FANB:STAND1);box(x,y-jump,3,2,FANS);box(x-1,y+2-jump,5,3,shirt);box(x,y+5-jump,1,2,BLACK);box(x+2,y+5-jump,1,2,BLACK);if(celebrate||(((frame>>3)+i+row)&1)){box(x-2,y-2-jump,1,5,shirt);box(x+4,y-2-jump,1,5,shirt);}}box(18-camera_x/4,22,76,3,WHITE);box(212-camera_x/4,22,82,3,YELLOW);for(int stripe=0;stripe<14;stripe++)box(stripe*72-camera_x,TOP,72,FH/FP,(stripe&1)?GRASS0:GRASS1);}
static void world_line(int x0,int y0,int x1,int y1,uint8_t c){int ax=sx(x0),ay=sy(y0),bx=sx(x1),by=sy(y1),steps=absi(bx-ax)+absi(by-ay);if(steps<1)steps=1;for(int i=0;i<=steps;i++)box(ax+(bx-ax)*i/steps,ay+(by-ay)*i/steps,2,2,c);}
static void markings(void){world_line(0,0,FW,0,LINE);world_line(0,FH,FW,FH,LINE);world_line(0,0,0,FH,LINE);world_line(FW,0,FW,FH,LINE);world_line(FW/2,0,FW/2,FH,LINE);int cx=sx(FW/2),cy=sy(FH/2);for(int a=-30;a<=30;a++){int xx=30-absi(a)*absi(a)/30;box(cx-xx,cy+a,2,2,LINE);box(cx+xx,cy+a,2,2,LINE);}world_line(0,FH/2-42*FP,55*FP,FH/2-42*FP,LINE);world_line(55*FP,FH/2-42*FP,55*FP,FH/2+42*FP,LINE);world_line(55*FP,FH/2+42*FP,0,FH/2+42*FP,LINE);world_line(FW,FH/2-42*FP,FW-55*FP,FH/2-42*FP,LINE);world_line(FW-55*FP,FH/2-42*FP,FW-55*FP,FH/2+42*FP,LINE);world_line(FW-55*FP,FH/2+42*FP,FW,FH/2+42*FP,LINE);int l=sx(0),r=sx(FW),t=sy(FH/2-28*FP),b=sy(FH/2+28*FP);box(l-5,t,5,b-t,GOAL);box(r,t,5,b-t,GOAL);for(int y=t;y<=b;y+=7){box(l-9,y,9,1,GOAL);box(r,y,9,1,GOAL);}}
static void player(int i){Player*q=&p[i];int x=sx(q->x),y=sy(q->y),step=(q->anim==1||q->anim==3)?2:0,kickleg=q->action==2?(q->face?5:-5):0,dive=q->action==3?(q->face?5:-5):0;uint8_t base=q->team?RED:BLUE,hi=q->team?RED2:BLUE2,dark=q->team?REDD:BLUED;if(q->role==0){base=YELLOW;hi=WHITE;dark=BLACK;}box(x-9,y+5,19,4,SHADOW);box(x-4+dive,y-20,8,6,SKIN);box(x-3+dive,y-21,7,2,HAIR);box(x-6+dive,y-14,12,12,base);box(x-5+dive,y-13,3,8,hi);box(x+3+dive,y-13,2,8,dark);box(x-9+dive,y-13,3,10,q->role?SKIN:WHITE);box(x+6+dive,y-13,3,10,q->role?SKIN:WHITE);box(x-5+dive,y-2,10,4,WHITE);box(x-5-step+dive,y+2,4,7,dark);box(x+1+step+kickleg+dive,y+2,4,7,dark);box(x-6-step+dive,y+8,5,2,BLACK);box(x+1+step+kickleg+dive,y+8,5,2,BLACK);if(q->action==1)box(x+(q->face?7:-9),y-1,3,3,WHITE);if(!attract&&(i==controlled[0]||i==controlled[1]))box(x-4,y-26,8,2,i==controlled[0]?BLUE2:RED2);}
static void draw_ball(void){int x=sx(ball.x),ground=sy(ball.y),y=ground-ball.z/FP;box(x-4,ground+5,9,3,SHADOW);box(x-3,y-3,7,7,WHITE);box(x-1,y-2,3,3,BALLD);box(x+2,y+1,2,2,BALLD);}
static void digit(int x,int y,int n,uint8_t c){char t[2]={(char)('0'+n%10),0};prg32_gfx_text8(x,y,t,prg32_palette_get(c),prg32_palette_get(HUD));}
void sensational_soccer_draw(void){
 if(screen!=SCREEN_MATCH){prg32_gfx_clear_indexed(HUD);box(0,0,W,200,HUD);box(22,20,276,5,BLUE2);box(22,175,276,5,RED2);prg32_gfx_text8(72,42,"SENSATIONAL SOCCER",prg32_palette_get(WHITE),prg32_palette_get(HUD));
  if(screen==SCREEN_TITLE){prg32_gfx_text8(92,76,"WORLD TOUR",prg32_palette_get(YELLOW),prg32_palette_get(HUD));prg32_gfx_text8(66,118,"PRESS ANY BUTTON",prg32_palette_get(WHITE),prg32_palette_get(HUD));prg32_gfx_text8(67,144,"OR WAIT FOR DEMO",prg32_palette_get(SKY2),prg32_palette_get(HUD));}
  else if(screen==SCREEN_TEAM){prg32_palette_set(BLUE, rgb(team_rgb[chosen[0]][0],team_rgb[chosen[0]][1],team_rgb[chosen[0]][2]));prg32_palette_set(RED,rgb(team_rgb[chosen[1]][0],team_rgb[chosen[1]][1],team_rgb[chosen[1]][2]));prg32_gfx_text8(94,72,"CHOOSE TEAMS",prg32_palette_get(YELLOW),prg32_palette_get(HUD));prg32_gfx_text8(38,103,"HOME",prg32_palette_get(BLUE),prg32_palette_get(HUD));prg32_gfx_text8(38,120,team_name[chosen[0]],prg32_palette_get(WHITE),prg32_palette_get(HUD));prg32_gfx_text8(200,103,"AWAY",prg32_palette_get(RED),prg32_palette_get(HUD));prg32_gfx_text8(200,120,team_name[chosen[1]],prg32_palette_get(WHITE),prg32_palette_get(HUD));prg32_gfx_text8(66,151,"ARROWS CHANGE  A OK",prg32_palette_get(SKY2),prg32_palette_get(HUD));}
  else if(screen==SCREEN_MODE){prg32_gfx_text8(104,75,"MATCH MODE",prg32_palette_get(YELLOW),prg32_palette_get(HUD));prg32_gfx_text8(82,105,mode?"  SINGLE MATCH":"* SINGLE MATCH",prg32_palette_get(WHITE),prg32_palette_get(HUD));prg32_gfx_text8(82,125,mode?"* CHAMPIONSHIP":"  CHAMPIONSHIP",prg32_palette_get(WHITE),prg32_palette_get(HUD));}
  else{prg32_gfx_text8(112,74,score[0]>score[1]?"YOU WIN!":(score[0]<score[1]?"FULL TIME":"DRAW"),prg32_palette_get(YELLOW),prg32_palette_get(HUD));prg32_gfx_text8(88,108,team_name[chosen[0]],prg32_palette_get(BLUE),prg32_palette_get(HUD));digit(155,108,score[0],WHITE);digit(175,108,score[1],WHITE);prg32_gfx_text8(196,108,team_name[chosen[1]],prg32_palette_get(RED),prg32_palette_get(HUD));prg32_gfx_text8(78,148,"PRESS A TO CONTINUE",prg32_palette_get(WHITE),prg32_palette_get(HUD));}return;}
 stadium();markings();for(int i=0;i<N;i++)p[i].drawn=0;for(int pass=0;pass<N;pass++){int next=-1,best=1<<30;for(int i=0;i<N;i++)if(!p[i].drawn&&p[i].y<best){best=p[i].y;next=i;}if(next>=0){player(next);p[next].drawn=1;}}draw_ball();box(0,0,W,13,HUD);prg32_gfx_text8(6,2,team_name[chosen[0]],prg32_palette_get(WHITE),prg32_palette_get(HUD));digit(126,2,score[0],BLUE2);prg32_gfx_text8(143,2,"-",prg32_palette_get(WHITE),prg32_palette_get(HUD));digit(158,2,score[1],RED2);prg32_gfx_text8(174,2,team_name[chosen[1]],prg32_palette_get(WHITE),prg32_palette_get(HUD));int sec=(clock_frames/FPS)%60,min=(clock_frames/(FPS*60))%10;char tm[6]={(char)('0'+min),':',(char)('0'+sec/10),(char)('0'+sec%10),0};prg32_gfx_text8(265,2,tm,prg32_palette_get(WHITE),prg32_palette_get(HUD));if(attract){box(55,154,210,33,HUD);prg32_gfx_text8(86,159,"DEMO: FULL AI",prg32_palette_get(YELLOW),prg32_palette_get(HUD));prg32_gfx_text8(68,174,"KEEPERS ACTIVE",prg32_palette_get(WHITE),prg32_palette_get(HUD));}else prg32_gfx_text8(4,190,"MOVE A SHOOT B PASS SELECT MENU",prg32_palette_get(WHITE),prg32_palette_get(GRASS0));if(goal_flash){box(77,77,166,38,goal_flash&4?FLASH:HUD);prg32_gfx_text8(96,90,"GOAL! CROWD ROARS",prg32_palette_get(goal_flash&4?HUD:FLASH),prg32_palette_get(goal_flash&4?FLASH:HUD));}}
