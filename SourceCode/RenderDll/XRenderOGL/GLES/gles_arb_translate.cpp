/*=============================================================================
  gles_arb_translate.cpp : ARB_vertex_program / ARB_fragment_program text to
  GLSL ES 3.00. Covers what cgc emits for arbvp1/arbfp1 (see GLES-PORT-PLAN.md).

  Interface shared with the fixed-function halves (gles_ffp.cpp):
    attributes a_pos, a_normal, a_color, a_color2, a_texN (vec4)
    varyings   v_color, v_color2, v_texN (vec4), v_fogc (float)
    uniforms   env[], u_texN samplers, u_rectScale[], u_fogColor, u_fogParams
=============================================================================*/
#include "gles_arb.h"
#include <ctype.h>
#include <stdlib.h>

namespace {

struct SParam
{
  std::string name;
  int count;                        // 0 for a scalar/vec param
  std::vector<std::string> elems;   // GLSL vec4 expressions per element
};

struct STranslator
{
  SARBProgram* p;
  bool vertex;
  std::string out;                  // main body
  std::string error;
  std::map<std::string, std::string> alias;   // ATTRIB/OUTPUT/ALIAS/scalar PARAM name -> GLSL expr
  std::map<std::string, SParam> arrays;       // PARAM arrays
  std::vector<std::string> temps;
  std::vector<std::string> addrs;
  bool usesOut[16];
  bool writesDepth;
  bool usesFragCoord;
  int texTarget[GLES_MAX_UNITS];

  STranslator(SARBProgram* prog) : p(prog), vertex(prog->vertex), writesDepth(false), usesFragCoord(false)
  {
    memset(usesOut, 0, sizeof(usesOut));
    memset(texTarget, 0, sizeof(texTarget));
  }

  bool Fail(const std::string& msg) { if (error.empty()) error = msg; return false; }

  void NoteEnv(int idx) { if (idx + 1 > p->maxEnv) p->maxEnv = idx + 1; }

  static std::string Trim(const std::string& s)
  {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
  }

  static std::vector<std::string> SplitTop(const std::string& s, char sep)
  {
    std::vector<std::string> r;
    int depth = 0;
    std::string cur;
    for (size_t i = 0; i < s.size(); i++)
    {
      char c = s[i];
      if (c == '{' || c == '[') depth++;
      if (c == '}' || c == ']') depth--;
      if (c == sep && depth == 0) { r.push_back(Trim(cur)); cur.clear(); }
      else cur += c;
    }
    if (!Trim(cur).empty()) r.push_back(Trim(cur));
    return r;
  }

  static std::string Num(const std::string& s)
  {
    // GLSL ES needs a decimal point or exponent on float literals
    std::string t = Trim(s);
    if (t.find_first_of(".eE") == std::string::npos) t += ".0";
    return t;
  }

  // "{ a, b, c, d }" or "1.0" -> vec4 constant
  std::string Constant(const std::string& s)
  {
    std::string t = Trim(s);
    std::vector<std::string> v;
    if (t[0] == '{')
      v = SplitTop(t.substr(1, t.rfind('}') - 1), ',');
    else
      v.push_back(t);
    for (size_t i = 0; i < v.size(); i++) v[i] = Num(v[i]);
    if (v.size() == 1) return "vec4(" + v[0] + ")";
    while (v.size() < 3) v.push_back("0.0");
    if (v.size() < 4) v.push_back("1.0");
    return "vec4(" + v[0] + ", " + v[1] + ", " + v[2] + ", " + v[3] + ")";
  }

