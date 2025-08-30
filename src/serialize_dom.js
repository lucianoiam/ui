function serializeNode(node, indent = '') {
    let s = '';
    if (node.nodeType === 1) {
        let style = node.style && node.style.cssText ? node.style.cssText : '';
        let styleAttr = style.length > 0 ? " style=\"" + style + "\"" : "";
        let className = node.class && node.class.length > 0 ? " class=\"" + node.class + "\"" : "";
        s += indent + '<' + node._nodeName.toLowerCase() + className + styleAttr + '>' + '\n';
        for (let child of node.childNodes) {
            s += serializeNode(child, indent + '  ');
        }
        s += indent + '</' + node._nodeName.toLowerCase() + '>' + '\n';
    } else if (node.nodeType === 3) {
        s += indent + node.nodeValue + '\n';
    }
    return s;
}

function serialize_dom() {
    return serializeNode(document.body);
}

if (typeof globalThis !== 'undefined') {
    globalThis.serialize_dom = serialize_dom;
}
