// functions.js - lightweight runtime helpers injected before app scripts
// Provides JSX-ish <Box> syntax support by rewriting capitalized element tags
// inside template literals passed to htm`...` before they reach htm.
// This is intentionally heuristic and not a full HTML/JSX parser.
//
// Usage: After loading this file, user code can write:
//   htmx`<App><Box prop=1 /></App>`
// instead of: htm`<${App}><${Box} prop=1 /></${App}>`
//
// Implementation notes:
// - We create a wrapper tag function `htmx` that scans for <Capitalized...> tags
//   and rewrites them into the dynamic form expected by htm (<${Name} ...>).
// - Self-closing tags (<Name />) and paired tags (<Name>...</Name>) handled.
// - Attributes preserved verbatim; does not attempt to parse JS expressions embedded
//   in attribute values.
// - We only transform tags whose first char is A-Z and that are not already in <${ ... } form.
// - Nested tags supported via iterative regex passes until no more matches.
// - Edge cases: strings like <Path d="M10 10"> inside SVG still match; acceptable for test scope.
// - To opt-out for a given tag, prefix with lowercase or use explicit <${Name}> syntax.
//
// Limitations / non-goals:
// - No support for fragments <>...</>.
// - Does not handle namespace prefixes (e.g., <Foo.Bar/>).
// - Does not parse embedded braces inside tag definitions.
// - Minimal escaping; assumes templates are trusted.
//
// If a tag identifier is not found on globalThis, we leave it untouched (so native div/span unaffected).
//
(function() {
// Resolve a dotted path (e.g., ThemeContext.Provider) from globalThis
function resolveComponent(name) {
   let cur = globalThis;
   for (const part of name.split('.')) {
      if (cur == null) {
         cur = undefined;
         break;
      }
      cur = cur[part];
   }
   if (cur) return cur;
   // Lazy resolver to allow forward references within same template definition
   const lazy = function LazyResolvedComponent() {
      let ref = globalThis;
      for (const part of name.split('.')) {
         if (ref == null) {
            ref = null;
            break;
         }
         ref = ref[part];
      }
      if (typeof ref !== 'function') {
         throw new Error('htmx: unresolved component ' + name);
      }
      return ref.apply(this, arguments);
   };
   return lazy;
}

// Build new template arrays; streaming scan converting <Component and </Component>
function htmx(strings, ...values) {
   if (typeof htm !== 'function') throw new Error('htmx: global htm() not yet available');
   const outStrings = [];
   const outValues = [];
   let buffer = '';
   function flush() {
      outStrings.push(buffer);
      buffer = '';
   }
   for (let si = 0; si < strings.length; si++) {
      const str = strings[si];
      let i = 0;
      while (i < str.length) {
         const lt = str.indexOf('<', i);
         if (lt === -1) {
            buffer += str.slice(i);
            break;
         }
         buffer += str.slice(i, lt);  // copy before '<'
         // Already dynamic or comment/doctype
         if (str.startsWith('<${', lt) || str.startsWith('<!--', lt) || str.startsWith('<!DOCTYPE', lt)) {
            buffer += '<';
            i = lt + 1;
            continue;
         }
         let j = lt + 1;
         let closing = false;
         if (j < str.length && str[j] === '/') {
            closing = true;
            j++;
         }
         const nameStart = j;
         while (j < str.length && /[A-Za-z0-9_.]/.test(str[j])) j++;
         const name = str.slice(nameStart, j);
         if (name && /[A-Z]/.test(name[0])) {
            // dynamic component tag
            buffer += '<' + (closing ? '/' : '');
            flush();
            outValues.push(resolveComponent(name));
            // continue with rest of tag (attributes, >) into buffer
            i = j;  // next char after name stays for attribute accumulation
         } else {
            // not a component; treat literally
            buffer += '<';
            i = lt + 1;
         }
      }
      if (si < values.length) {
         flush();
         outValues.push(values[si]);
      }
   }
   flush();
   if (outStrings.length === 0) outStrings.push('');
   const cooked = outStrings.slice();
   cooked.raw = outStrings.slice();
   return htm(cooked, ...outValues);
}

// Expose globally if not already defined.
if (!globalThis.htmx) {
   Object.defineProperty(globalThis, 'htmx', {value: htmx, enumerable: true});
}
})();