  // Expand a binding like program.env[3] / program.env[0..9] / {const} / state.* into element expressions.
  bool ExpandBinding(const std::string& b, std::vector<std::string>& elems)
  {
    std::string t = Trim(b);
    if (t[0] == '{' || isdigit((unsigned char)t[0]) || t[0] == '-' || t[0] == '.')
    {
      elems.push_back(Constant(t));
      return true;
    }
    if (t.compare(0, 12, "program.env[") == 0 || t.compare(0, 14, "program.local[") == 0)
    {
      size_t lb = t.find('['), rb = t.find(']');
      std::string idx = t.substr(lb + 1, rb - lb - 1);
      size_t dots = idx.find("..");
      int a = atoi(idx.c_str()), e = a;
      if (dots != std::string::npos) e = atoi(idx.c_str() + dots + 2);
      for (int i = a; i <= e; i++)
      {
        char buf[32];
        sprintf(buf, "%s[%d]", p->vertex ? "envv" : "envf", i);
        elems.push_back(buf);
        NoteEnv(i);
      }
      return true;
    }
    if (t.compare(0, 6, "state.") == 0)
    {
      // Not produced by the cache; matrices are uploaded through env by the renderer.
      GLES_Log("GLES: ARB program uses unsupported %s", t.c_str());
      elems.push_back("vec4(0.0)");
      return true;
    }
    return Fail("unknown binding " + t);
  }

  bool Declare(const std::string& kw, const std::string& rest)
  {
    if (kw == "TEMP" || kw == "ADDRESS")
    {
      std::vector<std::string> names = SplitTop(rest, ',');
      for (size_t i = 0; i < names.size(); i++)
        (kw == "TEMP" ? temps : addrs).push_back(names[i]);
      return true;
    }
    if (kw == "ATTRIB" || kw == "OUTPUT" || kw == "ALIAS")
    {
      size_t eq = rest.find('=');
      if (eq == std::string::npos) return Fail(kw + " without =");
      std::string name = Trim(rest.substr(0, eq)), target = Trim(rest.substr(eq + 1));
      alias[name] = target;   // resolved when used
      return true;
    }
    if (kw == "PARAM")
    {
      size_t eq = rest.find('=');
      std::string decl = Trim(eq == std::string::npos ? rest : rest.substr(0, eq));
      std::string init = eq == std::string::npos ? "" : Trim(rest.substr(eq + 1));
      size_t lb = decl.find('[');
      if (lb != std::string::npos)
      {
        SParam a;
        a.name = Trim(decl.substr(0, lb));
        std::string sz = Trim(decl.substr(lb + 1, decl.find(']') - lb - 1));
        std::vector<std::string> items = SplitTop(init.substr(1, init.rfind('}') - 1), ',');
        for (size_t i = 0; i < items.size(); i++)
          if (!ExpandBinding(items[i], a.elems)) return false;
        a.count = sz.empty() ? (int)a.elems.size() : atoi(sz.c_str());
        while ((int)a.elems.size() < a.count) a.elems.push_back("vec4(0.0)");
        arrays[a.name] = a;
        return true;
      }
      std::vector<std::string> e;
      if (!ExpandBinding(init, e)) return false;
      alias[decl] = e[0];
      return true;
    }
    if (kw == "OPTION")
    {
      std::string o = Trim(rest);
      if (o == "ARB_fog_linear") p->fogMode = 1;
      else if (o == "ARB_fog_exp") p->fogMode = 2;
      else if (o == "ARB_fog_exp2") p->fogMode = 3;
      else if (o == "ARB_fragment_program_shadow" || o == "ARB_precision_hint_fastest" || o == "ARB_precision_hint_nicest") {}
      else if (o == "ARB_position_invariant") return Fail("ARB_position_invariant not supported");
      else GLES_Log("GLES: ARB OPTION %s ignored", o.c_str());
      return true;
    }
    return Fail("unknown declaration " + kw);
  }

