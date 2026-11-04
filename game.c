#include "prg32.h"
#include <stdint.h>

#define W 320
#define H 200
#define PLAYERS 10
#define FP 16
#define FIELD_W (280 * FP)
#define FIELD_H (150 * FP)

enum { SKY=16, CROWD0, CROWD1, CROWD2, PITCH0, PITCH1, LINE, SHADOW,
       BLUE, BLUE_HI, RED, RED_HI, SKIN, BLACK, WHITE, YELLOW, BALL_DARK,
       GOAL, HUD, FLASH };

typedef struct { int x,y,vx,vy; uint8_t team, role; } Player;
typedef struct { int x,y,vx,vy,owner; } Ball;
static Player p[PLAYERS];
static Ball ball;
static uint32_t frame,last_input;
static int blue_score,red_score,clock_frames,controlled,attract,goal_flash;
static int kickoff_wait;

static int abs_i(int x){ return x<0?-x:x; }
static int clamp_i(int x,int a,int b){ return x<a?a:(x>b?b:x); }
static uint16_t rgb565(int r,int g,int b){return (uint16_t)(((r&248)<<8)|((g&252)<<3)|(b>>3));}
static int dist1(int x0,int y0,int x1,int y1){ return abs_i(x1-x0)+abs_i(y1-y0); }
static void rect(int x,int y,int w,int h,uint8_t c){
  if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;} if(x+w>W)w=W-x;if(y+h>H)h=H-y;
  if(w>0&&h>0)prg32_gfx_rect_indexed(x,y,w,h,c);
}

/* Elevated television camera: world y controls trapezoid width and scale. */
static int sy(int y){ return 35 + (y * 153) / FIELD_H; }
static int sx(int x,int y){ int yy=sy(y); int half=102+(yy-35)*58/153; return 160+((x-FIELD_W/2)*half)/(FIELD_W/2); }
static int scale_at(int y){ return 7 + sy(y)/28; }

static void palette(void){
  prg32_palette_set(SKY,rgb565(10,18,38)); prg32_palette_set(CROWD0,rgb565(22,31,55));
  prg32_palette_set(CROWD1,rgb565(38,75,116)); prg32_palette_set(CROWD2,rgb565(225,71,66));
  prg32_palette_set(PITCH0,rgb565(16,112,55)); prg32_palette_set(PITCH1,rgb565(20,132,63));
  prg32_palette_set(LINE,rgb565(218,246,220)); prg32_palette_set(SHADOW,rgb565(8,65,35));
  prg32_palette_set(BLUE,rgb565(28,95,235)); prg32_palette_set(BLUE_HI,rgb565(92,180,255));
  prg32_palette_set(RED,rgb565(224,42,47)); prg32_palette_set(RED_HI,rgb565(255,125,80));
  prg32_palette_set(SKIN,rgb565(244,181,119)); prg32_palette_set(BLACK,rgb565(3,7,12));
  prg32_palette_set(WHITE,rgb565(248,252,245)); prg32_palette_set(YELLOW,rgb565(255,224,50));
  prg32_palette_set(BALL_DARK,rgb565(48,52,55)); prg32_palette_set(GOAL,rgb565(174,212,220));
  prg32_palette_set(HUD,rgb565(5,12,25)); prg32_palette_set(FLASH,rgb565(255,250,160));
}

static void formation(void){
  static const uint8_t fx[5]={18,35,48,61,76}, fy[5]={50,22,76,38,62};
  for(int i=0;i<5;i++){
    p[i].team=0;p[i].role=i;p[i].x=fx[i]*FIELD_W/100;p[i].y=fy[i]*FIELD_H/100;p[i].vx=p[i].vy=0;
    p[i+5].team=1;p[i+5].role=i;p[i+5].x=FIELD_W-fx[i]*FIELD_W/100;p[i+5].y=fy[i]*FIELD_H/100;p[i+5].vx=p[i+5].vy=0;
  }
  ball.x=FIELD_W/2;ball.y=FIELD_H/2;ball.vx=ball.vy=0;ball.owner=(blue_score+red_score)&1?5:0;
  controlled=0;kickoff_wait=35;
}

static void crowd_music(void){ prg32_audio_set_master_volume(185); prg32_audio_play_track(0); }
static void whistle(void){ prg32_audio_note(7,7,88,210,150); }
static void kick_sound(int team){ prg32_audio_note(6,6,48+(uint8_t)(frame&3),150,65); prg32_audio_set_channel_pan(6,team?38:-38); }

void sensational_soccer_init(void){
  palette();frame=last_input=0;blue_score=red_score=clock_frames=0;attract=1;goal_flash=0;
  formation();crowd_music();prg32_band_set_game_info("SENSATIONAL SOCCER  /  A KICK  B PASS");
}

