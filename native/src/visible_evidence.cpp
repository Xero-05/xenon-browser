#include "xenon/visible_evidence.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xenon {
namespace {
constexpr size_t kMaxDocuments = 64, kMaxNodes = 30000, kMaxLayouts = 30000;
constexpr size_t kMaxTextBoxes = 60000, kMaxStringBytes = 1024 * 1024;
constexpr size_t kMaxNameBytes = 2048, kMaxWork = 3000000;
constexpr double kMinOpacity = 0.5, kMinContrast = 1.5, kMinFontSize = 6;
const std::vector<std::string> kStyles = {
  "display", "visibility", "opacity", "color", "background-color", "font-size",
  "-webkit-text-fill-color", "overflow-x", "overflow-y", "clip", "clip-path",
  "filter", "backdrop-filter", "transform", "perspective", "mask-image",
  "-webkit-mask-image", "mix-blend-mode", "background-image", "text-shadow",
  "-webkit-text-stroke-width", "letter-spacing", "word-spacing", "text-overflow",
  "-webkit-line-clamp", "-webkit-text-security", "content-visibility", "zoom",
  "border-top-width", "border-right-width", "border-bottom-width", "border-left-width",
  "padding-top", "padding-right", "padding-bottom", "padding-left",
  "border-top-color", "border-right-color", "border-bottom-color", "border-left-color",
  "box-shadow", "outline-width", "outline-offset", "outline-color", "outline-style"
};
enum Style { Display, Visibility, Opacity, Color, Background, FontSize, TextFill,
  OverflowX, OverflowY, Clip, ClipPath, Filter, BackdropFilter, Transform,
  Perspective, MaskImage, WebkitMaskImage, MixBlendMode, BackgroundImage,
  TextShadow, TextStrokeWidth, LetterSpacing, WordSpacing, TextOverflow,
  LineClamp, TextSecurity, ContentVisibility, Zoom, BorderTop, BorderRight,
  BorderBottom, BorderLeft, PaddingTop, PaddingRight, PaddingBottom, PaddingLeft,
  BorderTopColor, BorderRightColor, BorderBottomColor, BorderLeftColor, BoxShadow,
  OutlineWidth, OutlineOffset, OutlineColor, OutlineStyle };

const Json kNull;
const Json& at(const Json& value, std::string_view key) {
  if (!value.is_object()) return kNull;
  const auto it = value.find(std::string(key)); return it == value.end() ? kNull : *it;
}
const Json& item(const Json& value, size_t index) {
  return value.is_array() && index < value.size() ? value[index] : kNull;
}
int integer(const Json& value, int fallback = -1) {
  if (!value.is_number_integer()) return fallback;
  const auto number = value.get<int64_t>();
  return number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max()
    ? static_cast<int>(number) : fallback;
}
std::optional<double> number(const Json& value) {
  if (!value.is_number()) return {};
  const double result = value.get<double>(); return std::isfinite(result) ? std::optional(result) : std::nullopt;
}
std::optional<double> css_number(const std::string& value, bool pixels = false) {
  if (value.empty() || value.size() > 64) return {};
  char* end{}; const double parsed = std::strtod(value.c_str(), &end);
  if (end == value.c_str() || !std::isfinite(parsed)) return {};
  if (*end && !(pixels && std::string_view(end) == "px")) return {};
  return parsed;
}
struct Rect { double x{}, y{}, w{}, h{}; };
std::optional<Rect> rect(const Json& value) {
  std::array<std::optional<double>, 4> v;
  if (value.is_array() && value.size() == 4)
    for (size_t i = 0; i < 4; ++i) v[i] = number(value[i]);
  else if (value.is_object()) v = {number(at(value, "x")), number(at(value, "y")), number(at(value, "width")), number(at(value, "height"))};
  else return {};
  for (auto n : v) if (!n || std::abs(*n) > 1e8) return {};
  if (*v[2] <= 0 || *v[3] <= 0) return {};
  return Rect{*v[0], *v[1], *v[2], *v[3]};
}
bool contains(const Rect& outer, const Rect& inner) {
  return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w && inner.y + inner.h <= outer.y + outer.h;
}
bool overlaps(const Rect& a, const Rect& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
Json encode_rect(const Rect& r) { return {{"x", r.x}, {"y", r.y}, {"width", r.w}, {"height", r.h}, {"coordinateSpace", "frame-document-css"}}; }
struct Rgba { double r{}, g{}, b{}, a{}; };
std::optional<Rgba> color(std::string value) {
  if (value == "transparent") return Rgba{};
  // Chromium serializes computed sRGB colors as rgb()/rgba(). Wide-gamut and
  // other color spaces are deliberately unsupported, not guessed as black.
  size_t offset = value.starts_with("rgba(") ? 5 : value.starts_with("rgb(") ? 4 : 0;
  if (!offset || value.back() != ')' || value.size() > 128) return {};
  value = value.substr(offset, value.size() - offset - 1);
  for (char& c : value) if (c == ',' || c == '/') c = ' ';
  const char* cursor = value.c_str(); std::vector<double> parts;
  while (*cursor) {
    while (*cursor == ' ') ++cursor;
    if (!*cursor) break;
    char* end{}; double v = std::strtod(cursor, &end);
    if (end == cursor || !std::isfinite(v)) return {};
    if (*end == '%') { v *= parts.size() < 3 ? 2.55 : .01; ++end; }
    if (*end && *end != ' ') return {};
    parts.push_back(v); cursor = end;
    if (parts.size() > 4) return {};
  }
  if (parts.size() != 3 && parts.size() != 4) return {};
  for (size_t i = 0; i < 3; ++i) if (parts[i] < 0 || parts[i] > 255.00001) return {};
  double alpha = parts.size() == 4 ? parts[3] : 1;
  if (alpha < 0 || alpha > 1) return {};
  return Rgba{parts[0] / 255, parts[1] / 255, parts[2] / 255, alpha};
}
double luminance(const Rgba& c) {
  const auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
  return .2126 * linear(c.r) + .7152 * linear(c.g) + .0722 * linear(c.b);
}
double contrast(Rgba fg, const Rgba& bg, double opacity) {
  const double alpha = fg.a * opacity;
  fg = {fg.r * alpha + bg.r * (1-alpha), fg.g * alpha + bg.g * (1-alpha), fg.b * alpha + bg.b * (1-alpha), 1};
  const double a = luminance(fg), b = luminance(bg);
  return (std::max(a,b) + .05) / (std::min(a,b) + .05);
}
std::optional<double> brightness_gain(const std::string& value) {
  if(value=="none")return 1;
  if(value.empty() || value.size()>512)return {};
  double gain=1;size_t start=0;
  while(start<value.size()) {
    if(value.compare(start,11,"brightness(")!=0)return {};
    const auto end=value.find(')',start+11);if(end==std::string::npos)return {};
    auto argument=value.substr(start+11,end-start-11);const bool percent=!argument.empty() && argument.back()=='%';
    if(percent)argument.pop_back();const auto parsed=css_number(argument);if(!parsed)return {};
    const double amount=*parsed/(percent?100:1);if(amount<0 || amount>1)return {};gain*=amount;
    start=end+1;if(start<value.size()) {if(value[start]!=' ')return {};while(start<value.size() && value[start]==' ')++start;}
  }
  return gain;
}
double darkened_contrast(Rgba fg, const Rgba& bg, double opacity, double gain) {
  if(gain==1)return contrast(fg,bg,opacity);
  const double alpha=fg.a*opacity;
  // Foreground passes through every ancestor brightness filter. Background
  // can be painted inside or outside those layers; bound each channel rather
  // than guessing which layer supplied a blended snapshot background.
  const Rgba low_bg{bg.r*gain,bg.g*gain,bg.b*gain,1};
  const Rgba low_ink{fg.r*gain*alpha+low_bg.r*(1-alpha),fg.g*gain*alpha+low_bg.g*(1-alpha),fg.b*gain*alpha+low_bg.b*(1-alpha),1};
  const Rgba high_ink{fg.r*gain*alpha+bg.r*(1-alpha),fg.g*gain*alpha+bg.g*(1-alpha),fg.b*gain*alpha+bg.b*(1-alpha),1};
  const double bg_low=luminance(low_bg),bg_high=luminance(bg),ink_low=luminance(low_ink),ink_high=luminance(high_ink);
  if(ink_high<bg_low)return (bg_low+.05)/(ink_high+.05);
  if(bg_high<ink_low)return (ink_low+.05)/(bg_high+.05);
  return 1; // overlapping luminance intervals cannot prove readable contrast
}
std::optional<Rect> box_shadow_bounds(const std::string& value, const Rect& box, bool text_shadow=false) {
  if(value=="none")return box;
  if(value.empty() || value.size()>4096)return {};
  Rect bounds=box;std::vector<std::string> tokens;size_t shadows=0;
  auto shadow=[&]() {
    if(tokens.empty() || tokens.size()>6 || ++shadows>64)return false;
    bool inset=false;std::optional<Rgba> rgba;std::vector<double> lengths;
    for(const auto& token:tokens) {
      if(token=="inset") {if(inset || text_shadow)return false;inset=true;}
      else if(const auto parsed=color(token)) {if(rgba)return false;rgba=parsed;}
      else {const auto length=css_number(token,true);if(!length || std::abs(*length)>1e8)return false;lengths.push_back(*length);}
    }
    // Computed Chromium shadows have an sRGB color and two offsets, with
    // optional blur/spread. Unsupported colors or syntax remain unbounded.
    if(!rgba || lengths.size()<2 || lengths.size()>(text_shadow?3:4))return false;
    const double blur=lengths.size()>2?lengths[2]:0,spread=lengths.size()>3?lengths[3]:0;
    if(blur<0)return false;
    if(inset || rgba->a==0)return true;
    // Blink uses ceil(3*sigma), where sigma is half the CSS blur radius
    // (core/style/shadow_data.{h,cc}). Retain a rounding margin and never
    // shrink for negative spread, so these are conservative ink bounds.
    const double expand=std::ceil(1.5*blur)+std::max(0.0,spread)+1;
    const Rect ink{box.x+lengths[0]-expand,box.y+lengths[1]-expand,box.w+2*expand,box.h+2*expand};
    const double right=std::max(bounds.x+bounds.w,ink.x+ink.w),bottom=std::max(bounds.y+bounds.h,ink.y+ink.h);
    bounds.x=std::min(bounds.x,ink.x);bounds.y=std::min(bounds.y,ink.y);bounds.w=right-bounds.x;bounds.h=bottom-bounds.y;
    return true;
  };
  // Split only outside color parentheses: rgba() contains commas and spaces.
  size_t start=0;int depth=0;
  for(size_t i=0;i<=value.size();++i) {
    const char c=i<value.size()?value[i]:',';
    if(c=='(') {if(++depth>1)return {};}
    else if(c==')') {if(--depth<0)return {};}
    if(depth || (c!=' ' && c!=','))continue;
    if(i>start)tokens.push_back(value.substr(start,i-start));start=i+1;
    if(c==',') {if(!shadow())return {};tokens.clear();}
  }
  if(depth)return {};
  return bounds;
}
struct TextUnits { std::vector<size_t> offsets; bool safe{true}; };
TextUnits utf16_offsets(const std::string& text) {
  TextUnits out; out.offsets.push_back(0);
  for (size_t i = 0; i < text.size();) {
    const size_t start = i; const auto first = static_cast<unsigned char>(text[i++]);
    uint32_t cp{}; int remaining{};
    if (first < 0x80) cp = first;
    else if (first >= 0xC2 && first <= 0xDF) { cp = first & 31; remaining = 1; }
    else if (first >= 0xE0 && first <= 0xEF) { cp = first & 15; remaining = 2; }
    else if (first >= 0xF0 && first <= 0xF4) { cp = first & 7; remaining = 3; }
    else { out.safe = false; return out; }
    for (int j = 0; j < remaining; ++j) {
      if (i >= text.size() || (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) { out.safe = false; return out; }
      cp = (cp << 6) | (static_cast<unsigned char>(text[i++]) & 63);
    }
    if ((remaining == 1 && cp < 0x80) || (remaining == 2 && cp < 0x800) || (remaining == 3 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { out.safe = false; return out; }
    // Do not silently strip controls: joining the remaining characters can
    // manufacture different text. Omit the affected run and report a gap.
    if ((cp < 0x20 && cp != 9 && cp != 10 && cp != 13) || (cp >= 0x7F && cp <= 0x9F) || cp == 0xAD || cp == 0x061C || cp == 0x180E ||
        (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2060 && cp <= 0x206F) || cp == 0xFEFF ||
        (cp >= 0xFFF9 && cp <= 0xFFFB) || (cp >= 0xE0000 && cp <= 0xE0FFF)) out.safe = false;
    if (cp > 0xFFFF) out.offsets.push_back(std::numeric_limits<size_t>::max()); // middle of a surrogate pair
    out.offsets.push_back(i);
    if (i <= start) { out.safe = false; return out; }
  }
  return out;
}
std::string normalized_space(std::string value) {
  std::string out; bool space = true;
  for (unsigned char c : value) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { if (!space) out.push_back(' '); space = true; }
    else { out.push_back(static_cast<char>(c)); space = false; }
  }
  if (!out.empty() && out.back() == ' ') out.pop_back(); return out;
}
struct Layout {
  int node{-1}, paint{-1}; std::optional<Rect> box;
  std::array<std::string, 45> styles;
  std::string text, blended;
  std::optional<double> text_opacity;
  bool styles_complete{}, basic{}, potentially_painted{}, unbounded_paint{};
  std::optional<Rect> paint_box;
  double effective_opacity{};
};
struct Document {
  std::string frame; int parent_doc{-1}, owner_node{-1};
  std::optional<Rect> viewport;
  std::vector<int> parents, backend, node_layout, node_types;
  std::vector<bool> editable, svg;
  std::vector<bool> fragmented, fragment_inconsistent;
  std::vector<std::vector<int>> fragments;
  std::vector<std::string> tags, types, ids, label_for;
  std::vector<Layout> layouts;
  const Json* source{};
};
struct FilterState {
  const Json& strings; Json result{{"frames", Json::object()}, {"omitted", Json::object()}, {"partial", false}};
  bool human_diagnostics{};std::set<std::string> unsupported_transforms,unsupported_paint;
  size_t work{}; bool exhausted{};
  void omit(const char* reason, size_t count=1) {
    auto& v = result["omitted"][reason]; v = v.is_number_unsigned() ? v.get<size_t>() + count : count; result["partial"] = true;
  }
  bool step() { if (++work <= kMaxWork) return true; if (!exhausted) omit("workLimit"); exhausted = true; return false; }
  std::string str(const Json& index) {
    const int i = integer(index); if (i < 0 || !strings.is_array() || static_cast<size_t>(i) >= strings.size() || !strings[i].is_string()) return {};
    const auto& s = strings[i].get_ref<const std::string&>();
    if (s.size() > kMaxStringBytes) { omit("stringLimit"); return {}; } return s;
  }
};
bool ancestor(const Document& d, int parent, int child) {
  for (int depth=0; child >= 0 && static_cast<size_t>(child) < d.parents.size() && depth < 128; ++depth) {
    if (parent == child) return true; child = d.parents[child];
  }
  return false;
}
std::optional<std::pair<double,double>> axis_transform(const std::string& value) {
  if(value=="none" || value=="matrix3d(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)")return std::pair{1.0,1.0};
  // Snapshot bounds already include a 2D translation. It preserves axes,
  // dimensions, clipping and shadow radii; never apply the offset a second time.
  if(!value.starts_with("matrix(") || !value.ends_with(")") || value.size()>256)return {};
  std::array<double,6> values{};size_t begin=7;
  for(size_t i=0;i<values.size();++i) {
    const size_t end=i+1==values.size()?value.size()-1:value.find(',',begin);
    if(end==std::string::npos || end<=begin)return {};
    auto part=value.substr(begin,end-begin);while(!part.empty()&&part.back()==' ')part.pop_back();
    const auto parsed=css_number(part);if(!parsed || std::abs(*parsed)>1e8)return {};
    values[i]=*parsed;begin=end+1;
  }
  if(std::abs(values[0])==1 && values[1]==0 && values[2]==0 && std::abs(values[3])==1)return std::pair{values[0],values[3]};
  return {};
}
bool unscaled_transform(const std::string& value) {
  const auto axes=axis_transform(value);return axes&&axes->first==1&&axes->second==1;
}
std::optional<Rect> inset_clip(const std::string& value,const Rect& box,bool* empty=nullptr,bool interior=true) {
  if(value=="none")return box;
  if(!value.starts_with("inset(")||!value.ends_with(")")||value.size()>256)return {};
  std::vector<std::string> tokens;size_t start=6;
  for(size_t i=6;i<value.size();++i)if(value[i]==' '||i==value.size()-1){if(i>start)tokens.push_back(value.substr(start,i-start));start=i+1;}
  std::optional<double> radius;
  if(tokens.size()>=3 && tokens[tokens.size()-2]=="round"){
    radius=css_number(tokens.back(),true);if(!radius || *radius<0 || *radius>1e6)return {};
    tokens.resize(tokens.size()-2);
  }
  if(tokens.empty()||tokens.size()>4)return {};
  std::array<std::string,4> sides{tokens[0],tokens.size()>1?tokens[1]:tokens[0],tokens.size()>2?tokens[2]:tokens[0],tokens.size()>3?tokens[3]:tokens.size()>1?tokens[1]:tokens[0]};
  std::array<double,4> offsets{};
  for(size_t i=0;i<4;++i){auto token=sides[i];const bool percent=token.ends_with('%');if(percent)token.pop_back();const auto n=css_number(token,!percent);if(!n||std::abs(*n)>1e6)return {};offsets[i]=*n*(percent?(i%2?box.w:box.h)/100:1);}
  if(box.w-offsets[1]-offsets[3]<=0||box.h-offsets[0]-offsets[2]<=0){if(empty)*empty=true;return {};}
  Rect clipped{box.x+offsets[3],box.y+offsets[0],box.w-offsets[1]-offsets[3],box.h-offsets[0]-offsets[2]};
  if(radius && interior){
    // A rectangle inscribed in the rounded clip proves every admitted pixel.
    // Use outer bounds only for possible candidates; never discard the curves
    // as if they were known invisible. Other radius syntax remains unsupported.
    const double inset=std::min(*radius,std::min(clipped.w,clipped.h)/2)*(1-std::sqrt(0.5));
    clipped={clipped.x+inset,clipped.y+inset,clipped.w-2*inset,clipped.h-2*inset};
  }
  return clipped;
}
std::optional<double> blur_extent(const std::string& value) {
  if(!value.starts_with("blur(")||!value.ends_with(")")||value.size()>64)return {};
  const auto radius=css_number(value.substr(5,value.size()-6),true);
  if(!radius||*radius<0||*radius>1e5)return {};
  // Unlike box-shadow's blur radius, filter blur specifies sigma directly.
  return std::ceil(*radius*3)+1;
}
bool bounded_color_filter(const std::string& value) {
  if(brightness_gain(value))return true;
  if(!value.starts_with("invert(")||!value.ends_with(")")||value.size()>64)return false;
  auto argument=value.substr(7,value.size()-8);const bool percent=argument.ends_with('%');if(percent)argument.pop_back();
  const auto amount=css_number(argument);return amount&&*amount>=0&&*amount<=(percent?100:1);
}
std::optional<double> paint_transform_expansion(const std::string& value) {
  if(value=="none"||value=="matrix3d(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)")return 1;
  if(!value.starts_with("matrix(")||!value.ends_with(")")||value.size()>256)return {};
  std::array<double,6> v{};size_t begin=7;
  for(size_t i=0;i<v.size();++i){const auto end=i+1==v.size()?value.size()-1:value.find(',',begin);if(end==std::string::npos||end<=begin)return {};
    auto part=value.substr(begin,end-begin);while(!part.empty()&&part.back()==' ')part.pop_back();const auto n=css_number(part);if(!n||std::abs(*n)>1e6)return {};v[i]=*n;begin=end+1;}
  // Infinity norm bounds the transformed blur support. Snapshot source bounds
  // already contain the transform; only its expansion is multiplied here.
  return std::max({1.0,std::abs(v[0])+std::abs(v[2]),std::abs(v[1])+std::abs(v[3])});
}
bool basic_style(const Layout& l) {
  if (!l.styles_complete || !l.box || l.styles[Display] == "none" || l.styles[Visibility] != "visible" || l.styles[ContentVisibility] == "hidden") return false;
  if(!inset_clip(l.styles[ClipPath],*l.box,nullptr,false))return false;
  for (int s : {Perspective, MaskImage, WebkitMaskImage}) if (l.styles[s] != "none") return false;
  if(!axis_transform(l.styles[Transform]))return false;
  if(!brightness_gain(l.styles[Filter]))return false;
  if (l.styles[Clip] != "auto" || l.styles[MixBlendMode] != "normal") return false;
  const auto zoom = css_number(l.styles[Zoom]); if (!zoom || *zoom != 1) return false;
  return true;
}
bool inherited_geometry(FilterState& state, const Document& d, int node, const Rect& r, bool require_viewport, bool unfiltered_embedding=false) {
  if (require_viewport && (!d.viewport || !contains(*d.viewport,r))) return false;
  int depth = 0;
  for (int n=node; n >= 0 && static_cast<size_t>(n)<d.parents.size() && depth++<128; n=d.parents[n]) {
    if (!state.step()) return false;
    if (d.node_types[n]==9 || d.node_types[n]==11) continue;
    const int li=d.node_layout[n]; if (li < 0) continue;
    const auto& l=d.layouts[li];
    // Visibility is inherited but may explicitly become visible on descendants.
    // Other paint-affecting ancestor styles cannot be overridden that way.
    if (!l.styles_complete) return false;
    if(unfiltered_embedding&&l.styles[BackdropFilter]!="none")return false;
    for (int s : {Transform, Perspective, MaskImage, WebkitMaskImage}) if (s==Transform?!axis_transform(l.styles[s]):l.styles[s]!="none") {
      if(s==Transform&&state.human_diagnostics&&state.unsupported_transforms.size()<8&&l.styles[s].size()<256)state.unsupported_transforms.insert(l.styles[s]);
      state.omit(("unsupportedAncestor_"+kStyles[s]).c_str());return false;
    }
    // LayoutText copies the containing element's clip style. Its glyph box
    // is not a second clip-path reference box; the element below owns the clip.
    if(d.node_types[n]==1 && l.styles[ClipPath]!="none"){
      const auto clip=l.box&&unscaled_transform(l.styles[Transform])?inset_clip(l.styles[ClipPath],*l.box):std::nullopt;
      if(!clip||!contains(*clip,r)){state.omit("unprovenClipPath");return false;}
    }
    const auto brightness=brightness_gain(l.styles[Filter]);
    if(!brightness || (unfiltered_embedding && *brightness!=1))return false;
    const bool reflected=!unscaled_transform(l.styles[Transform]);
    if(unfiltered_embedding&&reflected)return false;
    const auto zoom=css_number(l.styles[Zoom]);
    if (!zoom || *zoom != 1 || l.styles[Clip] != "auto" || l.styles[MixBlendMode] != "normal") return false;
    const auto op=css_number(l.styles[Opacity]); if (!op || *op < 0 || *op > 1) return false;
    // Overflow clips descendants. It does not clip the element's own border
    // box, and LayoutText's copied style is not a new scrolling container.
    if(n==node)continue;
    for (int axis=0; axis<2; ++axis) {
      const auto& overflow=l.styles[axis ? OverflowY : OverflowX];
      if (overflow == "visible") continue;
      if(reflected)return false;
      if (overflow != "hidden" && overflow != "clip" && overflow != "scroll" && overflow != "auto") return false;
      if (!l.box) return false;
      const auto before=css_number(l.styles[axis ? BorderTop : BorderLeft],true), after=css_number(l.styles[axis ? BorderBottom : BorderRight],true);
      if (!before || !after || *before < 0 || *after < 0) return false;
      const double start=(axis ? l.box->y : l.box->x)+*before, end=(axis ? l.box->y+l.box->h : l.box->x+l.box->w)-*after;
      if ((axis ? r.y : r.x)<start || (axis ? r.y+r.h : r.x+r.w)>end) return false;
    }
  }
  return depth <= 128;
}
bool occluded(FilterState& state, const Document& d, const Layout& target, const Rect& r,Json* diagnostic=nullptr) {
  if (target.paint < 0) return true;
  for (const auto& other:d.layouts) {
    if (!state.step()) return true;
    if (!other.paint_box || !other.potentially_painted || other.paint <= target.paint || (!other.unbounded_paint && !overlaps(r,*other.paint_box))) continue;
    if (ancestor(d,other.node,target.node) || ancestor(d,target.node,other.node)) continue;
    if(diagnostic){(*diagnostic)["occluderUnbounded"]=other.unbounded_paint;(*diagnostic)["occluderTextShadow"]=other.styles[TextShadow]!="none";(*diagnostic)["occluderBoxShadow"]=other.styles[BoxShadow]!="none";(*diagnostic)["occluderFilter"]=other.styles[Filter]!="none";}
    state.omit(other.unbounded_paint?"unboundedPaintOcclusion":other.node<0?"fragmentedPaintOcclusion":"boundedPaintOcclusion");return true;
  }
  return false;
}
bool shown(FilterState& state, const Document& d, const Layout& l, const Rect& r) {
  const bool embedding=l.node>=0 && (d.tags[l.node]=="IFRAME" || d.tags[l.node]=="FRAME" || d.tags[l.node]=="OBJECT" || d.tags[l.node]=="EMBED");
  // Child-frame contrast is evaluated in its own snapshot. Do not accept an
  // embedding that changes its colors without propagating those paint layers.
  return l.basic && l.effective_opacity >= kMinOpacity && inherited_geometry(state,d,l.node,r,true,embedding) && !occluded(state,d,l,r);
}
bool center_geometry(const Document& d,const Layout& l) {
  if(l.node<0 || d.node_types[l.node]!=1)return false;
  const auto& tag=d.tags[l.node];return tag!="IFRAME" && tag!="FRAME" && tag!="OBJECT" && tag!="EMBED";
}
bool shown_geometry(FilterState& state,const Document& d,const Layout& l,const Rect& bounds) {
  if(!center_geometry(d,l))return shown(state,d,l,bounds);
  const double w=std::min(2.0,bounds.w),h=std::min(2.0,bounds.h);
  const Rect center{bounds.x+(bounds.w-w)/2,bounds.y+(bounds.h-h)/2,w,h};
  // Actionability proves the same center patch through ancestor clips and
  // occlusion. Text and embeddings still require their complete boxes.
  return l.basic && l.effective_opacity>=kMinOpacity && d.viewport && contains(*d.viewport,bounds) && inherited_geometry(state,d,l.node,center,true) && !occluded(state,d,l,center);
}
void clip_paint_bounds(const Document& d,Layout& paint) {
  if(!paint.paint_box || paint.unbounded_paint || paint.node<0)return;
  for(int n=d.parents[paint.node],depth=0;n>=0 && static_cast<size_t>(n)<d.parents.size() && depth++<128;n=d.parents[n]) {
    if(d.node_types[n]==9 || d.node_types[n]==11)continue;
    const int li=d.node_layout[n];if(li<0)continue;const auto& l=d.layouts[li];if(!l.box)continue;
    if(!unscaled_transform(l.styles[Transform]))continue;
    for(int axis=0;axis<2;++axis) {
      const auto& overflow=l.styles[axis?OverflowY:OverflowX];
      if(overflow!="hidden" && overflow!="clip" && overflow!="scroll" && overflow!="auto")continue;
      const auto before=css_number(l.styles[axis?BorderTop:BorderLeft],true),after=css_number(l.styles[axis?BorderBottom:BorderRight],true);
      if(!before || !after)continue;
      const double start=std::max(axis?paint.paint_box->y:paint.paint_box->x,(axis?l.box->y:l.box->x)+*before);
      const double end=std::min(axis?paint.paint_box->y+paint.paint_box->h:paint.paint_box->x+paint.paint_box->w,(axis?l.box->y+l.box->h:l.box->x+l.box->w)-*after);
      if(end<=start){paint.paint_box.reset();return;}
      if(axis){paint.paint_box->y=start;paint.paint_box->h=end-start;}else{paint.paint_box->x=start;paint.paint_box->w=end-start;}
    }
  }
}
std::optional<Rgba> text_background(const Document& d,const Layout& text,const Rect& bounds) {
  for(int n=text.node,depth=0;n>=0 && static_cast<size_t>(n)<d.parents.size() && depth++<128;n=d.parents[n]) {
    if(d.node_types[n]==9 || d.node_types[n]==11)continue;
    const int li=d.node_layout[n];if(li<0)continue;const auto& l=d.layouts[li];
    if(!l.box)continue;
    const auto background=color(l.styles[Background]);
    if(!contains(*l.box,bounds)) {
      if(d.node_types[n]==1 && overlaps(*l.box,bounds) && (!background || background->a>0 || l.styles[BackgroundImage]!="none"))return {};
      continue;
    }
    // Chromium supplies blendedBackgroundColors on the element, and -1 on
    // its LayoutText child. Use the containing element's pixel evidence.
    const auto blended=color(l.blended);if(blended && blended->a>=.999)return blended;
    // LayoutText's style is its parent's style; its background property does
    // not mean that the text rectangle itself paints that background.
    if(d.node_types[n]!=1)continue;
    if(!background)return {};
    if(background->a>=.999)return background;
    if(background->a>0)return {}; // do not guess through an unblended layer
  }
  return {};
}
std::optional<Rect> child_viewport(const Document& parent, int owner, const Json& child) {
  if (owner < 0 || static_cast<size_t>(owner)>=parent.node_layout.size()) return {};
  const int li=parent.node_layout[owner]; if (li<0) return {};
  const auto& l=parent.layouts[li]; if (!l.box) return {};
  std::array<double,8> edges{};
  for(size_t i=0;i<edges.size();++i) { const auto v=css_number(l.styles[BorderTop+i],true); if(!v || *v<0)return {}; edges[i]=*v; }
  const auto x=number(at(child,"scrollOffsetX")),y=number(at(child,"scrollOffsetY")); if(!x || !y)return {};
  const double w=l.box->w-edges[1]-edges[3]-edges[5]-edges[7],h=l.box->h-edges[0]-edges[2]-edges[4]-edges[6];
  if(w<=0 || h<=0)return {}; return Rect{*x,*y,w,h};
}
}

Json visible_snapshot_styles() { return kStyles; }

Json visible_snapshot(const Json& snapshot, const Json& viewports, bool human_diagnostics, bool include_uncertain) {
  FilterState state{at(snapshot,"strings")};
  state.human_diagnostics=human_diagnostics;
  const auto& raw_docs=at(snapshot,"documents");
  if(!raw_docs.is_array() || !state.strings.is_array()) { state.omit("malformedSnapshot"); return state.result; }
  if(raw_docs.size()>kMaxDocuments) { state.omit("documentLimit",raw_docs.size());return state.result; }
  std::vector<Document> docs; docs.reserve(raw_docs.size()); size_t total_nodes=0,total_layouts=0,total_boxes=0;
  for(const auto& raw:raw_docs) {
    Document d;d.source=&raw;d.frame=state.str(at(raw,"frameId"));d.viewport=rect(at(viewports,d.frame));
    const auto& nodes=at(raw,"nodes");const auto& types=at(nodes,"nodeType");const auto& layout=at(raw,"layout");
    const auto& indexes=at(layout,"nodeIndex");const auto& boxes=at(at(raw,"textBoxes"),"layoutIndex");
    if(!types.is_array() || !indexes.is_array()) {state.omit("malformedDocument");docs.push_back(std::move(d));continue;}
    total_nodes+=types.size();total_layouts+=indexes.size();total_boxes+=boxes.is_array()?boxes.size():0;
    if(total_nodes>kMaxNodes || total_layouts>kMaxLayouts || total_boxes>kMaxTextBoxes) {state.omit("snapshotLimit");return state.result;}
    const size_t count=types.size();d.parents.resize(count,-1);d.backend.resize(count,-1);d.node_layout.resize(count,-1);d.node_types.resize(count,-1);
    d.tags.resize(count);d.types.resize(count);d.ids.resize(count);d.label_for.resize(count);d.editable.resize(count);d.svg.resize(count);d.fragmented.resize(count);d.fragment_inconsistent.resize(count);d.fragments.resize(count);
    for(size_t n=0;n<count;++n) {
      int parent=integer(item(at(nodes,"parentIndex"),n));d.parents[n]=parent>=0 && static_cast<size_t>(parent)<n?parent:-1;
      d.node_types[n]=integer(types[n]);
      d.backend[n]=integer(item(at(nodes,"backendNodeId"),n));d.tags[n]=state.str(item(at(nodes,"nodeName"),n));
      d.editable[n]=d.tags[n]=="INPUT" || d.tags[n]=="TEXTAREA" || (d.parents[n]>=0 && d.editable[d.parents[n]]);
      d.svg[n]=d.tags[n]=="svg" || d.tags[n]=="SVG" || (d.parents[n]>=0 && d.svg[d.parents[n]]);
      const auto& attrs=item(at(nodes,"attributes"),n);
      if(attrs.is_array() && attrs.size()<=512) for(size_t a=0;a+1<attrs.size();a+=2) {
        const std::string key=state.str(attrs[a]);
        if(key=="id")d.ids[n]=state.str(attrs[a+1]);
        else if(key=="for" && d.tags[n]=="LABEL")d.label_for[n]=state.str(attrs[a+1]);
        else if(key=="type" && d.tags[n]=="INPUT")d.types[n]=state.str(attrs[a+1]);
        else if(key=="contenteditable" && state.str(attrs[a+1])!="false")d.editable[n]=true;
      }
    }
    for(size_t i=0;i<indexes.size();++i) {
      Layout l;l.node=integer(indexes[i]);l.paint=integer(item(at(layout,"paintOrders"),i));l.box=rect(item(at(layout,"bounds"),i));
      const auto& styles=item(at(layout,"styles"),i);l.styles_complete=styles.is_array() && styles.size()==kStyles.size();
      if(l.styles_complete)for(size_t s=0;s<kStyles.size();++s)l.styles[s]=state.str(styles[s]);
      l.text=state.str(item(at(layout,"text"),i));l.blended=state.str(item(at(layout,"blendedBackgroundColors"),i));l.text_opacity=number(item(at(layout,"textColorOpacities"),i));
      if(l.node<0 || static_cast<size_t>(l.node)>=count) {l.node=-1;state.omit("malformedLayout");}
      else if(d.node_layout[l.node]>=0) {
        // Chromium can report several inline fragments for the same DOM node.
        // Preserve their proven ancestry instead of turning ordinary fragments
        // into anonymous, globally uncertain shadow paint. Inconsistent styles
        // or fragment clipping still fail closed. A fragment is not proof of
        // the complete element's action geometry.
        const auto& first=d.layouts[d.node_layout[l.node]];d.fragmented[l.node]=true;
        if(!l.styles_complete || !first.styles_complete || l.styles!=first.styles ||
           l.styles[OverflowX]!="visible" || l.styles[OverflowY]!="visible") {
          d.fragment_inconsistent[l.node]=true;l.node=-1;state.omit("unsupportedFragmentedLayout");
        }
      }
      else d.node_layout[l.node]=static_cast<int>(d.layouts.size());
      if(l.node>=0)d.fragments[l.node].push_back(static_cast<int>(d.layouts.size()));
      l.basic=l.node>=0 && basic_style(l);d.layouts.push_back(std::move(l));
    }
    for(auto& l:d.layouts) {
      double opacity=1;bool shadow_geometry=l.node>=0 && l.styles_complete;int n=l.node,depth=0;
      for(;n>=0 && static_cast<size_t>(n)<count && depth++<128;n=d.parents[n]) {
        if(d.node_types[n]==9 || d.node_types[n]==11)continue;
        const int li=d.node_layout[n];if(li<0)continue;const auto& a=d.layouts[li];const auto value=css_number(a.styles[Opacity]);
        const auto zoom=css_number(a.styles[Zoom]);
        if(!a.styles_complete || !unscaled_transform(a.styles[Transform]) || a.styles[Perspective]!="none" || !zoom || *zoom!=1)shadow_geometry=false;
        if(!value || *value<0 || *value>1){opacity=0;break;}opacity*=*value;
      }
      l.effective_opacity=depth>128?0:opacity;
      const auto bg=color(l.styles[Background]);
      bool border_paint=false;
      for(size_t edge=0;edge<4;++edge) {
        const auto width=css_number(l.styles[BorderTop+edge],true);
        const auto rgba=color(l.styles[BorderTopColor+edge]);
        if(!width || (*width>0 && (!rgba || rgba->a>0)))border_paint=true;
      }
      // CSS shadow offsets/radii are usable only in an unscaled frame space.
      const auto shadow_bounds=l.box && (l.styles[BoxShadow]=="none" || (shadow_geometry && depth<=128))?box_shadow_bounds(l.styles[BoxShadow],*l.box):std::nullopt;
      l.paint_box=shadow_bounds?shadow_bounds:l.box;
      const auto outline_width=css_number(l.styles[OutlineWidth],true),outline_offset=css_number(l.styles[OutlineOffset],true);
      const auto outline_color=color(l.styles[OutlineColor]);
      const bool outline_paint=l.styles[OutlineStyle]!="none" && (!outline_width || *outline_width>0) && (!outline_color || outline_color->a>0);
      if(outline_paint && l.paint_box && outline_width && outline_offset) {
        const double expand=std::max(0.0,*outline_width+*outline_offset);
        // Include the outline without losing any separately offset shadow.
        const double right=std::max(l.paint_box->x+l.paint_box->w,l.box->x+l.box->w+expand),bottom=std::max(l.paint_box->y+l.paint_box->h,l.box->y+l.box->h+expand);
        l.paint_box->x=std::min(l.paint_box->x,l.box->x-expand);l.paint_box->y=std::min(l.paint_box->y,l.box->y-expand);
        l.paint_box->w=right-l.paint_box->x;l.paint_box->h=bottom-l.paint_box->y;
      }
      // Only supported box shadows have bounded ink. Unknown shadow/filter
      // paint still blocks evidence globally rather than guessing its reach.
      // Backdrop output is clipped to the element's transformed border box,
      // including rounded corners. It cannot obscure unrelated distant pixels.
      // Foreground filters and shadows can extend beyond that box and still
      // require independent bounds. Content inside a backdrop remains withheld.
      // Text shadows paint glyphs, not an empty container's entire subtree.
      // Snapshot text layouts carry the inherited shadow and their own bounds.
      // Native form controls and SVG can paint glyphs absent from layout.text.
      // Their shadow source cannot be inferred from an empty snapshot string.
      const bool unknown_glyphs=l.text.empty() && l.node>=0 &&
        (d.editable[l.node] || d.svg[l.node] || d.tags[l.node]=="SELECT");
      const auto text_bounds=l.styles[TextShadow]=="none"?l.box:unknown_glyphs?std::nullopt:l.text.empty()?l.box:l.box && shadow_geometry && depth<=128?
        box_shadow_bounds(l.styles[TextShadow],*l.box,true):std::nullopt;
      if(text_bounds && l.paint_box){
        const double right=std::max(l.paint_box->x+l.paint_box->w,text_bounds->x+text_bounds->w),bottom=std::max(l.paint_box->y+l.paint_box->h,text_bounds->y+text_bounds->h);
        l.paint_box->x=std::min(l.paint_box->x,text_bounds->x);l.paint_box->y=std::min(l.paint_box->y,text_bounds->y);
        l.paint_box->w=right-l.paint_box->x;l.paint_box->h=bottom-l.paint_box->y;
      }
      l.unbounded_paint=!shadow_bounds || !text_bounds || !bounded_color_filter(l.styles[Filter]) ||
        (outline_paint && (!outline_width || !outline_offset));
      const std::string tag=l.node>=0?d.tags[l.node]:"";
      const bool media=tag=="IMG" || tag=="SVG" || tag=="CANVAS" || tag=="VIDEO" || tag=="IFRAME" || tag=="OBJECT" || tag=="EMBED";
      // Unknown paint is a blocker too. pointer-events does not affect pixels.
      l.potentially_painted=l.effective_opacity>0 && l.styles[Visibility]=="visible" && l.styles[Display]!="none" &&
        (media || border_paint || outline_paint || l.styles[BoxShadow]!="none" || l.styles[BackdropFilter]!="none" || l.unbounded_paint || !l.text.empty() || !bg || bg->a>0 || l.styles[BackgroundImage]!="none" || !l.basic);
    }
    // A pure Gaussian blur has finite raster ink. Include every descendant's
    // proven paint bounds before expanding; overflow children must not be lost.
    // Unsupported source paint or transformed ancestry stays unbounded.
    for(auto it=d.layouts.rbegin();it!=d.layouts.rend();++it){auto& l=*it;if(l.node<0||!l.unbounded_paint)continue;
      const auto extent=blur_extent(l.styles[Filter]);if(!extent||!l.paint_box)continue;
      const auto stroke=css_number(l.styles[TextStrokeWidth],true);
      bool safe=l.styles[TextShadow]=="none"&&l.styles[BoxShadow]=="none"&&stroke&&*stroke==0;Rect source=*l.paint_box;double expansion=1;int depth=0;
      for(int n=l.node;n>=0&&static_cast<size_t>(n)<count&&depth++<128;n=d.parents[n]){
        if(d.node_types[n]==9||d.node_types[n]==11)continue;
        const int li=d.node_layout[n];if(li<0)continue;const auto zoom=css_number(d.layouts[li].styles[Zoom]);
        const auto transform=paint_transform_expansion(d.layouts[li].styles[Transform]);
        if(!transform||d.layouts[li].styles[Perspective]!="none"||!zoom||*zoom!=1)safe=false;
        else{expansion*=*transform;if(!std::isfinite(expansion)||expansion>1e6)safe=false;}
      }
      if(depth>128)safe=false;
      for(const auto& child:d.layouts){
        if(!state.step()){safe=false;break;}
        if(child.node==l.node||!ancestor(d,l.node,child.node)||!child.potentially_painted)continue;
        if(child.unbounded_paint||!child.paint_box){
          if(human_diagnostics&&state.unsupported_paint.size()<8){
            const auto example="blur source descendant: "+(child.node>=0?d.tags[child.node]:std::string("fragment"))+"; filter: "+child.styles[Filter]+"; shadow: "+child.styles[BoxShadow]+"; transform: "+child.styles[Transform]+(child.paint_box?"":"; missing bounds");
            if(example.size()<512)state.unsupported_paint.insert(example);
          }
          safe=false;break;
        }
        const double right=std::max(source.x+source.w,child.paint_box->x+child.paint_box->w),bottom=std::max(source.y+source.h,child.paint_box->y+child.paint_box->h);
        source.x=std::min(source.x,child.paint_box->x);source.y=std::min(source.y,child.paint_box->y);source.w=right-source.x;source.h=bottom-source.y;
      }
      if(safe){const double padding=*extent*expansion;l.paint_box=Rect{source.x-padding,source.y-padding,source.w+2*padding,source.h+2*padding};l.unbounded_paint=false;}
    }
    for(const auto& l:d.layouts)if(human_diagnostics&&l.unbounded_paint&&l.potentially_painted&&state.unsupported_paint.size()<8){
      const auto example=std::string(l.box?"":"missing bounds; ")+"box-shadow: "+l.styles[BoxShadow]+"; text-shadow: "+l.styles[TextShadow]+"; filter: "+l.styles[Filter]+"; transform: "+l.styles[Transform];
      if(example.size()<512)state.unsupported_paint.insert(example);
    }
    for(auto& l:d.layouts)clip_paint_bounds(d,l);
    docs.push_back(std::move(d));
  }
  for(size_t i=0;i<docs.size();++i) {
    const auto& content=at(at(*docs[i].source,"nodes"),"contentDocumentIndex");const auto& indexes=at(content,"index");
    if(!indexes.is_array())continue;
    for(size_t j=0;j<indexes.size();++j) {
      const int child=integer(item(at(content,"value"),j)),owner=integer(indexes[j]);
      if(child>=0 && static_cast<size_t>(child)<docs.size() && child!=static_cast<int>(i) && docs[child].parent_doc<0) {docs[child].parent_doc=static_cast<int>(i);docs[child].owner_node=owner;}
    }
  }
  for(size_t pass=0;pass<docs.size();++pass) for(auto& d:docs) {
    if(d.viewport || d.parent_doc<0)continue;const auto& parent=docs[d.parent_doc];const int owner=d.owner_node;
    if(!parent.viewport || owner<0 || static_cast<size_t>(owner)>=parent.node_layout.size())continue;
    const int li=parent.node_layout[owner];if(li<0)continue;const auto& l=parent.layouts[li];
    if(l.box && shown(state,parent,l,*l.box))d.viewport=child_viewport(parent,owner,*d.source);
  }
  std::set<std::string> seen_frames;
  for(auto& d:docs) {
    if(d.frame.empty() || !seen_frames.insert(d.frame).second || !d.viewport) {state.omit("unprovenViewport");continue;}
    Json frame{{"nodes",Json::object()},{"viewport",encode_rect(*d.viewport)}};
    if(d.parent_doc>=0 && d.owner_node>=0 && static_cast<size_t>(d.owner_node)<docs[d.parent_doc].backend.size()) {
      frame["parentFrameId"]=docs[d.parent_doc].frame;frame["ownerBackendNodeId"]=docs[d.parent_doc].backend[d.owner_node];
    }
    std::vector<std::vector<std::pair<int,std::string>>> runs(d.layouts.size());
    const auto& boxes=at(*d.source,"textBoxes");const auto& indexes=at(boxes,"layoutIndex");
    std::vector<std::optional<TextUnits>> units(d.layouts.size());
    if(indexes.is_array())for(size_t b=0;b<indexes.size();++b) {
      const int li=integer(indexes[b]);const auto bounds=rect(item(at(boxes,"bounds"),b));
      const int start=integer(item(at(boxes,"start"),b)),length=integer(item(at(boxes,"length"),b));
      if(li<0 || static_cast<size_t>(li)>=d.layouts.size() || !bounds || start<0 || length<=0) {state.omit("malformedTextBox");continue;}
      auto& l=d.layouts[li];
      if(l.node>=0 && d.editable[l.node]) {state.omit("editableValue");continue;}
      if(l.node>=0 && d.svg[l.node]) {state.omit("unsupportedTextPaint");continue;}
      if(!shown(state,d,l,*bounds)) {state.omit("unprovenTextGeometry");continue;}
      const auto font=css_number(l.styles[FontSize],true);
      const auto fg=color(l.styles[TextFill]),bg=text_background(d,l,*bounds);
      const auto stroke=css_number(l.styles[TextStrokeWidth],true);
      bool unsupported=!font || *font<kMinFontSize || !fg || !bg || bg->a<.999 || !l.text_opacity || *l.text_opacity<kMinOpacity ||
        !stroke || *stroke!=0 || l.styles[TextShadow]!="none" || l.styles[TextSecurity]!="none";
      for(int s:{LetterSpacing,WordSpacing}) if(l.styles[s]!="normal") {const auto v=css_number(l.styles[s],true);if(!v || *v<0)unsupported=true;}
      double gain=1,orientation_x=1,orientation_y=1;
      for(int n=l.node,depth=0;n>=0 && static_cast<size_t>(n)<d.parents.size() && depth++<128;n=d.parents[n]) {
        if(d.node_types[n]==9 || d.node_types[n]==11)continue;
        const int ancestor_li=d.node_layout[n];if(ancestor_li<0)continue;const auto& a=d.layouts[ancestor_li];
        if(a.styles[TextOverflow]!="clip" || (a.styles[LineClamp]!="none" && a.styles[LineClamp]!="0"))unsupported=true;
        if(a.styles[BackgroundImage]!="none")unsupported=true;
        // Backdrop effects change contrast behind content, not its geometry.
        if(a.styles[BackdropFilter]!="none")unsupported=true;
        // LayoutText copies its element's computed style, but does not create
        // another CSS filter layer. Apply only actual element filters once.
        if(d.node_types[n]==1) {
          const auto brightness=brightness_gain(a.styles[Filter]);if(!brightness)unsupported=true;else gain*=*brightness;
          const auto axes=axis_transform(a.styles[Transform]);if(!axes)unsupported=true;else {orientation_x*=axes->first;orientation_y*=axes->second;}
        }
      }
      if(orientation_x!=1 || orientation_y!=1)unsupported=true;
      if(unsupported) {state.omit("unsupportedTextStyle");continue;}
      if(darkened_contrast(*fg,*bg,std::min(l.effective_opacity,*l.text_opacity),gain)<kMinContrast) {state.omit("lowContrastText");continue;}
      if(!units[li])units[li]=utf16_offsets(l.text);const auto& u=*units[li];
      const size_t begin=static_cast<size_t>(start),end=begin+static_cast<size_t>(length);
      if(!u.safe) {state.omit("unsafeUnicode");continue;}
      if(end>=u.offsets.size() || u.offsets[begin]==std::numeric_limits<size_t>::max() || u.offsets[end]==std::numeric_limits<size_t>::max()) {state.omit("malformedTextRange");continue;}
      const auto text=normalized_space(l.text.substr(u.offsets[begin],u.offsets[end]-u.offsets[begin]));
      if(!text.empty())runs[li].push_back({start,text});
    }
    std::vector<std::string> names(d.parents.size());std::vector<bool> name_limit(d.parents.size());
    for(size_t li=0;li<runs.size();++li) {
      auto& parts=runs[li];std::sort(parts.begin(),parts.end(),[](const auto& a,const auto& b){return a.first<b.first;});
      std::string text;int previous=-1;for(const auto& [start,part]:parts) {if(start==previous)continue;previous=start;if(!text.empty())text+=' ';text+=part;}
      if(text.empty())continue;
      for(int n=d.layouts[li].node,depth=0;n>=0 && static_cast<size_t>(n)<d.parents.size() && depth++<128;n=d.parents[n]) {
        if(!state.step())break;if(name_limit[n])continue;
        if(names[n].size()+text.size()+1>kMaxNameBytes) {names[n].clear();name_limit[n]=true;state.omit("nameLimit");continue;}
        if(!names[n].empty())names[n]+=' ';names[n]+=text;
      }
    }
    std::map<std::string,std::string> labels;
    for(size_t n=0;n<d.tags.size();++n)if(d.tags[n]=="LABEL" && !d.label_for[n].empty() && !names[n].empty()) {
      auto& label=labels[d.label_for[n]];if(label.size()+names[n].size()+1<=kMaxNameBytes) {if(!label.empty())label+=' ';label+=names[n];}
    }
    for(size_t n=0;n<d.tags.size();++n) {
      // LayoutView/document-fragment records are structural ancestry, not
      // visible elements. Their intentionally empty styles are not a gap.
      if(d.node_types[n]==9 || d.node_types[n]==11)continue;
      const int li=d.node_layout[n];if(li<0 || d.backend[n]<0)continue;
      Layout combined;const Layout* geometry=&d.layouts[li];
      if(d.fragmented[n]) {
        // A split inline link may have several boxes. Admit its union only when
        // the action center lies inside a verified fragment, not the gap between
        // lines. All fragment records must retain consistent styles/identity.
        bool complete=!d.fragment_inconsistent[n]&&center_geometry(d,*geometry);Rect bounds{};bool first=true;
        for(const auto index:d.fragments[n]) {
          const auto& fragment=d.layouts[index];if(!fragment.basic || !fragment.box){complete=false;break;}
          if(first){bounds=*fragment.box;first=false;}else {
            const double right=std::max(bounds.x+bounds.w,fragment.box->x+fragment.box->w),bottom=std::max(bounds.y+bounds.h,fragment.box->y+fragment.box->h);
            bounds.x=std::min(bounds.x,fragment.box->x);bounds.y=std::min(bounds.y,fragment.box->y);bounds.w=right-bounds.x;bounds.h=bottom-bounds.y;
          }
        }
        if(bounds.w>1e8 || bounds.h>1e8)complete=false;
        const Rect center{bounds.x+(bounds.w-std::min(2.0,bounds.w))/2,bounds.y+(bounds.h-std::min(2.0,bounds.h))/2,std::min(2.0,bounds.w),std::min(2.0,bounds.h)};
        const Layout* center_fragment=nullptr;
        if(complete)for(const auto index:d.fragments[n])if(contains(*d.layouts[index].box,center)){center_fragment=&d.layouts[index];break;}
        if(!center_fragment){state.omit("unprovenFragmentedGeometry");continue;}
        combined=*center_fragment;combined.box=bounds;geometry=&combined;
      }
      const auto& l=*geometry;
      if(!l.box || !shown_geometry(state,d,l,*l.box)) {state.omit("unprovenGeometry");continue;}
      std::string name=names[n];
      if(d.tags[n]=="INPUT" || d.tags[n]=="TEXTAREA" || d.tags[n]=="SELECT") {
        name.clear();if(!d.ids[n].empty() && labels.contains(d.ids[n]))name=labels[d.ids[n]];
        if(name.empty())for(int p=d.parents[n],depth=0;p>=0 && static_cast<size_t>(p)<d.parents.size() && depth++<128;p=d.parents[p])if(d.tags[p]=="LABEL"){name=names[p];break;}
      }
      static const std::set<std::string> safe_tags{"#text","#document","HTML","BODY","A","ABBR","ADDRESS","ARTICLE","ASIDE","AUDIO","B","BDI","BDO","BLOCKQUOTE","BR","BUTTON","CANVAS","CAPTION","CITE","CODE","COL","COLGROUP","DATA","DATALIST","DD","DEL","DETAILS","DFN","DIALOG","DIV","DL","DT","EM","EMBED","FIELDSET","FIGCAPTION","FIGURE","FOOTER","FORM","H1","H2","H3","H4","H5","H6","HEADER","HGROUP","HR","I","IFRAME","IMG","INPUT","INS","KBD","LABEL","LEGEND","LI","MAIN","MAP","MARK","MENU","METER","NAV","OBJECT","OL","OPTGROUP","OPTION","OUTPUT","P","PICTURE","PRE","PROGRESS","Q","RP","RT","RUBY","S","SAMP","SECTION","SELECT","SMALL","SOURCE","SPAN","STRONG","SUB","SUMMARY","SUP","TABLE","TBODY","TD","TEMPLATE","TEXTAREA","TFOOT","TH","THEAD","TIME","TR","TRACK","U","UL","VAR","VIDEO","WBR","SVG","svg","path","text","g","rect","circle","line","polygon","polyline","ellipse","tspan","foreignObject"};
      Json value{{"bounds",encode_rect(*l.box)},{"tag",safe_tags.contains(d.tags[n])?d.tags[n]:"ELEMENT"},{"name",name},{"textVisible",!name.empty()},
        {"geometryEvidence",center_geometry(d,l)?"unoccluded_center_patch":"full_box"}};
      if(d.tags[n]=="INPUT") {
        // Attribute values are not free-form output channels.
        static const std::set<std::string> allowed{"button","checkbox","color","date","datetime-local","email","file","hidden","image","month","number","password","radio","range","reset","search","submit","tel","text","time","url","week"};
        value["inputType"]=allowed.contains(d.types[n])?d.types[n]:"text";
      }
      frame["nodes"][std::to_string(d.backend[n])]=std::move(value);
    }
    if(include_uncertain&&!state.exhausted){
      Json candidates=Json::object();
      for(size_t n=0;n<d.tags.size();++n){
        if(!state.step())break;
        if(d.node_types[n]!=1||d.backend[n]<0||frame["nodes"].contains(std::to_string(d.backend[n])))continue;
        const int li=d.node_layout[n];if(li<0)continue;const auto& l=d.layouts[li];
        if(!l.box||!l.styles_complete||!overlaps(*l.box,*d.viewport)||l.effective_opacity<=0||l.styles[Visibility]!="visible"||l.styles[Display]=="none"||l.styles[ContentVisibility]=="hidden")continue;
        if(d.types[n]=="password"||d.types[n]=="hidden")continue;
        bool possible=true;std::string reason="clipping_or_occlusion_unproven";int depth=0;
        bool unsupported_transform=false,unsupported_paint=false,clip_present=false,nonrectangular_clip=false;
        for(int p=static_cast<int>(n);p>=0&&static_cast<size_t>(p)<d.parents.size()&&depth++<128;p=d.parents[p]){
          if(!state.step()){possible=false;break;}
          if(d.node_types[p]==9||d.node_types[p]==11)continue;
          const int ancestor_li=d.node_layout[p];if(ancestor_li<0)continue;const auto& a=d.layouts[ancestor_li];
          if(!a.styles_complete||a.styles[Display]=="none"||a.styles[ContentVisibility]=="hidden"){possible=false;break;}
          if(!axis_transform(a.styles[Transform])){reason="transform_visibility_unproven";unsupported_transform=true;}
          else if(!brightness_gain(a.styles[Filter])||a.styles[MaskImage]!="none"||a.styles[WebkitMaskImage]!="none"||a.styles[Perspective]!="none"){reason="paint_effect_visibility_unproven";unsupported_paint=true;}
          // Do not enumerate controls proven outside a rectangular ancestor clip.
          bool empty_clip=false;const auto clip=a.box?inset_clip(a.styles[ClipPath],*a.box,&empty_clip,false):std::nullopt;
          if(a.styles[ClipPath]!="none"){clip_present=true;if(!clip&&!empty_clip)nonrectangular_clip=true;}
          if(empty_clip){possible=false;break;}
          if(clip&&unscaled_transform(a.styles[Transform])&&!overlaps(*clip,*l.box)){possible=false;break;}
        }
        if(!possible||depth>128)continue;
        if(candidates.size()>=1000){state.omit("uncertainCandidateLimit");break;}
        // A separately verified text run may survive even when the control's
        // complete geometry does not. Never substitute AX names or DOM values.
        const auto name=d.tags[n]=="INPUT"||d.tags[n]=="TEXTAREA"||d.tags[n]=="SELECT"?std::string{}:names[n];
        candidates[std::to_string(d.backend[n])]={{"bounds",encode_rect(*l.box)},{"name",name},{"visibilityStatus","uncertain"},{"visibilityReason",reason},{"screenshotRequired",true}};
        // Internal numeric/boolean diagnostics only; never copy raw CSS or DOM strings.
        candidates[std::to_string(d.backend[n])]["filterDiagnostics"]={{"effectiveOpacity",l.effective_opacity},{"ancestorDepth",depth},{"unsupportedTransform",unsupported_transform},{"unsupportedPaint",unsupported_paint},{"clipPresent",clip_present},{"nonrectangularClip",nonrectangular_clip}};
        auto& diagnostic=candidates[std::to_string(d.backend[n])]["filterDiagnostics"];
        diagnostic["basicStyleProven"]=l.basic;
        diagnostic["boundsInsideViewport"]=contains(*d.viewport,*l.box);
        diagnostic["ancestorGeometryProven"]=inherited_geometry(state,d,l.node,*l.box,true);
        const double cw=std::min(2.0,l.box->w),ch=std::min(2.0,l.box->h);
        const Rect center{l.box->x+(l.box->w-cw)/2,l.box->y+(l.box->h-ch)/2,cw,ch};
        diagnostic["centerOcclusionProvenClear"]=!occluded(state,d,l,center,&diagnostic);
        if(std::set<std::string>{"INPUT","SELECT","TEXTAREA","BUTTON","A"}.contains(d.tags[n]))candidates[std::to_string(d.backend[n])]["tag"]=d.tags[n];
      }
      frame["uncertainNodes"]=std::move(candidates);
    }
    state.result["frames"][d.frame]=std::move(frame);
  }
  state.result["policy"]={{"minimumOpacity",kMinOpacity},{"minimumTextContrast",kMinContrast},{"minimumFontSizeCssPx",kMinFontSize},{"nameSource","rendered-text-boxes"}};
  if(human_diagnostics)state.result["policy"]["untrustedUnsupportedTransforms"]=state.unsupported_transforms;
  if(human_diagnostics)state.result["policy"]["untrustedUnsupportedPaint"]=state.unsupported_paint;
  return state.result;
}
}
