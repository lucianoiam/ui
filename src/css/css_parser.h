#pragma once
#include <string>
#include <unordered_map>
#include <vector>
// Unified CSS parsing interface using Lexbor only (no manual scanning in clients)
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
} // namespace css