  // Resolve a register name (no swizzle) to a GLSL expression. Handles array indexing.
  bool Resolve(const std::string& nameIn, std::string& expr)
  {
    std::string name = Trim(nameIn);
    // program.env[n] directly
    if (name.compare(0, 12, "program.env[") == 0 || name.compare(0, 14, "program.local[") == 0)
    {
      std::vector<std::string> e;
      if (!ExpandBinding(name, e)) return false;
      expr = e[0];
      return true;
    }
    if (name[0] == '{' || isdigit((unsigned char)name[0]) || (name[0] == '.' && name.size() > 1 && isdigit((unsigned char)name[1])))
    {
      expr = Constant(name);
      return true;
    }
    size_t lb = name.find('[');
    if (lb != std::string::npos && name.compare(0, 7, "vertex.") != 0 && name.compare(0, 9, "fragment.") != 0 && name.compare(0, 7, "result.") != 0)
    {
      std::string base = Trim(name.substr(0, lb));
      std::string idx = Trim(name.substr(lb + 1, name.rfind(']') - lb - 1));
      std::map<std::string, SParam>::iterator it = arrays.find(base);
      if (it == arrays.end()) return Fail("unknown array " + base);
      // relative addressing: A0.x + n / A0.x - n / A0.x
      size_t plus = idx.find('+'), minus = idx.find('-');
      if (idx.find("A") != std::string::npos || idx.find("a") != std::string::npos)
      {
        std::string reg = idx, off = "0";
        if (plus != std::string::npos) { reg = Trim(idx.substr(0, plus)); off = Trim(idx.substr(plus + 1)); }
        else if (minus != std::string::npos) { reg = Trim(idx.substr(0, minus)); off = "-" + Trim(idx.substr(minus + 1)); }
        size_t dot = reg.find('.');
        if (dot != std::string::npos) reg = reg.substr(0, dot);
        expr = base + "[" + reg + " + " + off + "]";
        return true;
      }
      int i = atoi(idx.c_str());
      if (i < 0 || i >= it->second.count) return Fail("array index out of range " + name);
      expr = base + "[" + idx + "]";
      return true;
    }
    std::map<std::string, std::string>::iterator al = alias.find(name);
    if (al != alias.end()) return Resolve(al->second, expr);
    for (size_t i = 0; i < temps.size(); i++) if (temps[i] == name) { expr = name; return true; }
    for (size_t i = 0; i < addrs.size(); i++) if (addrs[i] == name) { expr = name; return true; }
    // attributes
    if (vertex)
    {
      if (name == "vertex.position") { expr = "a_pos"; return true; }
      if (name == "vertex.normal") { expr = "a_normal"; return true; }
      if (name == "vertex.color" || name == "vertex.color.primary") { expr = "a_color"; return true; }
      if (name == "vertex.color.secondary") { expr = "a_color2"; return true; }
      if (name == "vertex.fogcoord") { expr = "vec4(0.0, 0.0, 0.0, 1.0)"; return true; }
      if (name.compare(0, 16, "vertex.texcoord[") == 0) { expr = "a_tex" + name.substr(16, name.find(']') - 16); return true; }
      if (name == "vertex.texcoord") { expr = "a_tex0"; return true; }
      if (name.compare(0, 14, "vertex.attrib[") == 0)
      {
        int n = atoi(name.c_str() + 14);
        if (n == 0) expr = "a_pos"; else if (n == 2) expr = "a_normal"; else if (n == 3) expr = "a_color";
        else if (n == 4) expr = "a_color2"; else if (n >= 8 && n < 16) { char b[16]; sprintf(b, "a_tex%d", n - 8); expr = b; }
        else return Fail("unsupported " + name);
        return true;
      }
      if (name == "result.position") { expr = "gl_Position"; return true; }
      if (name == "result.color" || name == "result.color.primary" || name == "result.color.front" || name == "result.color.front.primary") { usesOut[0] = true; expr = "v_color"; return true; }
      if (name == "result.color.secondary" || name == "result.color.front.secondary") { usesOut[1] = true; expr = "v_color2"; return true; }
      if (name == "result.fogcoord") { p->writesFog = true; expr = "v_fogc4"; return true; }
      if (name == "result.pointsize") { expr = "v_psize"; return true; }
      if (name.compare(0, 16, "result.texcoord[") == 0)
      {
        int n = atoi(name.c_str() + 16);
        if (n < 0 || n >= GLES_MAX_UNITS) return Fail("bad texcoord " + name);
        p->usesTexcoord[n] = true;
        char b[16]; sprintf(b, "v_tex%d", n); expr = b;
        return true;
      }
      if (name == "result.texcoord") { p->usesTexcoord[0] = true; expr = "v_tex0"; return true; }
    }
    else
    {
      if (name == "fragment.color" || name == "fragment.color.primary") { expr = "v_color"; return true; }
      if (name == "fragment.color.secondary") { expr = "v_color2"; return true; }
      if (name == "fragment.fogcoord") { expr = "vec4(v_fogc, 0.0, 0.0, 1.0)"; return true; }
      if (name == "fragment.position") { usesFragCoord = true; expr = "gl_FragCoord"; return true; }
      if (name.compare(0, 18, "fragment.texcoord[") == 0)
      {
        int n = atoi(name.c_str() + 18);
        if (n < 0 || n >= GLES_MAX_UNITS) return Fail("bad texcoord " + name);
        p->usesTexcoord[n] = true;
        char b[16]; sprintf(b, "v_tex%d", n); expr = b;
        return true;
      }
      if (name == "fragment.texcoord") { p->usesTexcoord[0] = true; expr = "v_tex0"; return true; }
      if (name == "result.color") { expr = "oColor"; return true; }
      if (name == "result.depth") { writesDepth = true; expr = "oDepth"; return true; }
    }
    return Fail("unknown operand " + name);
  }

