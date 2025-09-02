// layout.js - Preact + htm flex layout test (Yoga-backed) with random backgrounds
const { render, h } = preact;
const { useMemo } = preactHooks || {};

function randColor(){ const r=Math.floor(Math.random()*256), g=Math.floor(Math.random()*256), b=Math.floor(Math.random()*256); return `rgb(${r},${g},${b})`; }

function Box({ style='', children }) {
  const color = useMemo(randColor, []);
  const fullStyle = style + ` background:${color};`;
  return htm`<div style=${fullStyle}>${children}</div>`;
}

function App(){
  return htm`<div style="display:flex; flex-direction:column; width:800px; height:600px;">
    <${Box} style="flex:2;"></${Box}>
    <${Box} style="display:flex; flex-direction:row; flex:1;">
      <${Box} style="flex:1;"></${Box}>
      <${Box} style="flex:1;"></${Box}>
      <${Box} style="flex:1;"></${Box}>
    </${Box}>
    <${Box} style="display:flex; flex-direction:row; flex:1;">
      <${Box} style="flex:2;"></${Box}>
      <${Box} style="flex:1;"></${Box}>
    </${Box}>
  </div>`;
}

render(htm`<${App} />`, document.body);
if (typeof requestComposite === 'function') requestComposite();
