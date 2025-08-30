'use strict';

Object.defineProperty(exports, '__esModule', { value: true });

/** Normal hydration that attaches to a DOM tree but does not diff it. */
const MODE_HYDRATE = 1 << 5;
/** Signifies this VNode suspended on the previous render */
const MODE_SUSPENDED = 1 << 7;
/** Indicates that this node needs to be inserted while patching children */
const INSERT_VNODE = 1 << 2;
/** Indicates a VNode has been matched with another VNode in the diff */
const MATCHED = 1 << 1;

/** Reset all mode flags */
const RESET_MODE = ~(MODE_HYDRATE | MODE_SUSPENDED);

const SVG_NAMESPACE = 'http://www.w3.org/2000/svg';
const XHTML_NAMESPACE = 'http://www.w3.org/1999/xhtml';
const MATH_NAMESPACE = 'http://www.w3.org/1998/Math/MathML';

const NULL = null;
const UNDEFINED = undefined;
const EMPTY_OBJ = /** @type {any} */ ({});
const EMPTY_ARR = [];
const IS_NON_DIMENSIONAL =
	/acit|ex(?:s|g|n|p|$)|rph|grid|ows|mnc|ntw|ine[ch]|zoo|^ord|itera/i;

const isArray = Array.isArray;

/**
 * Assign properties from `props` to `obj`
 * @template O, P The obj and props types
 * @param {O} obj The object to copy properties to
 * @param {P} props The object to copy properties from
 * @returns {O & P}
 */
function assign(obj, props) {
	// @ts-expect-error We change the type of `obj` to be `O & P`
	for (let i in props) obj[i] = props[i];
	return /** @type {O & P} */ (obj);
}

/**
 * Remove a child node from its parent if attached. This is a workaround for
 * IE11 which doesn't support `Element.prototype.remove()`. Using this function
 * is smaller than including a dedicated polyfill.
 * @param {import('./index').ContainerNode} node The node to remove
 */
function removeNode(node) {
	if (node && node.parentNode) node.parentNode.removeChild(node);
}

const slice = EMPTY_ARR.slice;

/**
 * Find the closest error boundary to a thrown error and call it
 * @param {object} error The thrown value
 * @param {import('../internal').VNode} vnode The vnode that threw the error that was caught (except
 * for unmounting when this parameter is the highest parent that was being
 * unmounted)
 * @param {import('../internal').VNode} [oldVNode]
 * @param {import('../internal').ErrorInfo} [errorInfo]
 */
function _catchError(error, vnode, oldVNode, errorInfo) {
	/** @type {import('../internal').Component} */
	let component,
		/** @type {import('../internal').ComponentType} */
		ctor,
		/** @type {boolean} */
		handled;

	for (; (vnode = vnode._parent); ) {
		if ((component = vnode._component) && !component._processingException) {
			try {
				ctor = component.constructor;

				if (ctor && ctor.getDerivedStateFromError != NULL) {
					component.setState(ctor.getDerivedStateFromError(error));
					handled = component._dirty;
				}

				if (component.componentDidCatch != NULL) {
					component.componentDidCatch(error, errorInfo || {});
					handled = component._dirty;
				}

				// This is an error boundary. Mark it as having bailed out, and whether it was mid-hydration.
				if (handled) {
					return (component._pendingError = component);
				}
			} catch (e) {
				error = e;
			}
		}
	}

	throw error;
}

/**
 * The `option` object can potentially contain callback functions
 * that are called during various stages of our renderer. This is the
 * foundation on which all our addons like `preact/debug`, `preact/compat`,
 * and `preact/hooks` are based on. See the `Options` type in `internal.d.ts`
 * for a full list of available option hooks (most editors/IDEs allow you to
 * ctrl+click or cmd+click on mac the type definition below).
 * @type {import('./internal').Options}
 */
const options = {
	_catchError
};

let vnodeId = 0;

/**
 * Create an virtual node (used for JSX)
 * @param {import('./internal').VNode["type"]} type The node name or Component constructor for this
 * virtual node
 * @param {object | null | undefined} [props] The properties of the virtual node
 * @param {Array<import('.').ComponentChildren>} [children] The children of the
 * virtual node
 * @returns {import('./internal').VNode}
 */
function createElement(type, props, children) {
	let normalizedProps = {},
		key,
		ref,
		i;
	for (i in props) {
		if (i == 'key') key = props[i];
		else if (i == 'ref') ref = props[i];
		else normalizedProps[i] = props[i];
	}

	if (arguments.length > 2) {
		normalizedProps.children =
			arguments.length > 3 ? slice.call(arguments, 2) : children;
	}

	// If a Component VNode, check for and apply defaultProps
	// Note: type may be undefined in development, must never error here.
	if (typeof type == 'function' && type.defaultProps != NULL) {
		for (i in type.defaultProps) {
			if (normalizedProps[i] === UNDEFINED) {
				normalizedProps[i] = type.defaultProps[i];
			}
		}
	}

	return createVNode(type, normalizedProps, key, ref, NULL);
}

/**
 * Create a VNode (used internally by Preact)
 * @param {import('./internal').VNode["type"]} type The node name or Component
 * Constructor for this virtual node
 * @param {object | string | number | null} props The properties of this virtual node.
 * If this virtual node represents a text node, this is the text of the node (string or number).
 * @param {string | number | null} key The key for this virtual node, used when
 * diffing it against its children
 * @param {import('./internal').VNode["ref"]} ref The ref property that will
 * receive a reference to its created child
 * @returns {import('./internal').VNode}
 */
function createVNode(type, props, key, ref, original) {
	// V8 seems to be better at detecting type shapes if the object is allocated from the same call site
	// Do not inline into createElement and coerceToVNode!
	/** @type {import('./internal').VNode} */
	const vnode = {
		type,
		props,
		key,
		ref,
		_children: NULL,
		_parent: NULL,
		_depth: 0,
		_dom: NULL,
		_component: NULL,
		constructor: UNDEFINED,
		_original: original == NULL ? ++vnodeId : original,
		_index: -1,
		_flags: 0
	};

	// Only invoke the vnode hook if this was *not* a direct copy:
	if (original == NULL && options.vnode != NULL) options.vnode(vnode);

	return vnode;
}

function createRef() {
	return { current: NULL };
}

function Fragment(props) {
	return props.children;
}

/**
 * Check if a the argument is a valid Preact VNode.
 * @param {*} vnode
 * @returns {vnode is VNode}
 */
const isValidElement = vnode =>
	vnode != NULL && vnode.constructor == UNDEFINED;

/**
 * Base Component class. Provides `setState()` and `forceUpdate()`, which
 * trigger rendering
 * @param {object} props The initial component props
 * @param {object} context The initial context from parent components'
 * getChildContext
 */
function BaseComponent(props, context) {
	this.props = props;
	this.context = context;
}

/**
 * Update component state and schedule a re-render.
 * @this {import('./internal').Component}
 * @param {object | ((s: object, p: object) => object)} update A hash of state
 * properties to update with new values or a function that given the current
 * state and props returns a new partial state
 * @param {() => void} [callback] A function to be called once component state is
 * updated
 */
