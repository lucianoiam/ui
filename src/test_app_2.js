// test_app_2.js
// Complexity-based React/Preact stress test for QuickJS DOM emulation
// Focus: deep component trees, context, hooks, keys, and prop changes

const { h, render, createContext } = preact;
const { useState, useEffect, useContext } = preactHooks || {};

const ThemeContext = createContext('light');

function DeepTree({ level, max, onLeaf }) {
    if (level >= max) {
        useEffect(() => { onLeaf && onLeaf(); }, []);
        return h('span', null, `Leaf at level ${level}`);
    }
    return h('div', { class: 'deep-node', key: level }, [
        h('span', null, `Level ${level}`),
        h(ThemeDisplay, {}),
        h(DeepTree, { level: level + 1, max, onLeaf })
    ]);
}

function ThemeDisplay() {
    const theme = useContext(ThemeContext);
    return h('span', { style: `color: ${theme === 'dark' ? 'white' : 'black'}; background: ${theme === 'dark' ? 'black' : 'white'};` }, `Theme: ${theme}`);
}

function DynamicList({ n }) {
    const [items, setItems] = useState(Array.from({ length: n }, (_, i) => i));
    useEffect(() => {
        const id = setInterval(() => {
            setItems(items => items.map(x => (x + 1) % 1000));
        }, 10);
        return () => clearInterval(id);
    }, []);
    return h('ul', null, items.map(i => h('li', { key: i }, `Item ${i}`)));
}

function App() {
    const [theme, setTheme] = useState('light');
    const [leafCount, setLeafCount] = useState(0);
    return h(ThemeContext.Provider, { value: theme },
        h('div', { class: 'container' }, [
            h('h1', null, 'React/Preact Complexity Stress Test'),
            h('button', { onClick: () => setTheme(t => t === 'light' ? 'dark' : 'light') }, 'Toggle Theme'),
            h('p', null, 'Testing deep context, hooks, dynamic lists, and prop changes.'),
            h(DynamicList, { n: 100 }),
            h(DeepTree, { level: 0, max: 20, onLeaf: () => setLeafCount(c => c + 1) }),
            h('div', null, `Leaf nodes rendered: ${leafCount}`)
        ])
    );
}

render(h(App), document.body);
