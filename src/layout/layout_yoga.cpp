// layout_yoga.cpp - internal Yoga layout integration
#include "layout_yoga.h"
#include "wapis/dom.hpp"
#include "wapis/dom_adapter.h"
#include <yoga/Yoga.h>
#include <unordered_map>
#include <string>
#include <memory>
#include <lexbor/css/syntax/tokenizer.h>
#include <functional>
#include "wapis/dom_hooks.h"
#include "render/attachments.h"


// We hide direct access; adapter provides a helper we expose via a thin accessor.
// Declare accessor (implemented in dom_adapter.cpp via a small addition) that returns C++ node for a JS value.
extern "C" void* dom_get_cpp_node_opaque(JSContext* ctx, JSValueConst v);

static bool g_layout_dirty = true;
void layout_mark_dirty() { g_layout_dirty = true; }
namespace dom { void layout_mark_dirty() { ::layout_mark_dirty(); } }

// Register DOM attribute hook (one-time) to mark layout dirty on style mutations
struct LayoutDomHookInstaller {
    LayoutDomHookInstaller(){
        dom::setAttributeHook(+[](dom::Element* el, const std::string& name, const std::string& value){ (void)value; if(name=="style"){ mark_style_dirty(el); layout_mark_dirty(); } });
    }
};
static LayoutDomHookInstaller g_layout_hook_installer;

bool layout_get_box(dom::Element* el, int& x, int& y, int& w, int& h) {
    if(auto* rd = get_render_data(el)) { x=(int)rd->layoutX; y=(int)rd->layoutY; w=(int)rd->layoutW; h=(int)rd->layoutH; return true; }
    return false;
}

struct Box { float layoutX,layoutY,layoutW,layoutH; };
// TempFlexStore removed (flex meta persisted in attachment)

// --- Basic CSS token parsing helpers (very crude; will replace with Lexbor later) ---
static YGFlexDirection parse_flex_direction(const std::string& css) {
    if (css.find("flex-direction:row-reverse") != std::string::npos) return YGFlexDirectionRowReverse;
    if (css.find("flex-direction:row") != std::string::npos) return YGFlexDirectionRow;
    if (css.find("flex-direction:column-reverse") != std::string::npos) return YGFlexDirectionColumnReverse;
    return YGFlexDirectionColumn; // default
}
static float parse_number_decl(const std::string& css, const char* key) {
    size_t p = css.find(key);
    if (p == std::string::npos) return -1.f;
    size_t c = css.find(':', p); if (c == std::string::npos) return -1.f;
    size_t sc = css.find(';', c+1);
    std::string num = css.substr(c+1, sc==std::string::npos? std::string::npos : sc-(c+1));
    // strip trailing px if present
    if (num.size() > 2 && num.find("px") != std::string::npos) {
        size_t px = num.find("px"); num = num.substr(0, px);
    }
    try { return std::stof(num); } catch(...) { return -1.f; }
}
static float parse_flex(const std::string& css) { return parse_number_decl(css, "flex"); }
static float parse_px(const std::string& css, const char* key) { return parse_number_decl(css, key); }

struct ParsedDecls { std::unordered_map<std::string,std::string> kv; };