BaseComponent.prototype.setState = function (update, callback) {
	// only clone state when copying to nextState the first time.
	let s;
	if (this._nextState != NULL && this._nextState != this.state) {
		s = this._nextState;
	} else {
		s = this._nextState = assign({}, this.state);
	}

	if (typeof update == 'function') {
		// Some libraries like `immer` mark the current state as readonly,
		// preventing us from mutating it, so we need to clone it. See #2716
		update = update(assign({}, s), this.props);
	}

	if (update) {
		assign(s, update);
	}

	// Skip update if updater function returned null
	if (update == NULL) return;

	if (this._vnode) {
		if (callback) {
			this._stateCallbacks.push(callback);
		}
		enqueueRender(this);
	}
};

/**
 * Immediately perform a synchronous re-render of the component
 * @this {import('./internal').Component}
 * @param {() => void} [callback] A function to be called after component is
 * re-rendered
 */
BaseComponent.prototype.forceUpdate = function (callback) {
	if (this._vnode) {
		// Set render mode so that we can differentiate where the render request
		// is coming from. We need this because forceUpdate should never call
		// shouldComponentUpdate
		this._force = true;
		if (callback) this._renderCallbacks.push(callback);
		enqueueRender(this);
	}
};

/**
 * Accepts `props` and `state`, and returns a new Virtual DOM tree to build.
 * Virtual DOM is generally constructed via [JSX](https://jasonformat.com/wtf-is-jsx).
 * @param {object} props Props (eg: JSX attributes) received from parent
 * element/component
 * @param {object} state The component's current state
 * @param {object} context Context object, as returned by the nearest
 * ancestor's `getChildContext()`
 * @returns {ComponentChildren | void}
 */
BaseComponent.prototype.render = Fragment;

/**
 * @param {import('./internal').VNode} vnode
 * @param {number | null} [childIndex]
 */
function getDomSibling(vnode, childIndex) {
	if (childIndex == NULL) {
		// Use childIndex==null as a signal to resume the search from the vnode's sibling
		return vnode._parent
			? getDomSibling(vnode._parent, vnode._index + 1)
			: NULL;
	}

	let sibling;
	for (; childIndex < vnode._children.length; childIndex++) {
		sibling = vnode._children[childIndex];

		if (sibling != NULL && sibling._dom != NULL) {
			// Since updateParentDomPointers keeps _dom pointer correct,
			// we can rely on _dom to tell us if this subtree contains a
			// rendered DOM node, and what the first rendered DOM node is
			return sibling._dom;
		}
	}

	// If we get here, we have not found a DOM node in this vnode's children.
	// We must resume from this vnode's sibling (in it's parent _children array)
	// Only climb up and search the parent if we aren't searching through a DOM
	// VNode (meaning we reached the DOM parent of the original vnode that began
	// the search)
	return typeof vnode.type == 'function' ? getDomSibling(vnode) : NULL;
}

/**
 * Trigger in-place re-rendering of a component.
 * @param {import('./internal').Component} component The component to rerender
 */
function renderComponent(component) {
	let oldVNode = component._vnode,
		oldDom = oldVNode._dom,
		commitQueue = [],
		refQueue = [];

	if (component._parentDom) {
		const newVNode = assign({}, oldVNode);
		newVNode._original = oldVNode._original + 1;
		if (options.vnode) options.vnode(newVNode);

		diff(
			component._parentDom,
			newVNode,
			oldVNode,
			component._globalContext,
			component._parentDom.namespaceURI,
			oldVNode._flags & MODE_HYDRATE ? [oldDom] : NULL,
			commitQueue,
			oldDom == NULL ? getDomSibling(oldVNode) : oldDom,
			!!(oldVNode._flags & MODE_HYDRATE),
			refQueue
		);

		newVNode._original = oldVNode._original;
		newVNode._parent._children[newVNode._index] = newVNode;
		commitRoot(commitQueue, newVNode, refQueue);

		if (newVNode._dom != oldDom) {
			updateParentDomPointers(newVNode);
		}
	}
}

/**
 * @param {import('./internal').VNode} vnode
 */
function updateParentDomPointers(vnode) {
	if ((vnode = vnode._parent) != NULL && vnode._component != NULL) {
		vnode._dom = vnode._component.base = NULL;
		for (let i = 0; i < vnode._children.length; i++) {
			let child = vnode._children[i];
			if (child != NULL && child._dom != NULL) {
				vnode._dom = vnode._component.base = child._dom;
				break;
			}
		}

		return updateParentDomPointers(vnode);
	}
}

/**
 * The render queue
 * @type {Array<import('./internal').Component>}
 */
let rerenderQueue = [];

/*
 * The value of `Component.debounce` must asynchronously invoke the passed in callback. It is
 * important that contributors to Preact can consistently reason about what calls to `setState`, etc.
 * do, and when their effects will be applied. See the links below for some further reading on designing
 * asynchronous APIs.
 * * [Designing APIs for Asynchrony](https://blog.izs.me/2013/08/designing-apis-for-asynchrony)
 * * [Callbacks synchronous and asynchronous](https://blog.ometer.com/2011/07/24/callbacks-synchronous-and-asynchronous/)
 */

let prevDebounce;

const defer =
	typeof Promise == 'function'
		? Promise.prototype.then.bind(Promise.resolve())
		: setTimeout;

/**
 * Enqueue a rerender of a component
 * @param {import('./internal').Component} c The component to rerender
 */
function enqueueRender(c) {
	if (
		(!c._dirty &&
			(c._dirty = true) &&
			rerenderQueue.push(c) &&
			!process._rerenderCount++) ||
		prevDebounce != options.debounceRendering
	) {
		prevDebounce = options.debounceRendering;
		(prevDebounce || defer)(process);
	}
}

/**
 * @param {import('./internal').Component} a
 * @param {import('./internal').Component} b
 */
const depthSort = (a, b) => a._vnode._depth - b._vnode._depth;

/** Flush the render queue by rerendering all queued components */
function process() {
	let c,
		l = 1;

	// Don't update `renderCount` yet. Keep its value non-zero to prevent unnecessary
	// process() calls from getting scheduled while `queue` is still being consumed.
	while (rerenderQueue.length) {
		// Keep the rerender queue sorted by (depth, insertion order). The queue
		// will initially be sorted on the first iteration only if it has more than 1 item.
		//
		// New items can be added to the queue e.g. when rerendering a provider, so we want to
		// keep the order from top to bottom with those new items so we can handle them in a
		// single pass
		if (rerenderQueue.length > l) {
			rerenderQueue.sort(depthSort);
		}

		c = rerenderQueue.shift();
		l = rerenderQueue.length;

		if (c._dirty) {
			renderComponent(c);
		}
	}
	process._rerenderCount = 0;
}

process._rerenderCount = 0;

/**
 * @typedef {import('../internal').ComponentChildren} ComponentChildren
 * @typedef {import('../internal').Component} Component
 * @typedef {import('../internal').PreactElement} PreactElement
 * @typedef {import('../internal').VNode} VNode
 */