static int nearest(int team,int x,int y){ int best=team?5:0,bd=1<<30; for(int i=team?5:0;i<(team?10:5);i++){int d=dist1(p[i].x,p[i].y,x,y);if(d<bd){bd=d;best=i;}}return best; }
static void take_ball(int i){ ball.owner=i;ball.vx=ball.vy=0; }
static void shoot(int i,int pass){
  int dir=p[i].team?-1:1; int target_y;
  if(pass){ int mate=-1,score=1<<30; for(int j=p[i].team?5:0;j<(p[i].team?10:5);j++)if(j!=i){int forward=dir*(p[j].x-p[i].x);int d=dist1(p[i].x,p[i].y,p[j].x,p[j].y)-forward/2;if(forward>0&&d<score){score=d;mate=j;}} target_y=mate>=0?p[mate].y:FIELD_H/2; }
  else target_y=FIELD_H/2+((int)(prg32_random_number(0,32))-16)*FP;
  ball.owner=-1;ball.x=p[i].x+dir*8*FP;ball.y=p[i].y;
  ball.vx=dir*(pass?75:120);ball.vy=clamp_i((target_y-ball.y)/(pass?18:22),-65,65);kick_sound(p[i].team);
}

static void move_player(int i,int tx,int ty,int speed){
  int dx=tx-p[i].x,dy=ty-p[i].y; int m=abs_i(dx)+abs_i(dy); if(m>0){p[i].vx=dx*speed/m;p[i].vy=dy*speed/m;}
  p[i].x=clamp_i(p[i].x+p[i].vx,0,FIELD_W);p[i].y=clamp_i(p[i].y+p[i].vy,5*FP,FIELD_H-5*FP);
}

static void ai_player(int i){
  int team=p[i].team,dir=team?-1:1; int has=ball.owner==i;
  if(has){
    int goalx=team?0:FIELD_W; move_player(i,goalx,FIELD_H/2,42);
    if(abs_i(goalx-p[i].x)<72*FP || ((frame+i*17)%95)==0) shoot(i,abs_i(goalx-p[i].x)>=72*FP);
    return;
  }
  int chaser=nearest(team,ball.x,ball.y);
  if(i==chaser) move_player(i,ball.x,ball.y,40);
  else { int home_x=(team?FIELD_W:0)+dir*(25+p[i].role*12)*FP; int home_y=(18+p[i].role*16)*FIELD_H/100;
    int flow=(ball.x-FIELD_W/2)/4;move_player(i,clamp_i(home_x+flow,15*FP,FIELD_W-15*FP),home_y,25); }
}

static void update_ball(void){
  if(ball.owner>=0){ ball.x=p[ball.owner].x+(p[ball.owner].team?-7:7)*FP;ball.y=p[ball.owner].y;return; }
  ball.x+=ball.vx;ball.y+=ball.vy;ball.vx=ball.vx*61/64;ball.vy=ball.vy*61/64;
  if(ball.y<3*FP||ball.y>FIELD_H-3*FP){ball.y=clamp_i(ball.y,3*FP,FIELD_H-3*FP);ball.vy=-ball.vy;}
  for(int i=0;i<PLAYERS;i++)if(dist1(p[i].x,p[i].y,ball.x,ball.y)<12*FP){take_ball(i);controlled=nearest(0,ball.x,ball.y);break;}
  if(ball.x<0||ball.x>FIELD_W){
    if(ball.y>FIELD_H/2-28*FP&&ball.y<FIELD_H/2+28*FP){ if(ball.x>FIELD_W)blue_score++;else red_score++;goal_flash=75;whistle(); }
    formation();
  }
}

void sensational_soccer_update(void){
  uint32_t in=prg32_input_read(),pressed=in&~last_input;frame++;
  if(attract&&in){ attract=0;blue_score=red_score=clock_frames=0;formation();whistle(); }
  if(pressed&PRG32_BTN_SELECT){attract=!attract;formation();}
  if(kickoff_wait>0)kickoff_wait--;else{
    if(!attract){
      controlled=nearest(0,ball.owner>=0?p[ball.owner].x:ball.x,ball.owner>=0?p[ball.owner].y:ball.y);
      int dx=((in&PRG32_BTN_RIGHT)?1:0)-((in&PRG32_BTN_LEFT)?1:0);
      int dy=((in&PRG32_BTN_DOWN)?1:0)-((in&PRG32_BTN_UP)?1:0);
      if(dx||dy)move_player(controlled,p[controlled].x+dx*80*FP,p[controlled].y+dy*80*FP,52);else ai_player(controlled);
      if(ball.owner==controlled&&(pressed&PRG32_BTN_A))shoot(controlled,0);
      if(ball.owner==controlled&&(pressed&PRG32_BTN_B))shoot(controlled,1);
    }
    for(int i=0;i<PLAYERS;i++)if(attract||i!=controlled)ai_player(i);
    update_ball();clock_frames++;
  }
  if(goal_flash>0)goal_flash--;
  if(attract&&clock_frames>30*75){blue_score=red_score=clock_frames=0;formation();}
  last_input=in;
}

