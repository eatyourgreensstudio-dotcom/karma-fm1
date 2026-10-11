#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The firmware's UI and sound in the browser, no hardware (tests/op_emu.c behind a small web server):
  tools/op_emu.py [PORT]        then open http://localhost:8790/
Builds tests/op_emu.c with the host compiler (needs build/gen: tools/build.py makes it). The page
shows the screen, plays the audio and sends buttons, keys and knobs; each reload of the page restarts
the firmware (and rebuilds it, so an edit of the sources shows after a reload)."""
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "build" / "host" / "op_emu"
SCREEN = 240 * 240 * 2
FRAME_PCM = 22 * 32 * 4                 # 22 blocks of CTL = 32 stereo 16 bit samples

lock = threading.Lock()
proc = None
build_log = ""


def restart():
    """rebuild and start the firmware again; the compiler's words if it fails"""
    global proc, build_log
    if proc:
        proc.kill()
        proc = None
    EXE.parent.mkdir(parents=True, exist_ok=True)
    r = subprocess.run(["cc", "-O2", "-w", "-Ibuild/gen", "-Ifirmware/src", "-Ifirmware/hal", "-Itests", "-o", str(EXE),
                        "tests/op_emu.c", "-lm"], cwd=ROOT, capture_output=True, text=True)
    build_log = r.stderr
    if r.returncode == 0:
        proc = subprocess.Popen([str(EXE)], cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE)


def step(cmds, n):
    with lock:
        if not proc or proc.poll() is not None:
            return None
        proc.stdin.write((cmds + f"\nF {n}\n").encode())
        proc.stdin.flush()
        return proc.stdout.read(SCREEN + n * FRAME_PCM)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def reply(self, code, kind, body):
        self.send_response(code)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.split("?")[0] != "/":
            return self.reply(404, "text/plain", b"not found")
        with lock:
            restart()
        self.reply(200, "text/html; charset=utf-8", PAGE.replace("__BUILD_LOG__", build_log.replace("<", "&lt;")).encode())

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", "0"))).decode()
        lines = [l for l in body.splitlines() if l[:1] in "BKE" and len(l) < 24]
        n = max(1, min(8, int(self.path.rsplit("=", 1)[-1]) if "=" in self.path else 1))
        out = step("\n".join(lines), n)
        if out is None:
            return self.reply(503, "text/plain", b"firmware not running")
        self.reply(200, "application/octet-stream", out)