/**
 * Diff the children of a virtual node
 * @param {PreactElement} parentDom The DOM element whose children are being
 * diffed
 * @param {ComponentChildren[]} renderResult
 * @param {VNode} newParentVNode The new virtual node whose children should be
 * diff'ed against oldParentVNode
 * @param {VNode} oldParentVNode The old virtual node whose children should be
 * diff'ed against newParentVNode
 * @param {object} globalContext The current context object - modified by
 * getChildContext
 * @param {string} namespace Current namespace of the DOM node (HTML, SVG, or MathML)
 * @param {Array<PreactElement>} excessDomChildren
 * @param {Array<Component>} commitQueue List of components which have callbacks
 * to invoke in commitRoot
 * @param {PreactElement} oldDom The current attached DOM element any new dom
 * elements should be placed around. Likely `null` on first render (except when
 * hydrating). Can be a sibling DOM element when diffing Fragments that have
 * siblings. In most cases, it starts out as `oldChildren[0]._dom`.
 * @param {boolean} isHydrating Whether or not we are in hydration
 * @param {any[]} refQueue an array of elements needed to invoke refs
 */
function diffChildren(
	parentDom,
	renderResult,
	newParentVNode,
	oldParentVNode,
	globalContext,
	namespace,
	excessDomChildren,
	commitQueue,
	oldDom,
	isHydrating,
	refQueue
) {
	let i,
		/** @type {VNode} */
		oldVNode,
		/** @type {VNode} */
		childVNode,
		/** @type {PreactElement} */
		newDom,
		/** @type {PreactElement} */
		firstChildDom;

	// This is a compression of oldParentVNode!=null && oldParentVNode != EMPTY_OBJ && oldParentVNode._children || EMPTY_ARR
	// as EMPTY_OBJ._children should be `undefined`.
	/** @type {VNode[]} */
	let oldChildren = (oldParentVNode && oldParentVNode._children) || EMPTY_ARR;

	let newChildrenLength = renderResult.length;

	oldDom = constructNewChildrenArray(
		newParentVNode,
		renderResult,
		oldChildren,
		oldDom,
		newChildrenLength
	);

	for (i = 0; i < newChildrenLength; i++) {
		childVNode = newParentVNode._children[i];
		if (childVNode == NULL) continue;

		// At this point, constructNewChildrenArray has assigned _index to be the
		// matchingIndex for this VNode's oldVNode (or -1 if there is no oldVNode).
		if (childVNode._index == -1) {
			oldVNode = EMPTY_OBJ;
		} else {
			oldVNode = oldChildren[childVNode._index] || EMPTY_OBJ;
		}

		// Update childVNode._index to its final index
		childVNode._index = i;

		// Morph the old element into the new one, but don't append it to the dom yet
		let result = diff(
			parentDom,
			childVNode,
			oldVNode,
			globalContext,
			namespace,
			excessDomChildren,
			commitQueue,
			oldDom,
			isHydrating,
			refQueue
		);

		// Adjust DOM nodes
		newDom = childVNode._dom;
		if (childVNode.ref && oldVNode.ref != childVNode.ref) {
			if (oldVNode.ref) {
				applyRef(oldVNode.ref, NULL, childVNode);
			}
			refQueue.push(
				childVNode.ref,
				childVNode._component || newDom,
				childVNode
			);
		}

		if (firstChildDom == NULL && newDom != NULL) {
			firstChildDom = newDom;
		}

		if (
			childVNode._flags & INSERT_VNODE ||
			oldVNode._children === childVNode._children
		) {
			oldDom = insert(childVNode, oldDom, parentDom);
		} else if (typeof childVNode.type == 'function' && result !== UNDEFINED) {
			oldDom = result;
		} else if (newDom) {
			oldDom = newDom.nextSibling;
		}

		// Unset diffing flags
		childVNode._flags &= ~(INSERT_VNODE | MATCHED);
	}

	newParentVNode._dom = firstChildDom;

	return oldDom;
}

/**
 * @param {VNode} newParentVNode
 * @param {ComponentChildren[]} renderResult
 * @param {VNode[]} oldChildren
 */
function constructNewChildrenArray(
	newParentVNode,
	renderResult,
	oldChildren,
	oldDom,
	newChildrenLength
) {
	/** @type {number} */
	let i;
	/** @type {VNode} */
	let childVNode;
	/** @type {VNode} */
	let oldVNode;

	let oldChildrenLength = oldChildren.length,
		remainingOldChildren = oldChildrenLength;

	let skew = 0;

	newParentVNode._children = new Array(newChildrenLength);
	for (i = 0; i < newChildrenLength; i++) {
		// @ts-expect-error We are reusing the childVNode variable to hold both the
		// pre and post normalized childVNode
		childVNode = renderResult[i];

		if (
			childVNode == NULL ||
			typeof childVNode == 'boolean' ||
			typeof childVNode == 'function'
		) {
			newParentVNode._children[i] = NULL;
			continue;
		}
		// If this newVNode is being reused (e.g. <div>{reuse}{reuse}</div>) in the same diff,
		// or we are rendering a component (e.g. setState) copy the oldVNodes so it can have
		// it's own DOM & etc. pointers
		else if (
			typeof childVNode == 'string' ||
			typeof childVNode == 'number' ||
			// eslint-disable-next-line valid-typeof
			typeof childVNode == 'bigint' ||
			childVNode.constructor == String
		) {
			childVNode = newParentVNode._children[i] = createVNode(
				NULL,
				childVNode,
				NULL,
				NULL,
				NULL
			);
		} else if (isArray(childVNode)) {
			childVNode = newParentVNode._children[i] = createVNode(
				Fragment,
				{ children: childVNode },
				NULL,
				NULL,
				NULL
			);
		} else if (childVNode.constructor == UNDEFINED && childVNode._depth > 0) {
			// VNode is already in use, clone it. This can happen in the following
			// scenario:
			//   const reuse = <div />
			//   <div>{reuse}<span />{reuse}</div>
			childVNode = newParentVNode._children[i] = createVNode(
				childVNode.type,
				childVNode.props,
				childVNode.key,
				childVNode.ref ? childVNode.ref : NULL,
				childVNode._original
			);
		} else {
			childVNode = newParentVNode._children[i] = childVNode;
		}

		const skewedIndex = i + skew;
		childVNode._parent = newParentVNode;
		childVNode._depth = newParentVNode._depth + 1;

		// Temporarily store the matchingIndex on the _index property so we can pull
		// out the oldVNode in diffChildren. We'll override this to the VNode's
		// final index after using this property to get the oldVNode
		const matchingIndex = (childVNode._index = findMatchingIndex(
			childVNode,
			oldChildren,
			skewedIndex,
			remainingOldChildren
		));

		oldVNode = NULL;
		if (matchingIndex != -1) {
			oldVNode = oldChildren[matchingIndex];
			remainingOldChildren--;
			if (oldVNode) {
				oldVNode._flags |= MATCHED;
			}
		}

		// Here, we define isMounting for the purposes of the skew diffing
		// algorithm. Nodes that are unsuspending are considered mounting and we detect
		// this by checking if oldVNode._original == null
		const isMounting = oldVNode == NULL || oldVNode._original == NULL;

		if (isMounting) {
			if (matchingIndex == -1) {
				// When the array of children is growing we need to decrease the skew
				// as we are adding a new element to the array.
				// Example:
				// [1, 2, 3] --> [0, 1, 2, 3]
				// oldChildren   newChildren
				//
				// The new element is at index 0, so our skew is 0,
				// we need to decrease the skew as we are adding a new element.
				// The decrease will cause us to compare the element at position 1
				// with value 1 with the element at position 0 with value 0.
				//
				// A linear concept is applied when the array is shrinking,
				// if the length is unchanged we can assume that no skew
				// changes are needed.
				if (newChildrenLength > oldChildrenLength) {
					skew--;
				} else if (newChildrenLength < oldChildrenLength) {
					skew++;
				}
			}

			// If we are mounting a DOM VNode, mark it for insertion
			if (typeof childVNode.type != 'function') {
				childVNode._flags |= INSERT_VNODE;
			}
		} else if (matchingIndex != skewedIndex) {
			// When we move elements around i.e. [0, 1, 2] --> [1, 0, 2]
			// --> we diff 1, we find it at position 1 while our skewed index is 0 and our skew is 0
			//     we set the skew to 1 as we found an offset.
			// --> we diff 0, we find it at position 0 while our skewed index is at 2 and our skew is 1
			//     this makes us increase the skew again.
			// --> we diff 2, we find it at position 2 while our skewed index is at 4 and our skew is 2
			//
			// this becomes an optimization question where currently we see a 1 element offset as an insertion
			// or deletion i.e. we optimize for [0, 1, 2] --> [9, 0, 1, 2]
			// while a more than 1 offset we see as a swap.
			// We could probably build heuristics for having an optimized course of action here as well, but
			// might go at the cost of some bytes.
			//
			// If we wanted to optimize for i.e. only swaps we'd just do the last two code-branches and have
			// only the first item be a re-scouting and all the others fall in their skewed counter-part.
			// We could also further optimize for swaps
			if (matchingIndex == skewedIndex - 1) {
				skew--;
			} else if (matchingIndex == skewedIndex + 1) {
				skew++;
			} else {
				if (matchingIndex > skewedIndex) {
					skew--;
				} else {
					skew++;
				}

				// Move this VNode's DOM if the original index (matchingIndex) doesn't
				// match the new skew index (i + new skew)
				// In the former two branches we know that it matches after skewing
				childVNode._flags |= INSERT_VNODE;
			}
		}
	}

	// Remove remaining oldChildren if there are any. Loop forwards so that as we
	// unmount DOM from the beginning of the oldChildren, we can adjust oldDom to
	// point to the next child, which needs to be the first DOM node that won't be
	// unmounted.
	if (remainingOldChildren) {
		for (i = 0; i < oldChildrenLength; i++) {
			oldVNode = oldChildren[i];
			if (oldVNode != NULL && (oldVNode._flags & MATCHED) == 0) {
				if (oldVNode._dom == oldDom) {
					oldDom = getDomSibling(oldVNode);
				}

				unmount(oldVNode, oldVNode);
			}
		}
	}

	return oldDom;
}

