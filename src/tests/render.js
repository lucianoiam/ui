// Preact render test (htm version): Randomly sized & positioned absolute divs each obtaining a canvas context and drawing either a circle or a rectangle.

const { render } = preact;
const { useRef, useLayoutEffect } = preactHooks || {};

function ShapeCell({ index }) { // now only draws circles
    const ref = useRef(null);
    // Precompute geometry so we can embed into style attribute for compositor
    const size = 64 + Math.floor(Math.random()*193); // 64-256
    const left = Math.floor(Math.random()*(800-size));
    const top  = Math.floor(Math.random()*(600-size));
    useLayoutEffect(() => {
        const el = ref.current; if (!el) return;
        el.__w = size; el.__h = size; // store for drawing bounds
        const ctx = el.getCanvasRenderingContext(); if (!ctx) return;
        const rand8 = () => Math.floor(Math.random()*256) & 0xFF;
        const r = rand8(), g = rand8(), b = rand8();
        const color = ((r << 24) | (g << 16) | (b << 8) | 0xFF) >>> 0;
        const w = size, h = size;
    const radius = Math.max(6, Math.floor(Math.random()*Math.min(w,h)/2));
    const cx = radius + Math.floor(Math.random()*(w - 2*radius));
    const cy = radius + Math.floor(Math.random()*(h - 2*radius));
    ctx.fillCircle(cx, cy, radius, color);
    }, []);
    const style = `position:absolute; left:${left}px; top:${top}px; width:${size}px; height:${size}px; background:transparent; border:1px solid #fff;`;
    return htm`<div ref=${ref} class="cell" style=${style}></div>`;
}

function App() {
    const cells = Array.from({ length: 8 }, (_, i) => htm`<${ShapeCell} key=${i} index=${i} />`);
    return htm`<div class="container" style="position:relative; width:800px; height:600px;">${cells}</div>`;
}

render(htm`<${App} />`, document.body);