static ParsedDecls parse_inline_css_lexbor(const std::string& css) {
    ParsedDecls out; if (css.empty()) return out;
    // Use Lexbor CSS tokenizer to walk tokens and reconstruct simple name:value; pairs per declaration.
    lxb_css_syntax_tokenizer_t *tkz = lxb_css_syntax_tokenizer_create();
    if (!tkz) return out;
    if (lxb_css_syntax_tokenizer_init(tkz) != LXB_STATUS_OK) { lxb_css_syntax_tokenizer_destroy(tkz); return out; }
    lxb_css_syntax_tokenizer_buffer_set(tkz, (const lxb_char_t*)css.data(), css.size());
    std::string currentName; std::string currentValue; bool inName=true; bool seenColon=false;
    auto flushDecl=[&](){ if(!currentName.empty() && !currentValue.empty()) { // trim
            auto trim=[](std::string s){ size_t a=s.find_first_not_of(" \t\n\r"); size_t b=s.find_last_not_of(" \t\n\r"); if(a==std::string::npos) return std::string(); return s.substr(a,b-a+1);};
            std::string n=trim(currentName); std::string v=trim(currentValue);
            if(!n.empty() && !v.empty()) out.kv[n]=v; }
        currentName.clear(); currentValue.clear(); inName=true; seenColon=false; };
    while (true) {
        lxb_css_syntax_token_t* tok = lxb_css_syntax_token(tkz);
        if (!tok) break;
        if (tok->type == LXB_CSS_SYNTAX_TOKEN__EOF) { flushDecl(); break; }
        switch(tok->type) {
            case LXB_CSS_SYNTAX_TOKEN_SEMICOLON:
                flushDecl();
                break;
            case LXB_CSS_SYNTAX_TOKEN_COLON:
                inName=false; seenColon=true; break;
            case LXB_CSS_SYNTAX_TOKEN_WHITESPACE:
                // preserve single space in value if already has content
                if (!inName && seenColon && !currentValue.empty() && currentValue.back()!=' ') currentValue.push_back(' ');
                break;
            default: {
                // Extract token text
                const char* data = (const char*)tok->types.base.begin;
                size_t len = tok->types.base.length;
                if (len) {
                    if (inName) currentName.append(data, len);
                    else if (seenColon) currentValue.append(data, len);
                }
            } break;
        }
        lxb_css_syntax_token_consume(tkz);
    }
    lxb_css_syntax_tokenizer_destroy(tkz);
    return out;
}