/**
 * @param {VNode} parentVNode
 * @param {PreactElement} oldDom
 * @param {PreactElement} parentDom
 * @returns {PreactElement}
 */
function insert(parentVNode, oldDom, parentDom) {
	// Note: VNodes in nested suspended trees may be missing _children.

	if (typeof parentVNode.type == 'function') {
		let children = parentVNode._children;
		for (let i = 0; children && i < children.length; i++) {
			if (children[i]) {
				// If we enter this code path on sCU bailout, where we copy
				// oldVNode._children to newVNode._children, we need to update the old
				// children's _parent pointer to point to the newVNode (parentVNode
				// here).
				children[i]._parent = parentVNode;
				oldDom = insert(children[i], oldDom, parentDom);
			}
		}

		return oldDom;
	} else if (parentVNode._dom != oldDom) {
		if (oldDom && parentVNode.type && !parentDom.contains(oldDom)) {
			oldDom = getDomSibling(parentVNode);
		}
		parentDom.insertBefore(parentVNode._dom, oldDom || NULL);
		oldDom = parentVNode._dom;
	}

	do {
		oldDom = oldDom && oldDom.nextSibling;
	} while (oldDom != NULL && oldDom.nodeType == 8);

	return oldDom;
}

/**
 * Flatten and loop through the children of a virtual node
 * @param {ComponentChildren} children The unflattened children of a virtual
 * node
 * @returns {VNode[]}
 */
function toChildArray(children, out) {
	out = out || [];
	if (children == NULL || typeof children == 'boolean') ; else if (isArray(children)) {
		children.some(child => {
			toChildArray(child, out);
		});
	} else {
		out.push(children);
	}
	return out;
}

/**
 * @param {VNode} childVNode
 * @param {VNode[]} oldChildren
 * @param {number} skewedIndex
 * @param {number} remainingOldChildren
 * @returns {number}
 */
function findMatchingIndex(
	childVNode,
	oldChildren,
	skewedIndex,
	remainingOldChildren
) {
	const key = childVNode.key;
	const type = childVNode.type;
	let oldVNode = oldChildren[skewedIndex];
	const matched = oldVNode != NULL && (oldVNode._flags & MATCHED) == 0;

	// We only need to perform a search if there are more children
	// (remainingOldChildren) to search. However, if the oldVNode we just looked
	// at skewedIndex was not already used in this diff, then there must be at
	// least 1 other (so greater than 1) remainingOldChildren to attempt to match
	// against. So the following condition checks that ensuring
	// remainingOldChildren > 1 if the oldVNode is not already used/matched. Else
	// if the oldVNode was null or matched, then there could needs to be at least
	// 1 (aka `remainingOldChildren > 0`) children to find and compare against.
	//
	// If there is an unkeyed functional VNode, that isn't a built-in like our Fragment,
	// we should not search as we risk re-using state of an unrelated VNode. (reverted for now)
	let shouldSearch =
		// (typeof type != 'function' || type === Fragment || key) &&
		remainingOldChildren > (matched ? 1 : 0);

	if (
		(oldVNode === NULL && childVNode.key == null) ||
		(matched && key == oldVNode.key && type == oldVNode.type)
	) {
		return skewedIndex;
	} else if (shouldSearch) {
		let x = skewedIndex - 1;
		let y = skewedIndex + 1;
		while (x >= 0 || y < oldChildren.length) {
			const childIndex = x >= 0 ? x-- : y++;
			oldVNode = oldChildren[childIndex];
			if (
				oldVNode != NULL &&
				(oldVNode._flags & MATCHED) == 0 &&
				key == oldVNode.key &&
				type == oldVNode.type
			) {
				return childIndex;
			}
		}
	}

	return -1;
}

function setStyle(style, key, value) {
	if (key[0] == '-') {
		style.setProperty(key, value == NULL ? '' : value);
	} else if (value == NULL) {
		style[key] = '';
	} else if (typeof value != 'number' || IS_NON_DIMENSIONAL.test(key)) {
		style[key] = value;
	} else {
		style[key] = value + 'px';
	}
}

const CAPTURE_REGEX = /(PointerCapture)$|Capture$/i;

// A logical clock to solve issues like https://github.com/preactjs/preact/issues/3927.
// When the DOM performs an event it leaves micro-ticks in between bubbling up which means that
// an event can trigger on a newly reated DOM-node while the event bubbles up.
//
// Originally inspired by Vue
// (https://github.com/vuejs/core/blob/caeb8a68811a1b0f79/packages/runtime-dom/src/modules/events.ts#L90-L101),
// but modified to use a logical clock instead of Date.now() in case event handlers get attached
// and events get dispatched during the same millisecond.
//
// The clock is incremented after each new event dispatch. This allows 1 000 000 new events
// per second for over 280 years before the value reaches Number.MAX_SAFE_INTEGER (2**53 - 1).
let eventClock = 0;

