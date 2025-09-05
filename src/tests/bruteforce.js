// Original brute force stress test for QuickJS + React/Preact DOM
// (moved from react_app.js)

const {render} = preact;

function ListItem({value}) {
   return htmx`
		<li>
			<span style="color: green;">Item: </span>
			<b>${value}</b>
		</li>`;
}

function List({count}) {
   return htmx`
		<ul class="big-list">
		${Array.from({length: count}, (_, i) => htmx`
			<ListItem value=${'Value ' + i} />
		`)}
		</ul>`;
}

function Nested({depth}) {
   return depth <= 0 ? htmx`<span>Leaf</span>` : htmx`<div class="nested">
				<span>Depth: ${depth}</span>
				<Nested depth=${depth - 1} />
			  </div>`;
}

function App() {
   return htmx`
		<div class="container">
			<h1>DOM Stress Test</h1>
			<p style="color: blue; font-weight: bold;">
				Rendering 500 list items and 10 levels of nesting
			</p>
			<List count=${500} />
			<Nested depth=${10} />
		</div>`;
}

render(htmx`<App />`, document.body);