  // Source operand: [-]reg[.swz]  -> vec4 expression
  bool Source(const std::string& sIn, std::string& expr)
  {
    std::string s = Trim(sIn);
    bool neg = false;
    if (s[0] == '-') { neg = true; s = Trim(s.substr(1)); }
    else if (s[0] == '+') s = Trim(s.substr(1));
    // constant literal: no swizzle
    std::string base = s, swz;
    if (s[0] != '{' && !(isdigit((unsigned char)s[0]) || s[0] == '.'))
    {
      // swizzle is the last ".xyzw" component if it is made of xyzw/rgba letters only
      size_t dot = s.rfind('.');
      if (dot != std::string::npos)
      {
        std::string tail = s.substr(dot + 1);
        bool isSwz = !tail.empty() && tail.size() <= 4;
        for (size_t i = 0; i < tail.size() && isSwz; i++) if (!strchr("xyzwrgba", tail[i])) isSwz = false;
        // "vertex.color.secondary" etc. have non-swizzle tails; "fragment.texcoord[0].x" has a swizzle
        if (isSwz) { base = s.substr(0, dot); swz = tail; }
      }
    }
    std::string e;
    if (!Resolve(base, e)) return false;
    // address registers are ints
    for (size_t i = 0; i < addrs.size(); i++) if (addrs[i] == base) { expr = e; return true; }
    if (!swz.empty())
    {
      for (size_t i = 0; i < swz.size(); i++) { if (swz[i] == 'r') swz[i] = 'x'; if (swz[i] == 'g') swz[i] = 'y'; if (swz[i] == 'b') swz[i] = 'z'; if (swz[i] == 'a') swz[i] = 'w'; }
      if (swz.size() == 1) swz = std::string(4, swz[0]);
      if (swz != "xyzw") e = "(" + e + ")." + swz;
    }
    expr = neg ? "(-" + e + ")" : e;
    return true;
  }

  // Destination: reg[.mask] -> (glsl name, mask)
  bool Dest(const std::string& dIn, std::string& name, std::string& mask)
  {
    std::string d = Trim(dIn);
    mask = "xyzw";
    size_t dot = d.rfind('.');
    if (dot != std::string::npos)
    {
      std::string tail = d.substr(dot + 1);
      bool isMask = !tail.empty() && tail.size() <= 4;
      for (size_t i = 0; i < tail.size() && isMask; i++) if (!strchr("xyzw", tail[i])) isMask = false;
      if (isMask) { mask = tail; d = d.substr(0, dot); }
    }
    return Resolve(d, name);
  }

