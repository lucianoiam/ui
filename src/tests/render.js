// Preact render test: draws either a circle or a rectangle (random per cell) in randomly sized & positioned div "canvas" elements.

const { h, render } = preact;
const { useRef, useLayoutEffect } = preactHooks || {};

function ShapeCell({ index }) {
    const ref = useRef(null);
    useLayoutEffect(() => {
        const el = ref.current;
        if (!el) return;
        const ctx = el.getCanvasRenderingContext();
        if (!ctx) return;
        const rand8 = () => Math.floor(Math.random()*256) & 0xFF;
        const r = rand8(), g = rand8(), b = rand8();
        const color = ((r << 24) | (g << 16) | (b << 8) | 0xFF) >>> 0;
        const w = el.__w || 64; // stored on element below
        const h = el.__h || 64;
        if (Math.random() < 0.5) {
            // Circle
            const radius = Math.max(6, Math.floor(Math.random()*Math.min(w,h)/2));
            const cx = radius + Math.floor(Math.random()*(w - 2*radius));
            const cy = radius + Math.floor(Math.random()*(h - 2*radius));
            ctx.fillCircle(cx, cy, radius, color);
        } else {
            // Rectangle
            const rw = Math.max(8, Math.floor(Math.random()*w));
            const rh = Math.max(8, Math.floor(Math.random()*h));
            const rx = Math.floor(Math.random()*(w - rw));
            const ry = Math.floor(Math.random()*(h - rh));
            ctx.fillRect(rx, ry, rw, rh, color);
        }
    }, []);
    // Random size 32..128 and random position within 800x600 window (simple bounds)
    const size = 32 + Math.floor(Math.random()*97); // 32-128
    const left = Math.floor(Math.random()*(800-size));
    const top  = Math.floor(Math.random()*(600-size));
    const style = `position:absolute; left:${left}px; top:${top}px; width:${size}px; height:${size}px; background:transparent; border:1px solid #fff;`;
    return h('div', { ref: (n)=>{ if(n){ n.__w=size; n.__h=size; ref.current=n; }}, class: 'cell', style });
}

function App() {
    const cells = [];
    for (let i=0;i<32;i++) cells.push(h(ShapeCell, { index: i, key: i }));
    return h('div', { class: 'container', style: 'position:relative; width:800px; height:600px;' }, cells);
}

render(h(App, {}), document.body);
