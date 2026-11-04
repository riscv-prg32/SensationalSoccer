#!/usr/bin/env python3
import json, pathlib

def sid(wave,pulse,cutoff,res): return 0x8000|(res<<10)|(cutoff<<6)|(pulse<<2)|wave
instruments=[
 {"sample_id":sid(0,8,10,1),"default_volume":115,"default_pan":-54,"attack":18,"decay":28,"sustain":155,"release":48},
 {"sample_id":sid(0,8,10,1),"default_volume":115,"default_pan":54,"attack":18,"decay":28,"sustain":155,"release":48},
 {"sample_id":sid(2,4,12,1),"default_volume":130,"default_pan":-30,"attack":2,"decay":15,"sustain":120,"release":20},
 {"sample_id":sid(2,11,12,1),"default_volume":130,"default_pan":30,"attack":2,"decay":15,"sustain":120,"release":20},
 {"sample_id":sid(1,8,8,2),"default_volume":80,"default_pan":-10,"attack":8,"decay":35,"sustain":90,"release":50},
 {"sample_id":sid(1,8,8,2),"default_volume":80,"default_pan":10,"attack":8,"decay":35,"sustain":90,"release":50},
 {"sample_id":sid(3,8,7,1),"default_volume":110,"default_pan":-45,"attack":0,"decay":8,"sustain":0,"release":8},
 {"sample_id":sid(3,8,14,1),"default_volume":125,"default_pan":45,"attack":0,"decay":6,"sustain":0,"release":9}]
E=[]
def ev(d,c,a=None,b=None):
 x={"delta":d,"command":c}
 if a is not None:x["arg0"]=a
 if b is not None:x["arg1"]=b
 E.append(x)
ev(0,"SET_TEMPO",112)
chords=[(48,55,60,64),(53,60,65,69),(55,62,67,71),(48,55,60,67)]
for bar,ch in enumerate(chords*4):
 for c in range(6): ev(0,"NOTE_OFF",c)
 for c,n in enumerate(ch): ev(0,"NOTE_ON",c,n+(12 if c in (2,3) else 0))
 ev(0,"NOTE_ON",4,ch[0]-12);ev(0,"NOTE_ON",5,ch[1]-12)
 ev(2,"NOTE_ON",6,70);ev(2,"NOTE_OFF",6);ev(0,"NOTE_ON",7,84)
 ev(2,"NOTE_OFF",7);ev(2,"NOTE_ON",6,74);ev(2,"NOTE_OFF",6)
for c in range(8):ev(0,"NOTE_OFF",c)
ev(8,"JUMP",0,0)
path=pathlib.Path(__file__).resolve().parents[1]/"assets/audio.json";path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps({"instruments":instruments,"tracks":[{"events":E}]},indent=2)+"\n")