/**
 * Set a property value on a DOM node
 * @param {import('../internal').PreactElement} dom The DOM node to modify
 * @param {string} name The name of the property to set
 * @param {*} value The value to set the property to
 * @param {*} oldValue The old value the property had
 * @param {string} namespace Whether or not this DOM node is an SVG node or not
 */
function setProperty(dom, name, value, oldValue, namespace) {
	let useCapture;

	o: if (name == 'style') {
		if (typeof value == 'string') {
			dom.style.cssText = value;
		} else {
			if (typeof oldValue == 'string') {
				dom.style.cssText = oldValue = '';
			}

			if (oldValue) {
				for (name in oldValue) {
					if (!(value && name in value)) {
						setStyle(dom.style, name, '');
					}
				}
			}

			if (value) {
				for (name in value) {
					if (!oldValue || value[name] != oldValue[name]) {
						setStyle(dom.style, name, value[name]);
					}
				}
			}
		}
	}
	// Benchmark for comparison: https://esbench.com/bench/574c954bdb965b9a00965ac6
	else if (name[0] == 'o' && name[1] == 'n') {
		useCapture = name != (name = name.replace(CAPTURE_REGEX, '$1'));
		const lowerCaseName = name.toLowerCase();

		// Infer correct casing for DOM built-in events:
		if (lowerCaseName in dom || name == 'onFocusOut' || name == 'onFocusIn')
			name = lowerCaseName.slice(2);
		else name = name.slice(2);

		if (!dom._listeners) dom._listeners = {};
		dom._listeners[name + useCapture] = value;

		if (value) {
			if (!oldValue) {
				value._attached = eventClock;
				dom.addEventListener(
					name,
					useCapture ? eventProxyCapture : eventProxy,
					useCapture
				);
			} else {
				value._attached = oldValue._attached;
			}
		} else {
			dom.removeEventListener(
				name,
				useCapture ? eventProxyCapture : eventProxy,
				useCapture
			);
		}
	} else {
		if (namespace == SVG_NAMESPACE) {
			// Normalize incorrect prop usage for SVG:
			// - xlink:href / xlinkHref --> href (xlink:href was removed from SVG and isn't needed)
			// - className --> class
			name = name.replace(/xlink(H|:h)/, 'h').replace(/sName$/, 's');
		} else if (
			name != 'width' &&
			name != 'height' &&
			name != 'href' &&
			name != 'list' &&
			name != 'form' &&
			// Default value in browsers is `-1` and an empty string is
			// cast to `0` instead
			name != 'tabIndex' &&
			name != 'download' &&
			name != 'rowSpan' &&
			name != 'colSpan' &&
			name != 'role' &&
			name != 'popover' &&
			name in dom
		) {
			try {
				dom[name] = value == NULL ? '' : value;
				// labelled break is 1b smaller here than a return statement (sorry)
				break o;
			} catch (e) {}
		}

		// aria- and data- attributes have no boolean representation.
		// A `false` value is different from the attribute not being
		// present, so we can't remove it. For non-boolean aria
		// attributes we could treat false as a removal, but the
		// amount of exceptions would cost too many bytes. On top of
		// that other frameworks generally stringify `false`.

		if (typeof value == 'function') ; else if (value != NULL && (value !== false || name[4] == '-')) {
			dom.setAttribute(name, name == 'popover' && value == true ? '' : value);
		} else {
			dom.removeAttribute(name);
		}
	}
}

/**
 * Create an event proxy function.
 * @param {boolean} useCapture Is the event handler for the capture phase.
 * @private
 */
function createEventProxy(useCapture) {
	/**
	 * Proxy an event to hooked event handlers
	 * @param {import('../internal').PreactEvent} e The event object from the browser
	 * @private
	 */
	return function (e) {
		if (this._listeners) {
			const eventHandler = this._listeners[e.type + useCapture];
			if (e._dispatched == NULL) {
				e._dispatched = eventClock++;

				// When `e._dispatched` is smaller than the time when the targeted event
				// handler was attached we know we have bubbled up to an element that was added
				// during patching the DOM.
			} else if (e._dispatched < eventHandler._attached) {
				return;
			}
			return eventHandler(options.event ? options.event(e) : e);
		}
	};
}

const eventProxy = createEventProxy(false);
const eventProxyCapture = createEventProxy(true);

/**
 * @typedef {import('../internal').ComponentChildren} ComponentChildren
 * @typedef {import('../internal').Component} Component
 * @typedef {import('../internal').PreactElement} PreactElement
 * @typedef {import('../internal').VNode} VNode
 */

/**
 * @template {any} T
 * @typedef {import('../internal').Ref<T>} Ref<T>
 */

/**
 * Diff two virtual nodes and apply proper changes to the DOM
 * @param {PreactElement} parentDom The parent of the DOM element
 * @param {VNode} newVNode The new virtual node
 * @param {VNode} oldVNode The old virtual node
 * @param {object} globalContext The current context object. Modified by
 * getChildContext
 * @param {string} namespace Current namespace of the DOM node (HTML, SVG, or MathML)
 * @param {Array<PreactElement>} excessDomChildren
 * @param {Array<Component>} commitQueue List of components which have callbacks
 * to invoke in commitRoot
 * @param {PreactElement} oldDom The current attached DOM element any new dom
 * elements should be placed around. Likely `null` on first render (except when
 * hydrating). Can be a sibling DOM element when diffing Fragments that have
 * siblings. In most cases, it starts out as `oldChildren[0]._dom`.
 * @param {boolean} isHydrating Whether or not we are in hydration
 * @param {any[]} refQueue an array of elements needed to invoke refs
 */