// Ensure style parsed & cached in attachment; apply style to Yoga node; store flex meta back into attachment
static void apply_node_style(dom::Element* el, YGNodeRef node) {
    if(!el) return; auto* rd = ensure_render_data(el); if(!rd) return;
    if (rd->dirtyFlags & 1) { // style dirty
        rd->parsedStyle.clear();
        auto pd = parse_inline_css_lexbor(el->styleCssText);
        rd->parsedStyle = pd.kv;
        rd->dirtyFlags &= ~1u; // clear style dirty
    }
    auto find=[&](const char* k)->std::string{ auto it=rd->parsedStyle.find(k); return it==rd->parsedStyle.end()?std::string():it->second; };
    if (std::getenv("LAYOUT_DEBUG") && !el->styleCssText.empty()) {
        fprintf(stderr, "[layout] raw style: '%s'\n", el->styleCssText.c_str());
        for (auto &kv : rd->parsedStyle) fprintf(stderr, "[layout]   decl %s = %s\n", kv.first.c_str(), kv.second.c_str());
    }
    std::string display = find("display");
    if (display == "flex") YGNodeStyleSetDisplay(node, YGDisplayFlex);
    std::string flexdir = find("flex-direction");
    if (!flexdir.empty()) {
        if (flexdir == "row") YGNodeStyleSetFlexDirection(node, YGFlexDirectionRow);
        else if (flexdir == "row-reverse") YGNodeStyleSetFlexDirection(node, YGFlexDirectionRowReverse);
        else if (flexdir == "column-reverse") YGNodeStyleSetFlexDirection(node, YGFlexDirectionColumnReverse);
        else YGNodeStyleSetFlexDirection(node, YGFlexDirectionColumn);
        if (display != "flex" && std::getenv("LAYOUT_DEBUG")) {
            fprintf(stderr, "[layout][warn] flex-direction specified without display:flex (parsed display='%s') raw='%s'\n", display.c_str(), el->styleCssText.c_str());
        }
    } else {
        YGNodeStyleSetFlexDirection(node, parse_flex_direction(el->styleCssText));
    }
    if (std::getenv("LAYOUT_DEBUG") && display == "flex") {
        YGFlexDirection dir = YGNodeStyleGetFlexDirection(node);
        const char* dirStr = dir==YGFlexDirectionRow?"row":dir==YGFlexDirectionRowReverse?"row-reverse":dir==YGFlexDirectionColumn?"column":"column-reverse";
        fprintf(stderr, "[layout] apply_node_style display:flex dir=%s raw='%s'\n", dirStr, flexdir.c_str());
    }
    std::string flexv = find("flex");
    if (!flexv.empty()) {
        std::vector<std::string> parts; size_t start=0; while(start<flexv.size()) { while(start<flexv.size() && isspace((unsigned char)flexv[start])) start++; if(start>=flexv.size()) break; size_t end=start; while(end<flexv.size() && !isspace((unsigned char)flexv[end])) end++; parts.push_back(flexv.substr(start,end-start)); start=end; }
        auto isNumber=[&](const std::string& s){ char* e=nullptr; std::strtof(s.c_str(), &e); return e && *e=='\0'; };
        float grow=0.f, shrink=1.f; enum BasisType { BASIS_AUTO, BASIS_POINT, BASIS_PERCENT, BASIS_NONE }; BasisType basisType=BASIS_NONE; float basisValue=0.f; bool haveGrow=false, haveShrink=false;
        if (parts.size()==1 && isNumber(parts[0])) { grow=std::max(0.f, std::strtof(parts[0].c_str(), nullptr)); haveGrow=true; basisType=BASIS_AUTO; }
        else {
            size_t idx=0; if(idx<parts.size() && isNumber(parts[idx])) { grow=std::strtof(parts[idx].c_str(),nullptr); haveGrow=true; idx++; }
            if(idx<parts.size() && isNumber(parts[idx])) { shrink=std::strtof(parts[idx].c_str(),nullptr); haveShrink=true; idx++; }
            if(idx<parts.size()) { std::string b=parts[idx]; if(b=="auto") basisType=BASIS_AUTO; else if(!b.empty() && b.back()=='%') { basisType=BASIS_PERCENT; basisValue=std::strtof(b.c_str(),nullptr);} else if(b.find("px")!=std::string::npos || isNumber(b)) { basisType=BASIS_POINT; basisValue=std::strtof(b.c_str(),nullptr);} }
        }
        if(haveGrow) YGNodeStyleSetFlexGrow(node,grow); if(haveShrink) YGNodeStyleSetFlexShrink(node,shrink);
        if(basisType==BASIS_PERCENT) YGNodeStyleSetFlexBasisPercent(node,basisValue); else if(basisType==BASIS_POINT) YGNodeStyleSetFlexBasis(node,basisValue); else if(basisType==BASIS_AUTO) YGNodeStyleSetFlexBasisAuto(node);
    }
    auto parsePx=[&](std::string v)->float{ auto tv=v; size_t px=tv.find("px"); if(px!=std::string::npos) tv.erase(px); try { return std::stof(tv); } catch(...) { return -1.f; } };
    auto widthv=find("width"); if(!widthv.empty()) { float f=parsePx(widthv); if(f>=0) YGNodeStyleSetWidth(node,f);} auto heightv=find("height"); if(!heightv.empty()) { float f=parsePx(heightv); if(f>=0) YGNodeStyleSetHeight(node,f);} 
    rd->isFlex=(display=="flex"); rd->flexGrow=YGNodeStyleGetFlexGrow(node); rd->flexShrink=YGNodeStyleGetFlexShrink(node);
    YGValue basis=YGNodeStyleGetFlexBasis(node);
    rd->flexBasisPercent = (basis.unit==YGUnitPercent);
    rd->flexBasisAuto = (basis.unit==YGUnitAuto);
    if(basis.unit==YGUnitPercent || basis.unit==YGUnitPoint) rd->flexBasis = basis.value; else if(basis.unit==YGUnitAuto) rd->flexBasis=0;
    rd->flexDirection=YGNodeStyleGetFlexDirection(node);
}