  void Emit(const std::string& dst, const std::string& mask, const std::string& expr, bool sat)
  {
    std::string e = sat ? "clamp(" + expr + ", 0.0, 1.0)" : expr;
    if (mask == "xyzw") out += "  " + dst + " = " + e + ";\n";
    else out += "  " + dst + "." + mask + " = (" + e + ")." + mask + ";\n";
  }

  bool Instruction(const std::string& stmt)
  {
    size_t sp = stmt.find_first_of(" \t");
    std::string op = sp == std::string::npos ? stmt : stmt.substr(0, sp);
    std::string rest = sp == std::string::npos ? "" : Trim(stmt.substr(sp));
    bool sat = false;
    if (op.size() > 4 && op.compare(op.size() - 4, 4, "_SAT") == 0) { sat = true; op = op.substr(0, op.size() - 4); }
    std::vector<std::string> args = SplitTop(rest, ',');

    if (op == "KIL")
    {
      std::string a; if (!Source(args[0], a)) return false;
      out += "  if (any(lessThan(" + a + ", vec4(0.0)))) discard;\n";
      return true;
    }
    if (op == "ARL")
    {
      std::string a; if (!Source(args[1], a)) return false;
      std::string d = Trim(args[0]); size_t dot = d.find('.'); if (dot != std::string::npos) d = d.substr(0, dot);
      out += "  " + d + " = int(floor((" + a + ").x));\n";
      return true;
    }
    if (args.size() < 2) return Fail("bad instruction " + stmt);
    std::string dst, mask;
    if (!Dest(args[0], dst, mask)) return false;
    std::string s[3];
    int ns = (int)args.size() - 1;
    if (op == "TEX" || op == "TXP" || op == "TXB")
    {
      // TEX dst, coord, texture[n], target
      if (args.size() != 4) return Fail("bad " + op);
      std::string coord; if (!Source(args[1], coord)) return false;
      std::string tex = Trim(args[2]);
      int unit = atoi(tex.c_str() + tex.find('[') + 1);
      if (unit < 0 || unit >= GLES_MAX_UNITS) return Fail("bad texture unit in " + stmt);
      std::string target = Trim(args[3]);
      int tt = ARB_TEX_2D;
      if (target == "RECT") tt = ARB_TEX_RECT; else if (target == "CUBE") tt = ARB_TEX_CUBE; else if (target == "3D") tt = ARB_TEX_3D;
      else if (target == "SHADOW2D") tt = ARB_TEX_SHADOW2D; else if (target == "SHADOWRECT") tt = ARB_TEX_SHADOWRECT;
      else if (target != "2D" && target != "1D") return Fail("unsupported texture target " + target);
      if (texTarget[unit] && texTarget[unit] != tt) return Fail("texture unit used with two targets");
      texTarget[unit] = tt;
      char u[16]; sprintf(u, "u_tex%d", unit);
      char rs[32]; sprintf(rs, "u_rectScale[%d]", unit);
      std::string c = "(" + coord + ")";
      std::string e;
      switch (tt)
      {
        case ARB_TEX_2D:
          e = (op == "TXP") ? "textureProj(" + std::string(u) + ", " + c + ".xyw)" :
              (op == "TXB") ? "texture(" + std::string(u) + ", " + c + ".xy, " + c + ".w)" : "texture(" + std::string(u) + ", " + c + ".xy)";
          break;
        case ARB_TEX_RECT:
          e = (op == "TXP") ? "texture(" + std::string(u) + ", " + c + ".xy / " + c + ".w * " + rs + ")" : "texture(" + std::string(u) + ", " + c + ".xy * " + rs + ")";
          break;
        case ARB_TEX_CUBE: case ARB_TEX_3D:
          e = (op == "TXP") ? "texture(" + std::string(u) + ", " + c + ".xyz / " + c + ".w)" : "texture(" + std::string(u) + ", " + c + ".xyz)";
          break;
        case ARB_TEX_SHADOW2D:
          e = "vec4(vec3(" + ((op == "TXP") ? "textureProj(" + std::string(u) + ", " + c + ")" : "texture(" + std::string(u) + ", " + c + ".xyz)") + "), 1.0)";
          break;
        case ARB_TEX_SHADOWRECT:
          e = "vec4(vec3(texture(" + std::string(u) + ", vec3(" + c + ".xy * " + rs + ", " + c + ".z))), 1.0)";
          break;
      }
      Emit(dst, mask, e, sat);
      return true;
    }
    for (int i = 0; i < ns && i < 3; i++) if (!Source(args[i + 1], s[i])) return false;
    std::string e;
    if (op == "MOV") e = s[0];
    else if (op == "ABS") e = "abs(" + s[0] + ")";
    else if (op == "FLR") e = "floor(" + s[0] + ")";
    else if (op == "FRC") e = "fract(" + s[0] + ")";
    else if (op == "ADD") e = s[0] + " + " + s[1];
    else if (op == "SUB") e = s[0] + " - " + s[1];
    else if (op == "MUL") e = s[0] + " * " + s[1];
    else if (op == "MAD") e = s[0] + " * " + s[1] + " + " + s[2];
    else if (op == "MIN") e = "min(" + s[0] + ", " + s[1] + ")";
    else if (op == "MAX") e = "max(" + s[0] + ", " + s[1] + ")";
    else if (op == "DP3") e = "vec4(dot((" + s[0] + ").xyz, (" + s[1] + ").xyz))";
    else if (op == "DP4") e = "vec4(dot(" + s[0] + ", " + s[1] + "))";
    else if (op == "DPH") e = "vec4(dot((" + s[0] + ").xyz, (" + s[1] + ").xyz) + (" + s[1] + ").w)";
    else if (op == "XPD") e = "vec4(cross((" + s[0] + ").xyz, (" + s[1] + ").xyz), 1.0)";
    else if (op == "DST") e = "vec4(1.0, (" + s[0] + ").y * (" + s[1] + ").y, (" + s[0] + ").z, (" + s[1] + ").w)";
    else if (op == "SGE") e = "vec4(greaterThanEqual(" + s[0] + ", " + s[1] + "))";
    else if (op == "SLT") e = "vec4(lessThan(" + s[0] + ", " + s[1] + "))";
    else if (op == "RCP") e = "vec4(1.0 / (" + s[0] + ").x)";
    else if (op == "RSQ") e = "vec4(inversesqrt(abs((" + s[0] + ").x)))";
    else if (op == "EX2") e = "vec4(exp2((" + s[0] + ").x))";
    else if (op == "LG2") e = "vec4(log2((" + s[0] + ").x))";
    else if (op == "EXP") e = "vec4(exp2(floor((" + s[0] + ").x)), fract((" + s[0] + ").x), exp2((" + s[0] + ").x), 1.0)";
    else if (op == "LOG") e = "vec4(floor(log2(abs((" + s[0] + ").x))), abs((" + s[0] + ").x) / exp2(floor(log2(abs((" + s[0] + ").x)))), log2(abs((" + s[0] + ").x)), 1.0)";
    else if (op == "POW") e = "vec4(pow(max((" + s[0] + ").x, 0.0), (" + s[1] + ").x))";
    else if (op == "LIT") e = "vec4(1.0, max((" + s[0] + ").x, 0.0), ((" + s[0] + ").x > 0.0) ? pow(max((" + s[0] + ").y, 0.0), clamp((" + s[0] + ").w, -128.0, 128.0)) : 0.0, 1.0)";
    else if (op == "CMP") e = "mix(" + s[2] + ", " + s[1] + ", vec4(lessThan(" + s[0] + ", vec4(0.0))))";
    else if (op == "LRP") e = "mix(" + s[2] + ", " + s[1] + ", " + s[0] + ")";
    else if (op == "COS") e = "vec4(cos((" + s[0] + ").x))";
    else if (op == "SIN") e = "vec4(sin((" + s[0] + ").x))";
    else if (op == "SCS") e = "vec4(cos((" + s[0] + ").x), sin((" + s[0] + ").x), 0.0, 0.0)";
    else if (op == "SWZ") return Fail("SWZ not supported");
    else return Fail("unknown opcode " + op);
    Emit(dst, mask, e, sat);
    return true;
  }