function diff(
	parentDom,
	newVNode,
	oldVNode,
	globalContext,
	namespace,
	excessDomChildren,
	commitQueue,
	oldDom,
	isHydrating,
	refQueue
) {
	/** @type {any} */
	let tmp,
		newType = newVNode.type;

	// When passing through createElement it assigns the object
	// constructor as undefined. This to prevent JSON-injection.
	if (newVNode.constructor != UNDEFINED) return NULL;

	// If the previous diff bailed out, resume creating/hydrating.
	if (oldVNode._flags & MODE_SUSPENDED) {
		isHydrating = !!(oldVNode._flags & MODE_HYDRATE);
		oldDom = newVNode._dom = oldVNode._dom;
		excessDomChildren = [oldDom];
	}

	if ((tmp = options._diff)) tmp(newVNode);

	outer: if (typeof newType == 'function') {
		try {
			let c, isNew, oldProps, oldState, snapshot, clearProcessingException;
			let newProps = newVNode.props;
			const isClassComponent =
				'prototype' in newType && newType.prototype.render;

			// Necessary for createContext api. Setting this property will pass
			// the context value as `this.context` just for this component.
			tmp = newType.contextType;
			let provider = tmp && globalContext[tmp._id];
			let componentContext = tmp
				? provider
					? provider.props.value
					: tmp._defaultValue
				: globalContext;

			// Get component and set it to `c`
			if (oldVNode._component) {
				c = newVNode._component = oldVNode._component;
				clearProcessingException = c._processingException = c._pendingError;
			} else {
				// Instantiate the new component
				if (isClassComponent) {
					// @ts-expect-error The check above verifies that newType is suppose to be constructed
					newVNode._component = c = new newType(newProps, componentContext); // eslint-disable-line new-cap
				} else {
					// @ts-expect-error Trust me, Component implements the interface we want
					newVNode._component = c = new BaseComponent(
						newProps,
						componentContext
					);
					c.constructor = newType;
					c.render = doRender;
				}
				if (provider) provider.sub(c);

				c.props = newProps;
				if (!c.state) c.state = {};
				c.context = componentContext;
				c._globalContext = globalContext;
				isNew = c._dirty = true;
				c._renderCallbacks = [];
				c._stateCallbacks = [];
			}

			// Invoke getDerivedStateFromProps
			if (isClassComponent && c._nextState == NULL) {
				c._nextState = c.state;
			}

			if (isClassComponent && newType.getDerivedStateFromProps != NULL) {
				if (c._nextState == c.state) {
					c._nextState = assign({}, c._nextState);
				}

				assign(
					c._nextState,
					newType.getDerivedStateFromProps(newProps, c._nextState)
				);
			}

			oldProps = c.props;
			oldState = c.state;
			c._vnode = newVNode;

			// Invoke pre-render lifecycle methods
			if (isNew) {
				if (
					isClassComponent &&
					newType.getDerivedStateFromProps == NULL &&
					c.componentWillMount != NULL
				) {
					c.componentWillMount();
				}

				if (isClassComponent && c.componentDidMount != NULL) {
					c._renderCallbacks.push(c.componentDidMount);
				}
			} else {
				if (
					isClassComponent &&
					newType.getDerivedStateFromProps == NULL &&
					newProps !== oldProps &&
					c.componentWillReceiveProps != NULL
				) {
					c.componentWillReceiveProps(newProps, componentContext);
				}

				if (
					(!c._force &&
						c.shouldComponentUpdate != NULL &&
						c.shouldComponentUpdate(
							newProps,
							c._nextState,
							componentContext
						) === false) ||
					newVNode._original == oldVNode._original
				) {
					// More info about this here: https://gist.github.com/JoviDeCroock/bec5f2ce93544d2e6070ef8e0036e4e8
					if (newVNode._original != oldVNode._original) {
						// When we are dealing with a bail because of sCU we have to update
						// the props, state and dirty-state.
						// when we are dealing with strict-equality we don't as the child could still
						// be dirtied see #3883
						c.props = newProps;
						c.state = c._nextState;
						c._dirty = false;
					}

					newVNode._dom = oldVNode._dom;
					newVNode._children = oldVNode._children;
					newVNode._children.some(vnode => {
						if (vnode) vnode._parent = newVNode;
					});

					for (let i = 0; i < c._stateCallbacks.length; i++) {
						c._renderCallbacks.push(c._stateCallbacks[i]);
					}
					c._stateCallbacks = [];

					if (c._renderCallbacks.length) {
						commitQueue.push(c);
					}

					break outer;
				}

				if (c.componentWillUpdate != NULL) {
					c.componentWillUpdate(newProps, c._nextState, componentContext);
				}

				if (isClassComponent && c.componentDidUpdate != NULL) {
					c._renderCallbacks.push(() => {
						c.componentDidUpdate(oldProps, oldState, snapshot);
					});
				}
			}

			c.context = componentContext;
			c.props = newProps;
			c._parentDom = parentDom;
			c._force = false;

			let renderHook = options._render,
				count = 0;
			if (isClassComponent) {
				c.state = c._nextState;
				c._dirty = false;

				if (renderHook) renderHook(newVNode);

				tmp = c.render(c.props, c.state, c.context);

				for (let i = 0; i < c._stateCallbacks.length; i++) {
					c._renderCallbacks.push(c._stateCallbacks[i]);
				}
				c._stateCallbacks = [];
			} else {
				do {
					c._dirty = false;
					if (renderHook) renderHook(newVNode);

					tmp = c.render(c.props, c.state, c.context);

					// Handle setState called in render, see #2553
					c.state = c._nextState;
				} while (c._dirty && ++count < 25);
			}

			// Handle setState called in render, see #2553
			c.state = c._nextState;

			if (c.getChildContext != NULL) {
				globalContext = assign(assign({}, globalContext), c.getChildContext());
			}

			if (isClassComponent && !isNew && c.getSnapshotBeforeUpdate != NULL) {
				snapshot = c.getSnapshotBeforeUpdate(oldProps, oldState);
			}

			let isTopLevelFragment =
				tmp != NULL && tmp.type === Fragment && tmp.key == NULL;
			let renderResult = tmp;

			if (isTopLevelFragment) {
				renderResult = cloneNode(tmp.props.children);
			}

			oldDom = diffChildren(
				parentDom,
				isArray(renderResult) ? renderResult : [renderResult],
				newVNode,
				oldVNode,
				globalContext,
				namespace,
				excessDomChildren,
				commitQueue,
				oldDom,
				isHydrating,
				refQueue
			);

			c.base = newVNode._dom;

			// We successfully rendered this VNode, unset any stored hydration/bailout state:
			newVNode._flags &= RESET_MODE;

			if (c._renderCallbacks.length) {
				commitQueue.push(c);
			}

			if (clearProcessingException) {
				c._pendingError = c._processingException = NULL;
			}
		} catch (e) {
			newVNode._original = NULL;
			// if hydrating or creating initial tree, bailout preserves DOM:
			if (isHydrating || excessDomChildren != NULL) {
				if (e.then) {
					newVNode._flags |= isHydrating
						? MODE_HYDRATE | MODE_SUSPENDED
						: MODE_SUSPENDED;

					while (oldDom && oldDom.nodeType == 8 && oldDom.nextSibling) {
						oldDom = oldDom.nextSibling;
					}

					excessDomChildren[excessDomChildren.indexOf(oldDom)] = NULL;
					newVNode._dom = oldDom;
				} else {
					for (let i = excessDomChildren.length; i--; ) {
						removeNode(excessDomChildren[i]);
					}
					markAsForce(newVNode);
				}
			} else {
				newVNode._dom = oldVNode._dom;
				newVNode._children = oldVNode._children;
				if (!e.then) markAsForce(newVNode);
			}
			options._catchError(e, newVNode, oldVNode);
		}
	} else if (
		excessDomChildren == NULL &&
		newVNode._original == oldVNode._original
	) {
		newVNode._children = oldVNode._children;
		newVNode._dom = oldVNode._dom;
	} else {
		oldDom = newVNode._dom = diffElementNodes(
			oldVNode._dom,
			newVNode,
			oldVNode,
			globalContext,
			namespace,
			excessDomChildren,
			commitQueue,
			isHydrating,
			refQueue
		);
	}

	if ((tmp = options.diffed)) tmp(newVNode);

	return newVNode._flags & MODE_SUSPENDED ? undefined : oldDom;
}

function markAsForce(vnode) {
	if (vnode && vnode._component) vnode._component._force = true;
	if (vnode && vnode._children) vnode._children.forEach(markAsForce);
}

/**
 * @param {Array<Component>} commitQueue List of components
 * which have callbacks to invoke in commitRoot
 * @param {VNode} root
 */
function commitRoot(commitQueue, root, refQueue) {
	for (let i = 0; i < refQueue.length; i++) {
		applyRef(refQueue[i], refQueue[++i], refQueue[++i]);
	}

	if (options._commit) options._commit(root, commitQueue);

	commitQueue.some(c => {
		try {
			// @ts-expect-error Reuse the commitQueue variable here so the type changes
			commitQueue = c._renderCallbacks;
			c._renderCallbacks = [];
			commitQueue.some(cb => {
				// @ts-expect-error See above comment on commitQueue
				cb.call(c);
			});
		} catch (e) {
			options._catchError(e, c._vnode);
		}
	});
}