static YGNodeRef ensure_yoga_node(dom::Element* el){ auto* rd=ensure_render_data(el); if(!rd) return nullptr; if(!rd->yogaNode){ rd->yogaNode = YGNodeNew(); rd->dirtyFlags |= 1; } return (YGNodeRef)rd->yogaNode; }

static void sync_subtree(dom::Element* el){
    if(!el) return; YGNodeRef node = ensure_yoga_node(el); if(!node) return; auto* rd=get_render_data(el);
    if(rd && (rd->dirtyFlags & 1)) apply_node_style(el,node); // update style if dirty
    // Rebuild children each pass (O(n^2) worst-case; optimize later with diff)
    uint32_t existing = YGNodeGetChildCount(node);
    for(int i=(int)existing-1;i>=0;--i){ YGNodeRef ch = YGNodeGetChild(node,(uint32_t)i); YGNodeRemoveChild(node,ch); }
    for(auto &c : el->childNodes){ if(c && c->nodeType==dom::NodeType::ELEMENT){ auto* ce=static_cast<dom::Element*>(c.get()); YGNodeRef cn = ensure_yoga_node(ce); YGNodeInsertChild(node, cn, YGNodeGetChildCount(node)); }}
    for(auto &c : el->childNodes) if(c && c->nodeType==dom::NodeType::ELEMENT) sync_subtree(static_cast<dom::Element*>(c.get()));
}

static void apply_layout_recursive(dom::Element* el, YGNodeRef node, float accL=0, float accT=0) {
    if (!el || !node) return;
    float relL = YGNodeLayoutGetLeft(node);
    float relT = YGNodeLayoutGetTop(node);
    float absL = accL + relL;
    float absT = accT + relT;
    float w = YGNodeLayoutGetWidth(node);
    float h = YGNodeLayoutGetHeight(node);
    if(auto* rd=ensure_render_data(el)) { rd->layoutX=absL; rd->layoutY=absT; rd->layoutW=w; rd->layoutH=h; }
    if (std::getenv("LAYOUT_DEBUG")) {
        fprintf(stderr, "[layout] el=%p tag=%s box=(%.0f,%.0f %.0fx%.0f) rel=(%.0f,%.0f) acc=(%.0f,%.0f)\n", (void*)el, el->tagName.c_str(), absL,absT,w,h, relL,relT, accL,accT);
    }
    // recurse with updated accumulated offset
    uint32_t childIdx = 0;
    for (auto &c : el->childNodes) {
        if (c && c->nodeType == dom::NodeType::ELEMENT) {
            apply_layout_recursive(static_cast<dom::Element*>(c.get()), YGNodeGetChild(node, childIdx++), absL, absT);
        }
    }
}

