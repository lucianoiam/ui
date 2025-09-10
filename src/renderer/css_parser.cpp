#include "css_parser.h"
#include <cctype>
#include <cmath>
#include <lexbor/css/syntax/tokenizer.h>

namespace css {
Decls parse_inline(const std::string& cssText)
{
   Decls out;
   if (cssText.empty())
      return out;
   lxb_css_syntax_tokenizer_t* tkz = lxb_css_syntax_tokenizer_create();
   if (!tkz)
      return out;
   if (lxb_css_syntax_tokenizer_init(tkz) != LXB_STATUS_OK) {
      lxb_css_syntax_tokenizer_destroy(tkz);
      return out;
   }
   lxb_css_syntax_tokenizer_buffer_set(tkz, (const lxb_char_t*)cssText.data(), cssText.size());
   std::string name, value;
   bool inName = true;
   bool seenColon = false;
   auto trim = [](std::string s) {
      size_t a = s.find_first_not_of(" \t\n\r");
      size_t b = s.find_last_not_of(" \t\n\r");
      if (a == std::string::npos)
         return std::string();
      return s.substr(a, b - a + 1);
   };
   auto flush = [&]() {
      if (!name.empty() && !value.empty()) {
         auto n = trim(name);
         auto v = trim(value);
         if (!n.empty() && !v.empty())
            out.kv[n] = v;
      }
      name.clear();
      value.clear();
      inName = true;
      seenColon = false;
   };
   while (true) {
      lxb_css_syntax_token_t* tok = lxb_css_syntax_token(tkz);
      if (!tok)
         break;
      if (tok->type == LXB_CSS_SYNTAX_TOKEN__EOF) {
         flush();
         break;
      }
      switch (tok->type) {
      case LXB_CSS_SYNTAX_TOKEN_SEMICOLON:
         flush();
         break;
      case LXB_CSS_SYNTAX_TOKEN_COLON:
         inName = false;
         seenColon = true;
         break;
      case LXB_CSS_SYNTAX_TOKEN_WHITESPACE:
         if (!inName && seenColon && !value.empty() && value.back() != ' ')
            value.push_back(' ');
         break;
      default: {
         const char* d = (const char*)tok->types.base.begin;
         size_t len = tok->types.base.length;
         if (len) {
            if (inName)
               name.append(d, len);
            else if (seenColon)
               value.append(d, len);
         }
      } break;
      }
      lxb_css_syntax_token_consume(tkz);
   }
   lxb_css_syntax_tokenizer_destroy(tkz);
   return out;
}

bool parse_number_unit(const std::string& value, float& out, std::string& unit)
{
   out = -1.f;
   unit.clear();
   if (value.empty())
      return false;
   lxb_css_syntax_tokenizer_t* tkz = lxb_css_syntax_tokenizer_create();
   if (!tkz)
      return false;
   if (lxb_css_syntax_tokenizer_init(tkz) != LXB_STATUS_OK) {
      lxb_css_syntax_tokenizer_destroy(tkz);
      return false;
   }
   lxb_css_syntax_tokenizer_buffer_set(tkz, (const lxb_char_t*)value.data(), value.size());
   while (true) {
      lxb_css_syntax_token_t* tok = lxb_css_syntax_token(tkz);
      if (!tok)
         break;
      if (tok->type == LXB_CSS_SYNTAX_TOKEN__EOF)
         break;
      if (tok->type == LXB_CSS_SYNTAX_TOKEN_NUMBER || tok->type == LXB_CSS_SYNTAX_TOKEN_DIMENSION ||
          tok->type == LXB_CSS_SYNTAX_TOKEN_PERCENTAGE) {
         const char* d = (const char*)tok->types.base.begin;
         size_t len = tok->types.base.length;
         if (len) {
            std::string txt(d, len);
            try {
               out = std::stof(txt);
            }
            catch (...) {
               out = -1.f;
            }
            if (tok->type == LXB_CSS_SYNTAX_TOKEN_DIMENSION) {
               size_t i = 0;
               while (i < txt.size() &&
                      (std::isdigit((unsigned char)txt[i]) || txt[i] == '+' || txt[i] == '-' || txt[i] == '.'))
                  ++i;
               unit = txt.substr(i);
            }
            else if (tok->type == LXB_CSS_SYNTAX_TOKEN_PERCENTAGE) {
               unit = "%";
            }
            lxb_css_syntax_tokenizer_destroy(tkz);
            return out >= 0;
         }
      }
      lxb_css_syntax_token_consume(tkz);
   }
   lxb_css_syntax_tokenizer_destroy(tkz);
   return false;
}

bool parse_rgb_color(const std::string& value, int& r, int& g, int& b)
{
   r = g = b = 0;
   if (value.empty())
      return false;
   lxb_css_syntax_tokenizer_t* tkz = lxb_css_syntax_tokenizer_create();
   if (!tkz)
      return false;
   if (lxb_css_syntax_tokenizer_init(tkz) != LXB_STATUS_OK) {
      lxb_css_syntax_tokenizer_destroy(tkz);
      return false;
   }
   lxb_css_syntax_tokenizer_buffer_set(tkz, (const lxb_char_t*)value.data(), value.size());
   auto is_name = [](const lxb_css_syntax_token_string_t& s, const char* want) {
      size_t n = 0;
      while (want[n] != '\0')
         ++n;
      if (s.length != n)
         return false;
      for (size_t i = 0; i < n; ++i) {
         if (std::tolower((unsigned char)s.data[i]) != std::tolower((unsigned char)want[i]))
            return false;
      }
      return true;
   };
   int nums[3] = {0, 0, 0};
   int idx = 0;
   bool ok = false;
   bool in_func = false;
   while (true) {
      lxb_css_syntax_token_t* tok = lxb_css_syntax_token(tkz);
      if (!tok)
         break;
      if (tok->type == LXB_CSS_SYNTAX_TOKEN__EOF)
         break;
      if (!in_func && tok->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION) {
         if (is_name(tok->types.function, "rgb") || is_name(tok->types.function, "rgba")) {
            in_func = true;
         }
         lxb_css_syntax_token_consume(tkz);
         continue;
      }
      if (in_func) {
         if (tok->type == LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS || tok->type == LXB_CSS_SYNTAX_TOKEN__EOF) {
            ok = (idx == 3);
            lxb_css_syntax_token_consume(tkz);
            break;
         }
         if (tok->type == LXB_CSS_SYNTAX_TOKEN_COMMA || tok->type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE ||
             tok->type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS) {
            lxb_css_syntax_token_consume(tkz);
            continue;
         }
         if ((tok->type == LXB_CSS_SYNTAX_TOKEN_NUMBER || tok->type == LXB_CSS_SYNTAX_TOKEN_PERCENTAGE) && idx < 3) {
            double v = tok->types.number.num;
            if (tok->type == LXB_CSS_SYNTAX_TOKEN_PERCENTAGE)
               v = v * 255.0 / 100.0;
            v = std::max(0.0, std::min(255.0, v));
            nums[idx++] = (int)std::lround(v);
            lxb_css_syntax_token_consume(tkz);
            continue;
         }
      }
      lxb_css_syntax_token_consume(tkz);
   }
   r = nums[0];
   g = nums[1];
   b = nums[2];
   lxb_css_syntax_tokenizer_destroy(tkz);
   return ok;
}

FlexShorthand parse_flex(const std::string& flexValue)
{
   FlexShorthand fp;
   if (flexValue.empty())
      return fp;
   lxb_css_syntax_tokenizer_t* tkz = lxb_css_syntax_tokenizer_create();
   if (!tkz)
      return fp;
   if (lxb_css_syntax_tokenizer_init(tkz) != LXB_STATUS_OK) {
      lxb_css_syntax_tokenizer_destroy(tkz);
      return fp;
   }
   lxb_css_syntax_tokenizer_buffer_set(tkz, (const lxb_char_t*)flexValue.data(), flexValue.size());
   int stage = 0;
   while (true) {
      lxb_css_syntax_token_t* tok = lxb_css_syntax_token(tkz);
      if (!tok)
         break;
      if (tok->type == LXB_CSS_SYNTAX_TOKEN__EOF)
         break;
      if (tok->type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE) {
         lxb_css_syntax_token_consume(tkz);
         continue;
      }
      std::string txt;
      const char* d = (const char*)tok->types.base.begin;
      size_t len = tok->types.base.length;
      if (len)
         txt.assign(d, len);
      switch (tok->type) {
      case LXB_CSS_SYNTAX_TOKEN_NUMBER: {
         if (stage == 0 && !fp.haveGrow) {
            try {
               fp.grow = std::stof(txt);
               if (fp.grow < 0)
                  fp.grow = 0;
               fp.haveGrow = true;
               stage = 1;
            }
            catch (...) {
            }
         }
         else if (stage <= 1 && !fp.haveShrink) {
            try {
               fp.shrink = std::stof(txt);
               fp.haveShrink = true;
               stage = 2;
            }
            catch (...) {
            }
         }
         else if (stage >= 1 && !fp.basisAuto && !fp.basisPercent && !fp.basisPoint) {
            try {
               fp.basisValue = std::stof(txt);
               fp.basisPoint = true;
               stage = 3;
            }
            catch (...) {
            }
         }
         break;
      }
      case LXB_CSS_SYNTAX_TOKEN_IDENT: {
         if (stage <= 2 && txt == "auto") {
            fp.basisAuto = true;
            stage = 3;
         }
         break;
      }
      case LXB_CSS_SYNTAX_TOKEN_PERCENTAGE: {
         if (stage <= 2 && !fp.basisPercent && !fp.basisPoint && !fp.basisAuto) {
            try {
               fp.basisValue = std::stof(txt);
               fp.basisPercent = true;
               stage = 3;
            }
            catch (...) {
            }
         }
         break;
      }
      case LXB_CSS_SYNTAX_TOKEN_DIMENSION: {
         if (stage <= 2 && !fp.basisPoint && !fp.basisPercent && !fp.basisAuto) {
            size_t i = 0;
            while (i < txt.size() &&
                   (std::isdigit((unsigned char)txt[i]) || txt[i] == '+' || txt[i] == '-' || txt[i] == '.'))
               ++i;
            std::string num = txt.substr(0, i);
            try {
               fp.basisValue = std::stof(num);
               fp.basisPoint = true;
               stage = 3;
            }
            catch (...) {
            }
         }
         break;
      }
      default:
         break;
      }
      lxb_css_syntax_token_consume(tkz);
   }
   lxb_css_syntax_tokenizer_destroy(tkz);
   if (fp.haveGrow && !fp.haveShrink && !fp.basisAuto && !fp.basisPercent && !fp.basisPoint)
      fp.basisAuto = true;
   return fp;
}
} // namespace css