function cloneNode(node) {
	if (
		typeof node != 'object' ||
		node == NULL ||
		(node._depth && node._depth > 0)
	) {
		return node;
	}

	if (isArray(node)) {
		return node.map(cloneNode);
	}

	return assign({}, node);
}

/**
 * Diff two virtual nodes representing DOM element
 * @param {PreactElement} dom The DOM element representing the virtual nodes
 * being diffed
 * @param {VNode} newVNode The new virtual node
 * @param {VNode} oldVNode The old virtual node
 * @param {object} globalContext The current context object
 * @param {string} namespace Current namespace of the DOM node (HTML, SVG, or MathML)
 * @param {Array<PreactElement>} excessDomChildren
 * @param {Array<Component>} commitQueue List of components which have callbacks
 * to invoke in commitRoot
 * @param {boolean} isHydrating Whether or not we are in hydration
 * @param {any[]} refQueue an array of elements needed to invoke refs
 * @returns {PreactElement}
 */
function diffElementNodes(
	dom,
	newVNode,
	oldVNode,
	globalContext,
	namespace,
	excessDomChildren,
	commitQueue,
	isHydrating,
	refQueue
) {
	let oldProps = oldVNode.props;
	let newProps = newVNode.props;
	let nodeType = /** @type {string} */ (newVNode.type);
	/** @type {any} */
	let i;
	/** @type {{ __html?: string }} */
	let newHtml;
	/** @type {{ __html?: string }} */
	let oldHtml;
	/** @type {ComponentChildren} */
	let newChildren;
	let value;
	let inputValue;
	let checked;

	// Tracks entering and exiting namespaces when descending through the tree.
	if (nodeType == 'svg') namespace = SVG_NAMESPACE;
	else if (nodeType == 'math') namespace = MATH_NAMESPACE;
	else if (!namespace) namespace = XHTML_NAMESPACE;

	if (excessDomChildren != NULL) {
		for (i = 0; i < excessDomChildren.length; i++) {
			value = excessDomChildren[i];

			// if newVNode matches an element in excessDomChildren or the `dom`
			// argument matches an element in excessDomChildren, remove it from
			// excessDomChildren so it isn't later removed in diffChildren
			if (
				value &&
				'setAttribute' in value == !!nodeType &&
				(nodeType ? value.localName == nodeType : value.nodeType == 3)
			) {
				dom = value;
				excessDomChildren[i] = NULL;
				break;
			}
		}
	}

	if (dom == NULL) {
		if (nodeType == NULL) {
			return document.createTextNode(newProps);
		}

		dom = document.createElementNS(
			namespace,
			nodeType,
			newProps.is && newProps
		);

		// we are creating a new node, so we can assume this is a new subtree (in
		// case we are hydrating), this deopts the hydrate
		if (isHydrating) {
			if (options._hydrationMismatch)
				options._hydrationMismatch(newVNode, excessDomChildren);
			isHydrating = false;
		}
		// we created a new parent, so none of the previously attached children can be reused:
		excessDomChildren = NULL;
	}

	if (nodeType == NULL) {
		// During hydration, we still have to split merged text from SSR'd HTML.
		if (oldProps !== newProps && (!isHydrating || dom.data != newProps)) {
			dom.data = newProps;
		}
	} else {
		// If excessDomChildren was not null, repopulate it with the current element's children:
		excessDomChildren = excessDomChildren && slice.call(dom.childNodes);

		oldProps = oldVNode.props || EMPTY_OBJ;

		// If we are in a situation where we are not hydrating but are using
		// existing DOM (e.g. replaceNode) we should read the existing DOM
		// attributes to diff them
		if (!isHydrating && excessDomChildren != NULL) {
			oldProps = {};
			for (i = 0; i < dom.attributes.length; i++) {
				value = dom.attributes[i];
				oldProps[value.name] = value.value;
			}
		}

		for (i in oldProps) {
			value = oldProps[i];
			if (i == 'children') ; else if (i == 'dangerouslySetInnerHTML') {
				oldHtml = value;
			} else if (!(i in newProps)) {
				if (
					(i == 'value' && 'defaultValue' in newProps) ||
					(i == 'checked' && 'defaultChecked' in newProps)
				) {
					continue;
				}
				setProperty(dom, i, NULL, value, namespace);
			}
		}

		// During hydration, props are not diffed at all (including dangerouslySetInnerHTML)
		// @TODO we should warn in debug mode when props don't match here.
		for (i in newProps) {
			value = newProps[i];
			if (i == 'children') {
				newChildren = value;
			} else if (i == 'dangerouslySetInnerHTML') {
				newHtml = value;
			} else if (i == 'value') {
				inputValue = value;
			} else if (i == 'checked') {
				checked = value;
			} else if (
				(!isHydrating || typeof value == 'function') &&
				oldProps[i] !== value
			) {
				setProperty(dom, i, value, oldProps[i], namespace);
			}
		}

		// If the new vnode didn't have dangerouslySetInnerHTML, diff its children
		if (newHtml) {
			// Avoid re-applying the same '__html' if it did not changed between re-render
			if (
				!isHydrating &&
				(!oldHtml ||
					(newHtml.__html != oldHtml.__html && newHtml.__html != dom.innerHTML))
			) {
				dom.innerHTML = newHtml.__html;
			}

			newVNode._children = [];
		} else {
			if (oldHtml) dom.innerHTML = '';

			diffChildren(
				// @ts-expect-error
				newVNode.type == 'template' ? dom.content : dom,
				isArray(newChildren) ? newChildren : [newChildren],
				newVNode,
				oldVNode,
				globalContext,
				nodeType == 'foreignObject' ? XHTML_NAMESPACE : namespace,
				excessDomChildren,
				commitQueue,
				excessDomChildren
					? excessDomChildren[0]
					: oldVNode._children && getDomSibling(oldVNode, 0),
				isHydrating,
				refQueue
			);

			// Remove children that are not part of any vnode.
			if (excessDomChildren != NULL) {
				for (i = excessDomChildren.length; i--; ) {
					removeNode(excessDomChildren[i]);
				}
			}
		}

		// As above, don't diff props during hydration
		if (!isHydrating) {
			i = 'value';
			if (nodeType == 'progress' && inputValue == NULL) {
				dom.removeAttribute('value');
			} else if (
				inputValue != UNDEFINED &&
				// #2756 For the <progress>-element the initial value is 0,
				// despite the attribute not being present. When the attribute
				// is missing the progress bar is treated as indeterminate.
				// To fix that we'll always update it when it is 0 for progress elements
				(inputValue !== dom[i] ||
					(nodeType == 'progress' && !inputValue) ||
					// This is only for IE 11 to fix <select> value not being updated.
					// To avoid a stale select value we need to set the option.value
					// again, which triggers IE11 to re-evaluate the select value
					(nodeType == 'option' && inputValue != oldProps[i]))
			) {
				setProperty(dom, i, inputValue, oldProps[i], namespace);
			}

			i = 'checked';
			if (checked != UNDEFINED && checked != dom[i]) {
				setProperty(dom, i, checked, oldProps[i], namespace);
			}
		}
	}

	return dom;
}

