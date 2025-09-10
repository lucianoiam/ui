#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace css {
struct Decls {
   std::unordered_map<std::string, std::string> kv;
};

Decls parse_inline(const std::string& cssText);

struct FlexShorthand {
   bool haveGrow = false;
   float grow = 0;
   bool haveShrink = false;
   float shrink = 1;
   bool basisAuto = false;
   bool basisPercent = false;
   bool basisPoint = false;
   float basisValue = 0;
};

FlexShorthand parse_flex(const std::string& flexValue);
bool parse_number_unit(const std::string& value, float& outValue, std::string& outUnit);
// Parse an rgb(R,G,B) color; returns true on success and fills 0..255 ints.
bool parse_rgb_color(const std::string& value, int& r, int& g, int& b);
} // namespace css