PAGE = r"""<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FM-1 preview</title>
<style>
body{margin:0;background:#1b1b1f;color:#c8c8cf;font:13px/1.4 -apple-system,system-ui,sans-serif;display:flex;justify-content:center;padding:16px}
#dev{background:#2a2a30;border-radius:14px;padding:16px;width:760px;max-width:100%;box-sizing:border-box}
#top{display:flex;gap:16px;flex-wrap:wrap}
canvas{width:480px;height:480px;max-width:100%;image-rendering:pixelated;background:#000;border-radius:4px}
#side{flex:1;min-width:200px;display:flex;flex-direction:column;gap:12px}
.row{display:flex;flex-wrap:wrap;gap:6px}
button{background:#3a3a42;color:#e6e6ec;border:1px solid #55555f;border-radius:6px;padding:7px 0;width:56px;font:inherit;cursor:pointer;user-select:none}
button.down{background:#e6e6ec;color:#000}
.knob{width:56px;text-align:center;cursor:ew-resize;user-select:none}
.knob i{display:block;width:40px;height:40px;margin:0 auto 2px;border-radius:50%;border:3px solid var(--c,#8a8a94);box-sizing:border-box;position:relative}
.knob i::after{content:"";position:absolute;left:50%;top:3px;width:2px;height:12px;background:var(--c,#8a8a94);transform-origin:1px 15px;transform:rotate(var(--a,0deg))}
#keys{position:relative;height:120px;margin-top:16px;user-select:none}
.w,.b{position:absolute;top:0;border-radius:0 0 4px 4px;cursor:pointer;box-sizing:border-box}
.w{background:#e9e9ee;border:1px solid #2a2a30;height:120px;color:#55555f;font-size:11px;display:flex;align-items:flex-end;justify-content:center;padding-bottom:4px}
.b{background:#101014;height:72px;z-index:1}
.w.down{background:#9db4ff}.b.down{background:#4a5cff}
#help{margin-top:12px;color:#8a8a94}
pre{color:#ff8a8a;white-space:pre-wrap}
</style></head><body><div id="dev">
<div id="top"><canvas id="lcd" width="240" height="240"></canvas>
<div id="side">
<div class="row" id="snd"><button id="audio" style="width:auto;padding:7px 12px">Sound on</button></div>
<div class="row" id="enc1"></div><div class="row" id="enc2"></div>
<div class="row" id="btn1"></div><div class="row" id="btn2"></div><div class="row" id="btn3"></div>
</div></div>
<div id="keys"></div>
<div id="help">Space: play. Left / right arrows: track knob. 1 to 4: turn a knob up (with Shift: down). Q to I and A to K: the 16 white keys.
Scroll or drag on a knob to turn it. Shift-click a button to keep it held; click again to let go. Reload the page to restart.</div>
<pre>__BUILD_LOG__</pre>
</div>
<script>
const SPF=704/44100, cv=document.getElementById('lcd'), g=cv.getContext('2d'), img=g.createImageData(240,240);
let q=[], busy=false, actx=null, next=0, simT=performance.now()/1000;
const clock=()=>actx?actx.currentTime:performance.now()/1000;
function send(c){q.push(c);}
async function pump(){
  if(busy)return; const now=clock(); if(simT<now-0.5)simT=now;
  if(simT>=now+0.09)return;
  const n=Math.min(6,Math.max(1,Math.ceil((now+0.09-simT)/SPF))); busy=true;
  try{
    let cut=q.length; const dn={};              // a press and its release never in one batch: the firmware must see it held a frame
    for(let i=0;i<q.length;i++){const m=q[i].split(' ');if(m[0]!=='E'){const k=m[0]+m[1];if(m[2]==='1')dn[k]=1;else if(dn[k]){cut=i;break;}}}
    const r=await fetch('/step?n='+n,{method:'POST',body:q.splice(0,cut).join('\n')}); if(!r.ok)throw 0;
    const buf=await r.arrayBuffer(), px=new Uint16Array(buf,0,57600), d=img.data;
    for(let i=0,j=0;i<57600;i++,j+=4){const p=px[i];d[j]=(p>>11)*255/31;d[j+1]=((p>>5)&63)*255/63;d[j+2]=(p&31)*255/31;d[j+3]=255;}
    g.putImageData(img,0,0);
    if(actx){
      const s=new Int16Array(buf,115200), len=s.length/2, ab=actx.createBuffer(2,len,44100), L=ab.getChannelData(0), R=ab.getChannelData(1);
      for(let i=0;i<len;i++){L[i]=s[2*i]/32768;R[i]=s[2*i+1]/32768;}
      const src=actx.createBufferSource(); src.buffer=ab; src.connect(actx.destination);
      if(next<actx.currentTime+0.01)next=actx.currentTime+0.03;
      src.start(next); next+=len/44100;
    }
    simT+=n*SPF;
  }catch(e){}
  busy=false;
}
setInterval(pump,12);
document.getElementById('audio').onclick=e=>{ if(!actx){actx=new AudioContext();simT=actx.currentTime;next=0;e.target.textContent='Sound is on';} };
const BT=['FX','SEL','ENV','LFO','EDIT','GLO','HOME','SAVE','ARP','SEQ','PLAY','REC','OCT-','OCT+'];
const rows={btn1:[0,1,2,3],btn2:[4,8,9,5],btn3:[6,7,10,11,12,13]}, bel={};
for(const r in rows)for(const id of rows[r]){
  const b=document.createElement('button'); b.textContent=BT[id]; bel[id]=b; let latched=false;
  const dn=()=>{send('B '+id+' 1');b.classList.add('down');}, up=()=>{send('B '+id+' 0');b.classList.remove('down');};
  b.onmousedown=e=>{ if(latched){latched=false;up();return;} dn(); if(e.shiftKey)latched=true; };
  b.onmouseup=b.onmouseleave=()=>{ if(!latched&&b.classList.contains('down'))up(); };
  document.getElementById(r).appendChild(b);
}
const EN=[['SELECT',0],['TRACK',1],['PRESETS',2],['KNOB 1',3,'#287cff'],['KNOB 2',4,'#1ecc70'],['KNOB 3',5,'#ececf0'],['KNOB 4',6,'#ff621a']], ang={};
function turn(e,n){send('E '+e+' '+n);ang[e]=(ang[e]||0)+n*18;kel[e].style.setProperty('--a',ang[e]+'deg');}
const kel={};
EN.forEach((k,i)=>{
  const d=document.createElement('div'); d.className='knob'; d.innerHTML='<i></i>'+k[0]; if(k[2])d.style.setProperty('--c',k[2]); kel[k[1]]=d.firstChild;
  let acc=0,drag=null;
  d.addEventListener('wheel',e=>{e.preventDefault();acc+=e.deltaY+e.deltaX;while(Math.abs(acc)>=30){turn(k[1],acc>0?1:-1);acc-=acc>0?30:-30;}},{passive:false});
  d.onmousedown=e=>{drag=e.clientX;e.preventDefault();};
  window.addEventListener('mousemove',e=>{if(drag===null)return;while(Math.abs(e.clientX-drag)>=10){const s=e.clientX>drag?1:-1;turn(k[1],s);drag+=s*10;}});
  window.addEventListener('mouseup',()=>{drag=null;});
  document.getElementById(i<3?'enc1':'enc2').appendChild(d);
});
const keys=document.getElementById('keys'), BLACK=[1,3,5,8,10], kd={}, whites=[];
let wi=0;
for(let k=0;k<27;k++){
  const blk=BLACK.includes(k%12), e=document.createElement('div'); e.className=blk?'b':'w';
  if(blk){e.style.left=(wi*6.25-1.9)+'%';e.style.width='3.8%';}else{e.style.left=(wi*6.25)+'%';e.style.width='6.25%';e.textContent=wi+1;whites.push(k);wi++;}
  kd[k]=e;
  const dn=()=>{send('K '+k+' 1');e.classList.add('down');}, up=()=>{if(e.classList.contains('down')){send('K '+k+' 0');e.classList.remove('down');}};
  e.onmousedown=dn; e.onmouseup=e.onmouseleave=up; keys.appendChild(e);
}
const WK='qwertyuiasdfghjk', held={};
window.addEventListener('keydown',e=>{
  if(e.repeat&&e.code!=='ArrowLeft'&&e.code!=='ArrowRight'&&!/^Digit[1-4]$/.test(e.code))return;
  const w=WK.indexOf(e.key.toLowerCase());
  if(e.code==='Space'){e.preventDefault();send('B 10 1');send('B 10 0');}
  else if(e.code==='ArrowLeft'){e.preventDefault();turn(1,-1);}
  else if(e.code==='ArrowRight'){e.preventDefault();turn(1,1);}
  else if(/^Digit[1-4]$/.test(e.code))turn(2+Number(e.code[5]),e.shiftKey?-1:1);
  else if(w>=0&&!held[w]){held[w]=1;send('K '+whites[w]+' 1');kd[whites[w]].classList.add('down');}
});
window.addEventListener('keyup',e=>{const w=WK.indexOf(e.key.toLowerCase());if(w>=0&&held[w]){held[w]=0;send('K '+whites[w]+' 0');kd[whites[w]].classList.remove('down');}});
</script></body></html>"""


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8790
    with lock:
        restart()
    if build_log and not proc:
        print(build_log)
    print(f"FM-1 preview: http://localhost:{port}/")
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()


if __name__ == "__main__":
    main()
