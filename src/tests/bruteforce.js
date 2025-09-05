// Original brute force stress test for QuickJS + React/Preact DOM
// (moved from react_app.js)

const {render} = preact;

function ListItem({value}) {
   return htm`
		<li>
			<span style="color: green;">Item: </span>
			<b>${value}</b>
		</li>`;
}

function List({count}) {
   return htm`
		<ul class="big-list">
		${Array.from({length: count}, (_, i) => htm`
			<${ListItem} value=${'Value ' + i} />
		`)}
		</ul>`;
}

function Nested({depth}) {
   return depth <= 0 ? htm`<span>Leaf</span>` : htm`<div class="nested">
				<span>Depth: ${depth}</span>
				<${Nested} depth=${depth - 1} />
			  </div>`;
}

function App() {
   return htm`
		<div class="container">
			<h1>DOM Stress Test</h1>
			<p style="color: blue; font-weight: bold;">
				Rendering 500 list items and 10 levels of nesting
			</p>
			<${List} count=${500} />
			<${Nested} depth=${10} />
		</div>`;
}

render(htm`<${App} />`, document.body);