  bool Run()
  {
    std::string src = p->source;
    // strip comments
    std::string clean;
    for (size_t i = 0; i < src.size(); )
    {
      if (src[i] == '#') { while (i < src.size() && src[i] != '\n') i++; continue; }
      clean += src[i++];
    }
    size_t hdr = clean.find("!!ARB");
    if (hdr == std::string::npos) return Fail("missing !!ARB header");
    size_t eol = clean.find('\n', hdr);
    std::string header = Trim(clean.substr(hdr, eol - hdr));
    if (vertex && header.compare(0, 8, "!!ARBvp1") != 0) return Fail("not a vertex program: " + header);
    if (!vertex && header.compare(0, 8, "!!ARBfp1") != 0) return Fail("not a fragment program: " + header);
    clean = clean.substr(eol + 1);

    std::vector<std::string> stmts = SplitTop(clean, ';');
    for (size_t i = 0; i < stmts.size(); i++)
    {
      std::string s = Trim(stmts[i]);
      if (s.empty() || s == "END") continue;
      size_t sp = s.find_first_of(" \t");
      std::string kw = sp == std::string::npos ? s : s.substr(0, sp);
      std::string rest = sp == std::string::npos ? "" : Trim(s.substr(sp));
      bool ok;
      if (kw == "TEMP" || kw == "ADDRESS" || kw == "ATTRIB" || kw == "OUTPUT" || kw == "ALIAS" || kw == "PARAM" || kw == "OPTION")
        ok = Declare(kw, rest);
      else
        ok = Instruction(s);
      if (!ok) return false;
    }
    return true;
  }

