// Preact render test: 16 draggable circle canvases using addEventListener.
const { render } = preact;
const { useRef, useLayoutEffect } = preactHooks || {};

function ShapeCell({ index }) {
  const ref = useRef(null);
  const size = 64 + Math.floor(Math.random()*65);
  const left = Math.floor(Math.random()*(800-size));
  const top  = Math.floor(Math.random()*(600-size));
  useLayoutEffect(() => {
    const el = ref.current; if(!el) return;
    const ctx = el.getContext && el.getContext('2d'); if(!ctx) return;
    const rand8 = () => Math.floor(Math.random()*256) & 0xFF;
    for (let i=0;i<8;i++) {
      const r=rand8(), g=rand8(), b=rand8();
      const color = ((r<<24)|(g<<16)|(b<<8)|0xFF)>>>0;
      const maxR = Math.min(size,size)/2;
      const radius = Math.max(4, Math.floor(Math.random()*maxR));
      if (radius*2 >= size) continue;
      const cx = radius + Math.floor(Math.random()*(size-2*radius));
      const cy = radius + Math.floor(Math.random()*(size-2*radius));
      ctx.fillCircle(cx, cy, radius, color);
    }
  }, []);
  useLayoutEffect(() => {
    const el = ref.current; if(!el) return;
    const styleFor=(x,y)=>`position:absolute; left:${x}px; top:${y}px; width:${size}px; height:${size}px; background:transparent; border:1px solid #fff;`;
    el.setAttribute('style', styleFor(left, top));
    let dragging=false, offX=0, offY=0;
    function onDown(e){ const st=el.getAttribute('style')||''; const mL=/left:(\d+)/.exec(st); const mT=/top:(\d+)/.exec(st); const curL=mL?+mL[1]:0; const curT=mT?+mT[1]:0; dragging=true; offX=(e.clientX|0)-curL; offY=(e.clientY|0)-curT; }
    function onMove(e){ if(!dragging) return; const x=e.clientX|0,y=e.clientY|0; let nx=Math.max(0,Math.min(800-size,x-offX)); let ny=Math.max(0,Math.min(600-size,y-offY)); el.setAttribute('style', styleFor(nx, ny)); if (typeof requestComposite==='function') requestComposite(); }
    function onUp(){ dragging=false; }
    el.addEventListener('mousedown', onDown);
    el.addEventListener('mousemove', onMove);
    el.addEventListener('mouseup', onUp);
  }, []);
  const style=`position:absolute; left:${left}px; top:${top}px; width:${size}px; height:${size}px; background:transparent; border:1px solid #fff;`;
  return htm`<canvas ref=${ref} style=${style}></canvas>`;
}

function App(){
  const cells = Array.from({length:4}, (_,i)=> htm`<${ShapeCell} key=${i} index=${i} />`);
  return htm`<canvas style="position:relative; width:800px; height:600px; display:block;">${cells}</canvas>`;
}

render(htm`<${App} />`, document.body);
