function printNode(node, indent = '') {
    let s = '';
    if (node.nodeType === 1) {
        let style = node.style && node.style.cssText ? node.style.cssText : '';
        let styleAttr = style.length > 0 ? " style=\"" + style + "\"" : "";
        let className = node.class && node.class.length > 0 ? " class=\"" + node.class + "\"" : "";
        s += indent + '<' + node._nodeName.toLowerCase() + className + styleAttr + '>' + '\n';
        for (let child of node.childNodes) {
            s += printNode(child, indent + '  ');
        }
        s += indent + '</' + node._nodeName.toLowerCase() + '>' + '\n';
    } else if (node.nodeType === 3) {
        s += indent + node.nodeValue + '\n';
    }
    return s;
}
console.log(printNode(document.body));