  std::string Assemble()
  {
    std::string s = "#version 300 es\n";
    char buf[128];
    if (vertex)
    {
      s += "in vec4 a_pos; in vec4 a_normal; in vec4 a_color; in vec4 a_color2;\n";
      for (int i = 0; i < GLES_MAX_UNITS; i++) { sprintf(buf, "in vec4 a_tex%d;\n", i); s += buf; }
      s += "out vec4 v_color; out vec4 v_color2; out float v_fogc;\n";
      for (int i = 0; i < GLES_MAX_UNITS; i++) { sprintf(buf, "out vec4 v_tex%d;\n", i); s += buf; }
    }
    else
    {
      s += "precision highp float;\nprecision highp sampler3D;\nprecision highp sampler2DShadow;\n";
      s += "in vec4 v_color; in vec4 v_color2; in float v_fogc;\n";
      for (int i = 0; i < GLES_MAX_UNITS; i++) { sprintf(buf, "in vec4 v_tex%d;\n", i); s += buf; }
      for (int i = 0; i < GLES_MAX_UNITS; i++)
      {
        const char* type = NULL;
        switch (texTarget[i])
        {
          case ARB_TEX_2D: case ARB_TEX_RECT: type = "sampler2D"; break;
          case ARB_TEX_CUBE: type = "samplerCube"; break;
          case ARB_TEX_3D: type = "sampler3D"; break;
          case ARB_TEX_SHADOW2D: case ARB_TEX_SHADOWRECT: type = "sampler2DShadow"; break;
        }
        if (type) { sprintf(buf, "uniform %s u_tex%d;\n", type, i); s += buf; }
      }
      s += "uniform vec2 u_rectScale[8]; uniform vec4 u_fogColor; uniform vec4 u_fogParams;\nout vec4 fragColor;\n";
    }
    sprintf(buf, "uniform vec4 %s[%d];\n", vertex ? "envv" : "envf", p->maxEnv > 0 ? p->maxEnv : 1);
    s += buf;
    s += "void main() {\n";
    for (size_t i = 0; i < temps.size(); i++) s += "  vec4 " + temps[i] + " = vec4(0.0);\n";
    for (size_t i = 0; i < addrs.size(); i++) s += "  int " + addrs[i] + " = 0;\n";
    for (std::map<std::string, SParam>::iterator it = arrays.begin(); it != arrays.end(); ++it)
    {
      sprintf(buf, "  vec4 %s[%d];\n", it->first.c_str(), it->second.count);
      s += buf;
      for (int i = 0; i < it->second.count; i++)
      {
        sprintf(buf, "  %s[%d] = ", it->first.c_str(), i);
        s += buf + it->second.elems[i] + ";\n";
      }
    }
    if (vertex)
    {
      s += "  vec4 v_fogc4 = vec4(0.0); vec4 v_psize = vec4(1.0);\n";
      s += "  v_color = vec4(1.0); v_color2 = vec4(0.0, 0.0, 0.0, 1.0);\n";
      for (int i = 0; i < GLES_MAX_UNITS; i++) { sprintf(buf, "  v_tex%d = vec4(0.0, 0.0, 0.0, 1.0);\n", i); s += buf; }
      s += out;
      // ARB clamps colour results to [0,1] on write; a light-attenuation term written unclamped
      // (Far Cry's tracer: c.x - dist*c.y with c = 1e8, 1e-8) otherwise saturates the whole primitive.
      s += "  v_color = clamp(v_color, 0.0, 1.0); v_color2 = clamp(v_color2, 0.0, 1.0);\n";
      s += "  v_fogc = v_fogc4.x;\n";
    }
    else
    {
      s += "  vec4 oColor = vec4(0.0); vec4 oDepth = vec4(0.0);\n";
      s += out;
      s += "  oColor = clamp(oColor, 0.0, 1.0);\n"; // result.color is clamped before fog, as in ARB
      if (p->fogMode == 1) s += "  float fogf = clamp((u_fogParams.y - v_fogc) * u_fogParams.z, 0.0, 1.0);\n";
      else if (p->fogMode == 2) s += "  float fogf = clamp(exp(-u_fogParams.w * v_fogc), 0.0, 1.0);\n";
      else if (p->fogMode == 3) s += "  float fogf = clamp(exp(-u_fogParams.w * u_fogParams.w * v_fogc * v_fogc), 0.0, 1.0);\n";
      if (p->fogMode) s += "  oColor.rgb = mix(u_fogColor.rgb, oColor.rgb, fogf);\n";
      // FARCRY_GLES_UVDEBUG=1: every ARB fragment program shows its texcoord 0 instead (red = u, green = v).
      static bool uvDebug = getenv("FARCRY_GLES_UVDEBUG") != NULL;
      if (uvDebug) s += "  oColor = vec4((isnan(v_tex0.x) || isnan(v_tex0.y) || isinf(v_tex0.x) || isinf(v_tex0.y)) ? 1.0 : 0.0, fract(v_tex0.x), fract(v_tex0.y), 1.0);\n";
      s += "  fragColor = oColor;\n";
      if (writesDepth) s += "  gl_FragDepth = oDepth.z;\n";
    }
    s += "}\n";
    return s;
  }
};

} // namespace

bool GLES_ARB_Translate(SARBProgram& p, std::string& error)
{
  p.maxEnv = 0;
  p.fogMode = 0;
  p.writesFog = false;
  memset(p.usesTexcoord, 0, sizeof(p.usesTexcoord));
  memset(p.texTarget, 0, sizeof(p.texTarget));
  STranslator t(&p);
  if (!t.Run())
  {
    error = t.error;
    return false;
  }
  for (int i = 0; i < GLES_MAX_UNITS; i++) p.texTarget[i] = (unsigned char)t.texTarget[i];
  p.glsl = t.Assemble();
  return true;
}
