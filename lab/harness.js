const fs = require('fs');
const path = require('path');

// Load Preact
const preactPath = path.resolve(__dirname, 'preact.esm.js');
console.log('[Lab] Loading Preact from:', preactPath);
const preact = require(preactPath);
const { h, render } = preact;
console.log('[Lab] Preact loaded, h:', !!h, 'render:', !!render);

// --- Custom DOM Implementation with full logging ---

class CustomNode {
  constructor(nodeName, nodeType) {
    console.log(`[DOM] constructor: ${nodeName} (${nodeType})`);
    this.nodeName = nodeName;
    this._nodeType = nodeType;
    this._childNodes = [];
    this._parentNode = null;
    this._nodeValue = null;
  }

  get nodeType() {
    console.log(`[DOM] get nodeType on ${this.nodeName}`);
    return this._nodeType;
  }

  get childNodes() {
    console.log(`[DOM] get childNodes on ${this.nodeName}`);
    return this._childNodes;
  }

  get firstChild() {
    console.log(`[DOM] get firstChild on ${this.nodeName}`);
    return this._childNodes[0] || null;
  }

  get nodeValue() {
    console.log(`[DOM] get nodeValue on ${this.nodeName}`);
    return this._nodeValue;
  }

  set nodeValue(value) {
    console.log(`[DOM] set nodeValue on ${this.nodeName} =`, value);
    this._nodeValue = value;
  }

  get parentNode() {
    console.log(`[DOM] get parentNode on ${this.nodeName}`);
    return this._parentNode;
  }

  get ownerDocument() {
    console.log(`[DOM] get ownerDocument on ${this.nodeName}`);
    return this._ownerDocument;
  }

  appendChild(node) {
    console.log(`[DOM] appendChild on ${this.nodeName} with node ${node.nodeName}`);
    this._childNodes.push(node);
    node._parentNode = this;
    return node;
  }

  insertBefore(node, reference) {
    console.log(`[DOM] insertBefore on ${this.nodeName} with node ${node.nodeName} before ${reference ? reference.nodeName : 'null'}`);
    const index = reference ? this._childNodes.indexOf(reference) : this._childNodes.length;
    this._childNodes.splice(index, 0, node);
    node._parentNode = this;
    return node;
  }
}

class CustomElement extends CustomNode {
  constructor(tagName) {
    console.log(`[DOM] new Element(${tagName})`);
    super(tagName.toUpperCase(), 1);
  }
}

class CustomText extends CustomNode {
  constructor(text) {
    console.log(`[DOM] new Text("${text}")`);
    super('#text', 3);
    this._nodeValue = text;
  }
}

class CustomDocument {
  constructor() {
    console.log(`[DOM] new Document()`);
    this.body = new CustomElement('BODY');
    this.body._ownerDocument = this;
  }

  createElement(tagName) {
    console.log(`[DOM] createElement(${tagName})`);
    const el = new CustomElement(tagName);
    el._ownerDocument = this;
    return el;
  }

  createElementNS(ns, qualifiedName) {
    console.log(`[DOM] createElementNS(${ns}, ${qualifiedName})`);
    return this.createElement(qualifiedName);
  }

  createTextNode(text) {
    console.log(`[DOM] createTextNode(${text})`);
    const txt = new CustomText(text);
    txt._ownerDocument = this;
    return txt;
  }

  insertBefore(node, reference) {
    console.log(`[DOM] insertBefore on Document.body`);
    return this.body.insertBefore(node, reference);
  }
}

const document = new CustomDocument();

const window = {
  requestAnimationFrame: (cb) => {
    console.log('[DOM] requestAnimationFrame called');
    setImmediate(cb);
  },
};

// Set globals
global.window = window;
global.document = document;
global.Node = CustomNode;
global.Element = CustomElement;
global.Text = CustomText;

// Render Preact content
try {
  render(h('div', null, 'Hello World'), document.body);
  console.log('[Lab] Rendered successfully');
} catch (e) {
  console.error('[Lab] Render failed:', e);
}

// Serialize output for inspection
function serializeNode(node, indent = '') {
  if (node.nodeType === 3) {
    return indent + node.nodeValue;
  }
  const children = node.childNodes.map(child => serializeNode(child, indent + '  ')).join('\n');
  return `${indent}<${node.nodeName.toLowerCase()}>\n${children}\n${indent}</${node.nodeName.toLowerCase()}>`;
}

console.log('[Lab] Final DOM:\n', serializeNode(document.body));

