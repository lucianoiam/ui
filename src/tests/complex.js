// Complexity-based React/Preact stress test for QuickJS DOM emulation
// Focus: deep component trees, context, hooks, keys, and prop changes

const {render, createContext} = preact;
const {useState, useEffect, useContext} = preactHooks || {};

const ThemeContext = createContext('light');

function DeepTree({level, max, onLeaf}) {
   if (level >= max) {
      useEffect(() => {
         onLeaf && onLeaf();
      }, []);
      return htm`<span>Leaf at level ${level}</span>`;
   }
   return htm`
      <div class="deep-node" key=${level}>
         <span>Level ${level}</span>
         <${ThemeDisplay} />
         <${DeepTree} level=${level + 1} max=${max} onLeaf=${onLeaf} />
      </div>`;
}

function ThemeDisplay() {
   const theme = useContext(ThemeContext);
   return htm`
      <span style="color: ${theme === 'dark' ? 'white' : 'black'}; \
background: ${theme === 'dark' ? 'black' : 'white'};">
         Theme: ${theme}
      </span>`;
}

function DynamicList({n}) {
   const [items, setItems] = useState(Array.from({length: n}, (_, i) => i));
   useEffect(() => {
      const id = setInterval(() => {
         setItems(items => items.map(x => (x + 1) % 1000));
      }, 10);
      return () => clearInterval(id);
   }, []);
   return htm`
      <ul>
         ${items.map(i => htm`<li key=${i}>Item ${i}</li>`)}
      </ul>`;
}

function App() {
   const [theme, setTheme] = useState('light');
   const [leafCount, setLeafCount] = useState(0);
   return htm`
      <${ThemeContext.Provider} value=${theme}>
         <div class="container">
            <h1>React/Preact Complexity Stress Test</h1>
            <button
               onClick=${() => setTheme(t => (t === 'light' ? 'dark' : 'light'))}
            >Toggle Theme</button>
            <p>Testing deep context, hooks, dynamic lists, and prop changes.</p>
            <${DynamicList} n=${100} />
            <${DeepTree} level=${0} max=${20} onLeaf=${() => setLeafCount(c => c + 1)} />
            <div>Leaf nodes rendered: ${leafCount}</div>
         </div>
      </${ThemeContext.Provider}>`;
}

render(htm`<${App} />`, document.body);
