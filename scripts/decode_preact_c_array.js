
// Usage: node decode_preact_c_array.js
// Decodes the C array in ../src/preact_js.cpp and writes the output to ../src/preact.js

const fs = require('fs');
const path = require('path');

const cppPath = path.join(__dirname, '../src/preact_js.cpp');
const jsPath = path.join(__dirname, '../src/preact.js');

const cpp = fs.readFileSync(cppPath, 'utf8');
const arrMatch = cpp.match(/preact_js\[\]\s*=\s*\{([\s\S]*?)\};/);
if (!arrMatch) throw new Error('C array not found');
const arrBody = arrMatch[1];
const hexes = arrBody.match(/0x[0-9a-fA-F]{2}/g);
if (!hexes) throw new Error('No hex bytes found');
const buf = Buffer.from(hexes.map(h => parseInt(h, 16)));
fs.writeFileSync(jsPath, buf);
console.log('Decoded preact_js.cpp to preact.js');