void layout_maybe_run(JSContext* ctx) {
    if (!ctx) return;
    static bool in_layout = false;
    if (in_layout) return; // avoid reentrant invocation
    in_layout = true;
    // Inefficient mode: always recompute layout each call (ignore dirty flag)
    g_layout_dirty = false; // maintain contract but not relied upon
    // flex meta now persisted in attachments, nothing transient to clear
    // Get body element
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue document = JS_GetPropertyStr(ctx, global, "document");
    JSValue body = JS_GetPropertyStr(ctx, document, "body");
    auto bodyNode = reinterpret_cast<dom::Node*>(dom_get_cpp_node_opaque(ctx, (JSValueConst)body));
    if (!bodyNode || bodyNode->nodeType != dom::NodeType::ELEMENT) {
        JS_FreeValue(ctx, body); JS_FreeValue(ctx, document); JS_FreeValue(ctx, global); return;
    }
    auto bodyEl = static_cast<dom::Element*>(bodyNode);
    // Treat the first element child of body (if any) as the layout root so its flex styles always map to the viewport.
    dom::Element* layoutRootEl = bodyEl;
    for (auto &c : bodyEl->childNodes) { if (c && c->nodeType==dom::NodeType::ELEMENT) { layoutRootEl = static_cast<dom::Element*>(c.get()); break; } }

    YGNodeRef root = ensure_yoga_node(layoutRootEl);
    apply_node_style(layoutRootEl, root);
    // Force viewport size on root flex container using centralized defaults (will adapt when dynamic resize added)
    extern int g_winW; extern int g_winH; // from viewport.h / main.mm
    YGNodeStyleSetWidth(root, (float)g_winW);
    YGNodeStyleSetHeight(root, (float)g_winH);
    // Sync subtree (structure + styles)
    sync_subtree(layoutRootEl);
    YGNodeCalculateLayout(root, YGUndefined, YGUndefined, YGDirectionLTR);
    // Apply to layoutRootEl and descendants (single pass). Root is at (0,0).
    apply_layout_recursive(layoutRootEl, root, 0, 0);
    // If layout root isn't body, set body box to viewport for compositor fallback
    if (layoutRootEl != bodyEl) { extern int g_winW; extern int g_winH; if(auto* rd=ensure_render_data(bodyEl)){ rd->layoutX=0; rd->layoutY=0; rd->layoutW=g_winW; rd->layoutH=g_winH; } }
    // Persist Yoga nodes; no freeing here
    // Diagnostic grouped logging
    if (std::getenv("LAYOUT_DEBUG")) {
        fprintf(stderr, "[layout] === BOXES ===\n");
        for_each_render_data([](dom::Element* el, DomElementRenderData* rd){ if(!rd) return; const char* dirStr="-"; if(rd->isFlex){ int d=rd->flexDirection; dirStr = d==YGFlexDirectionRow?"row":d==YGFlexDirectionRowReverse?"row-rev":d==YGFlexDirectionColumn?"col":"col-rev"; }
            fprintf(stderr, "[layout] box el=%p pos=(%.0f,%.0f) size=(%.0f x %.0f) flex=%d grow=%.2f dir=%s\n", (void*)el, rd->layoutX, rd->layoutY, rd->layoutW, rd->layoutH, rd->isFlex?1:0, rd->flexGrow, dirStr); });
        // Row groups
        for_each_render_data([](dom::Element* parent, DomElementRenderData* pRD){ if(!pRD||!pRD->isFlex) return; if(!(pRD->flexDirection==YGFlexDirectionRow || pRD->flexDirection==YGFlexDirectionRowReverse)) return; float pbw=pRD->layoutW; std::vector<std::pair<dom::Element*,Box>> children; for(auto &c: parent->childNodes) if(c && c->nodeType==dom::NodeType::ELEMENT){ auto* ce=static_cast<dom::Element*>(c.get()); if(auto* crd=get_render_data(ce)){ Box b{crd->layoutX,crd->layoutY,crd->layoutW,crd->layoutH}; children.push_back({ce,b}); }} if(children.empty()) return; fprintf(stderr, "[layout] ROW parent=%p totalW=%.0f children=%zu\n", (void*)parent, pbw, children.size()); float sumGrow=0; for(auto &ch:children){ if(auto* crd=get_render_data(ch.first)) sumGrow += crd->flexGrow; } for(auto &ch:children){ if(auto* crd=get_render_data(ch.first)){ float gw=crd->flexGrow; fprintf(stderr, "  child=%p w=%.0f grow=%.2f ratio=%.2f%% (expected%%=%.2f)\n", (void*)ch.first, ch.second.layoutW, gw, pbw>0? (ch.second.layoutW/pbw*100.f):0.f, (sumGrow>0? (gw/sumGrow*100.f):0.f)); }} });
    }
    // Free temp flex meta stores attached to YG nodes (already freed recursively); context memory leaked per run intentionally small.
    JS_FreeValue(ctx, body); JS_FreeValue(ctx, document); JS_FreeValue(ctx, global);
    // Auto composite after layout (JS unaware)
    extern void native_request_composite(JSContext*);
    native_request_composite(ctx);
    in_layout = false;
}

// (Batching removed for simplicity/robustness)