static void line_world(int x0,int y0,int x1,int y1,uint8_t c){
  int ax=sx(x0,y0),ay=sy(y0),bx=sx(x1,y1),by=sy(y1),steps=abs_i(by-ay)+1;
  for(int i=0;i<=steps;i++){int y=ay+(by-ay)*i/steps;int x=ax+(bx-ax)*i/steps;rect(x,y,2,2,c);}
}
static void draw_field(void){
  prg32_gfx_clear_indexed(SKY);rect(0,13,W,22,CROWD0);
  for(int i=0;i<80;i++){int x=(i*47)%W,y=16+(i*29)%16;rect(x,y,2,2,((i+(frame>>3))&3)?CROWD1:CROWD2);}
  for(int y=0;y<FIELD_H;y+=10*FP){int y2=y+10*FP;int l=sx(0,y),r=sx(FIELD_W,y),yy=sy(y),yy2=sy(y2);rect(l,yy,r-l+1,yy2-yy+1,((y/(10*FP))&1)?PITCH0:PITCH1);}
  line_world(0,0,FIELD_W,0,LINE);line_world(0,FIELD_H,FIELD_W,FIELD_H,LINE);
  line_world(0,0,0,FIELD_H,LINE);line_world(FIELD_W,0,FIELD_W,FIELD_H,LINE);
  line_world(FIELD_W/2,0,FIELD_W/2,FIELD_H,LINE);
  int cy=sy(FIELD_H/2),cx=sx(FIELD_W/2,FIELD_H/2);for(int a=-22;a<=22;a++){int yy=cy+a/2;int xx=18-abs_i(a)*7/10;rect(cx-xx,yy,2,2,LINE);rect(cx+xx,yy,2,2,LINE);}
  line_world(0,FIELD_H/2-40*FP,38*FP,FIELD_H/2-40*FP,LINE);line_world(38*FP,FIELD_H/2-40*FP,38*FP,FIELD_H/2+40*FP,LINE);line_world(38*FP,FIELD_H/2+40*FP,0,FIELD_H/2+40*FP,LINE);
  line_world(FIELD_W,FIELD_H/2-40*FP,FIELD_W-38*FP,FIELD_H/2-40*FP,LINE);line_world(FIELD_W-38*FP,FIELD_H/2-40*FP,FIELD_W-38*FP,FIELD_H/2+40*FP,LINE);line_world(FIELD_W-38*FP,FIELD_H/2+40*FP,FIELD_W,FIELD_H/2+40*FP,LINE);
}
static void draw_player(int i){
  int x=sx(p[i].x,p[i].y),y=sy(p[i].y),s=scale_at(p[i].y);uint8_t base=p[i].team?RED:BLUE,hi=p[i].team?RED_HI:BLUE_HI;
  rect(x-s,y+2,s*2,3,SHADOW);rect(x-s/2,y-s-2,s,4,SKIN);rect(x-s/2,y-s+2,s,7,base);rect(x-s/2,y-s+2,2,5,hi);rect(x-s/2,y+9-s,3,5,WHITE);rect(x+s/2-3,y+9-s,3,5,WHITE);
  if(!attract&&i==controlled){rect(x-3,y-s-7,6,2,YELLOW);}
}
static void draw_ball(void){int x=sx(ball.x,ball.y),y=sy(ball.y);rect(x-3,y-2,7,3,SHADOW);rect(x-2,y-6,5,5,WHITE);rect(x,y-4,2,2,BALL_DARK);}
static void digit(int x,int y,int n,uint16_t c){char t[2]={(char)('0'+n%10),0};prg32_gfx_text8(x,y,t,c,prg32_palette_get(HUD));}

void sensational_soccer_draw(void){
  draw_field();for(int pass=0;pass<2;pass++)for(int i=0;i<PLAYERS;i++)if((p[i].y>ball.y)==pass)draw_player(i);draw_ball();
  rect(0,0,W,13,HUD);prg32_gfx_text8(6,2,"SENSATIONAL",prg32_palette_get(WHITE),prg32_palette_get(HUD));
  digit(126,2,blue_score,prg32_palette_get(BLUE_HI));prg32_gfx_text8(143,2,"-",prg32_palette_get(WHITE),prg32_palette_get(HUD));digit(158,2,red_score,prg32_palette_get(RED_HI));
  int sec=(clock_frames/30)%60,min=(clock_frames/1800)%10;char tm[6]={(char)('0'+min),':',(char)('0'+sec/10),(char)('0'+sec%10),0};prg32_gfx_text8(265,2,tm,prg32_palette_get(WHITE),prg32_palette_get(HUD));
  if(attract){rect(58,157,204,31,HUD);prg32_gfx_text8(86,162,"DEMO MATCH",prg32_palette_get(YELLOW),prg32_palette_get(HUD));prg32_gfx_text8(68,176,"PRESS ANY BUTTON",prg32_palette_get(WHITE),prg32_palette_get(HUD));}
  else prg32_gfx_text8(5,190,"STICK MOVE  A SHOOT  B PASS",prg32_palette_get(WHITE),prg32_palette_get(PITCH0));
  if(goal_flash){rect(75,76,170,38,goal_flash&4?FLASH:HUD);prg32_gfx_text8(120,89,"GOAL!",prg32_palette_get(goal_flash&4?HUD:FLASH),prg32_palette_get(goal_flash&4?FLASH:HUD));}
}