/**
 * Invoke or update a ref, depending on whether it is a function or object ref.
 * @param {Ref<any> & { _unmount?: unknown }} ref
 * @param {any} value
 * @param {VNode} vnode
 */
function applyRef(ref, value, vnode) {
	try {
		if (typeof ref == 'function') {
			let hasRefUnmount = typeof ref._unmount == 'function';
			if (hasRefUnmount) {
				// @ts-ignore TS doesn't like moving narrowing checks into variables
				ref._unmount();
			}

			if (!hasRefUnmount || value != NULL) {
				// Store the cleanup function on the function
				// instance object itself to avoid shape
				// transitioning vnode
				ref._unmount = ref(value);
			}
		} else ref.current = value;
	} catch (e) {
		options._catchError(e, vnode);
	}
}

/**
 * Unmount a virtual node from the tree and apply DOM changes
 * @param {VNode} vnode The virtual node to unmount
 * @param {VNode} parentVNode The parent of the VNode that initiated the unmount
 * @param {boolean} [skipRemove] Flag that indicates that a parent node of the
 * current element is already detached from the DOM.
 */
function unmount(vnode, parentVNode, skipRemove) {
	let r;
	if (options.unmount) options.unmount(vnode);

	if ((r = vnode.ref)) {
		if (!r.current || r.current == vnode._dom) {
			applyRef(r, NULL, parentVNode);
		}
	}

	if ((r = vnode._component) != NULL) {
		if (r.componentWillUnmount) {
			try {
				r.componentWillUnmount();
			} catch (e) {
				options._catchError(e, parentVNode);
			}
		}

		r.base = r._parentDom = NULL;
	}

	if ((r = vnode._children)) {
		for (let i = 0; i < r.length; i++) {
			if (r[i]) {
				unmount(
					r[i],
					parentVNode,
					skipRemove || typeof vnode.type != 'function'
				);
			}
		}
	}

	if (!skipRemove) {
		removeNode(vnode._dom);
	}

	vnode._component = vnode._parent = vnode._dom = UNDEFINED;
}

/** The `.render()` method for a PFC backing instance. */
function doRender(props, state, context) {
	return this.constructor(props, context);
}

/**
 * Render a Preact virtual node into a DOM element
 * @param {import('./internal').ComponentChild} vnode The virtual node to render
 * @param {import('./internal').PreactElement} parentDom The DOM element to render into
 * @param {import('./internal').PreactElement | object} [replaceNode] Optional: Attempt to re-use an
 * existing DOM tree rooted at `replaceNode`
 */
function render(vnode, parentDom, replaceNode) {
	// https://github.com/preactjs/preact/issues/3794
	if (parentDom == document) {
		parentDom = document.documentElement;
	}

	if (options._root) options._root(vnode, parentDom);

	// We abuse the `replaceNode` parameter in `hydrate()` to signal if we are in
	// hydration mode or not by passing the `hydrate` function instead of a DOM
	// element..
	let isHydrating = typeof replaceNode == 'function';

	// To be able to support calling `render()` multiple times on the same
	// DOM node, we need to obtain a reference to the previous tree. We do
	// this by assigning a new `_children` property to DOM nodes which points
	// to the last rendered tree. By default this property is not present, which
	// means that we are mounting a new tree for the first time.
	let oldVNode = isHydrating
		? NULL
		: (replaceNode && replaceNode._children) || parentDom._children;

	vnode = ((!isHydrating && replaceNode) || parentDom)._children =
		createElement(Fragment, NULL, [vnode]);

	// List of effects that need to be called after diffing.
	let commitQueue = [],
		refQueue = [];
	diff(
		parentDom,
		// Determine the new vnode tree and store it on the DOM element on
		// our custom `_children` property.
		vnode,
		oldVNode || EMPTY_OBJ,
		EMPTY_OBJ,
		parentDom.namespaceURI,
		!isHydrating && replaceNode
			? [replaceNode]
			: oldVNode
				? NULL
				: parentDom.firstChild
					? slice.call(parentDom.childNodes)
					: NULL,
		commitQueue,
		!isHydrating && replaceNode
			? replaceNode
			: oldVNode
				? oldVNode._dom
				: parentDom.firstChild,
		isHydrating,
		refQueue
	);

	// Flush all queued effects
	commitRoot(commitQueue, vnode, refQueue);
}

/**
 * Update an existing DOM element with data from a Preact virtual node
 * @param {import('./internal').ComponentChild} vnode The virtual node to render
 * @param {import('./internal').PreactElement} parentDom The DOM element to update
 */
function hydrate(vnode, parentDom) {
	render(vnode, parentDom, hydrate);
}

/**
 * Clones the given VNode, optionally adding attributes/props and replacing its
 * children.
 * @param {import('./internal').VNode} vnode The virtual DOM element to clone
 * @param {object} props Attributes/props to add when cloning
 * @param {Array<import('./internal').ComponentChildren>} rest Any additional arguments will be used
 * as replacement children.
 * @returns {import('./internal').VNode}
 */
function cloneElement(vnode, props, children) {
	let normalizedProps = assign({}, vnode.props),
		key,
		ref,
		i;

	let defaultProps;

	if (vnode.type && vnode.type.defaultProps) {
		defaultProps = vnode.type.defaultProps;
	}

	for (i in props) {
		if (i == 'key') key = props[i];
		else if (i == 'ref') ref = props[i];
		else if (props[i] === UNDEFINED && defaultProps != UNDEFINED) {
			normalizedProps[i] = defaultProps[i];
		} else {
			normalizedProps[i] = props[i];
		}
	}

	if (arguments.length > 2) {
		normalizedProps.children =
			arguments.length > 3 ? slice.call(arguments, 2) : children;
	}

	return createVNode(
		vnode.type,
		normalizedProps,
		key || vnode.key,
		ref || vnode.ref,
		NULL
	);
}

let i = 0;

function createContext(defaultValue) {
	function Context(props) {
		if (!this.getChildContext) {
			/** @type {Set<import('./internal').Component> | null} */
			let subs = new Set();
			let ctx = {};
			ctx[Context._id] = this;

			this.getChildContext = () => ctx;

			this.componentWillUnmount = () => {
				subs = NULL;
			};

			this.shouldComponentUpdate = function (_props) {
				// @ts-expect-error even
				if (this.props.value != _props.value) {
					subs.forEach(c => {
						c._force = true;
						enqueueRender(c);
					});
				}
			};

			this.sub = c => {
				subs.add(c);
				let old = c.componentWillUnmount;
				c.componentWillUnmount = () => {
					if (subs) {
						subs.delete(c);
					}
					if (old) old.call(c);
				};
			};
		}

		return props.children;
	}

	Context._id = '__cC' + i++;
	Context._defaultValue = defaultValue;

	/** @type {import('./internal').FunctionComponent} */
	Context.Consumer = (props, contextValue) => {
		return props.children(contextValue);
	};

	// we could also get rid of _contextRef entirely
	Context.Provider =
		Context._contextRef =
		Context.Consumer.contextType =
			Context;

	return Context;
}

exports.Component = BaseComponent;
exports.Fragment = Fragment;
exports.cloneElement = cloneElement;
exports.createContext = createContext;
exports.createElement = createElement;
exports.createRef = createRef;
exports.h = createElement;
exports.hydrate = hydrate;
exports.isValidElement = isValidElement;
exports.options = options;
exports.render = render;
exports.toChildArray = toChildArray;
