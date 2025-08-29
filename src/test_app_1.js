// test_app_1.js
// Original brute force stress test for QuickJS + React/Preact DOM
// (moved from react_app.js)

const { h, render } = preact;

function ListItem({ value }) {
	return h('li', null, [
		h('span', { style: 'color: green;' }, 'Item: '),
		h('b', null, value)
	]);
}

function List({ count }) {
	let items = [];
	for (let i = 0; i < count; ++i) {
		items.push(h(ListItem, { value: 'Value ' + i }));
	}
	return h('ul', { class: 'big-list' }, items);
}

function Nested({ depth }) {
	if (depth <= 0) return h('span', null, 'Leaf');
	return h('div', { class: 'nested' }, [
		h('span', null, 'Depth: ' + depth),
		h(Nested, { depth: depth - 1 })
	]);
}

function App() {
	return h('div', { class: 'container' }, [
		h('h1', null, 'DOM Stress Test'),
		h('p', { style: 'color: blue; font-weight: bold;' }, 'Rendering 500 list items and 10 levels of nesting'),
		h(List, { count: 500 }),
		h(Nested, { depth: 10 })
	]);
}

render(h(App), document.body);
