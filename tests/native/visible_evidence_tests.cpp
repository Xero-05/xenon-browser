#include "xenon/visible_evidence.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using xenon::Json;
namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
struct Fixture {
  Json snapshot{{"strings",Json::array()},{"documents",Json::array()}};
  Json viewports{{"main",{{"x",0},{"y",0},{"width",800},{"height",600}}}};
  std::map<std::string,int> strings;
  std::vector<std::string> styles=xenon::visible_snapshot_styles().get<std::vector<std::string>>();
  int string(const std::string& text) {
    auto it=strings.find(text);if(it!=strings.end())return it->second;
    int index=static_cast<int>(snapshot["strings"].size());snapshot["strings"].push_back(text);strings.emplace(text,index);return index;
  }
  std::string default_style(const std::string& key) {
    if(key=="display")return "block";if(key=="visibility")return "visible";
    if(key=="opacity" || key=="zoom")return "1";
    if(key=="color" || key=="-webkit-text-fill-color" || key.ends_with("-color") && key!="background-color")return "rgb(0, 0, 0)";
    if(key=="background-color")return "rgba(0, 0, 0, 0)";
    if(key=="font-size")return "16px";
    if(key=="overflow-x" || key=="overflow-y")return "visible";
    if(key=="clip")return "auto";
    if(key=="mix-blend-mode" || key=="letter-spacing" || key=="word-spacing")return "normal";
    if(key=="text-overflow")return "clip";
    if(key=="content-visibility")return "visible";
    if(key.starts_with("border-") || key.starts_with("padding-") || key=="-webkit-text-stroke-width" || key=="outline-width" || key=="outline-offset")return "0px";
    return "none";
  }
  int document(const std::string& frame) {
    Json doc{{"frameId",string(frame)},{"scrollOffsetX",0},{"scrollOffsetY",0},
      {"nodes",{{"parentIndex",Json::array()},{"nodeType",Json::array()},{"nodeName",Json::array()},
                {"nodeValue",Json::array()},{"backendNodeId",Json::array()},{"attributes",Json::array()}}},
      {"layout",{{"nodeIndex",Json::array()},{"styles",Json::array()},{"bounds",Json::array()},
                 {"text",Json::array()},{"paintOrders",Json::array()},{"blendedBackgroundColors",Json::array()},
                 {"textColorOpacities",Json::array()}}},
      {"textBoxes",{{"layoutIndex",Json::array()},{"bounds",Json::array()},{"start",Json::array()},{"length",Json::array()}}}};
    int index=static_cast<int>(snapshot["documents"].size());snapshot["documents"].push_back(std::move(doc));return index;
  }
  int node(int doc,int parent,const std::string& tag,Json attrs=Json::object()) {
    auto& n=snapshot["documents"][doc]["nodes"];int index=static_cast<int>(n["nodeType"].size());
    n["parentIndex"].push_back(parent);n["nodeType"].push_back(tag=="#text"?3:tag=="#document"?9:1);
    n["nodeName"].push_back(string(tag));n["nodeValue"].push_back(string(""));n["backendNodeId"].push_back((doc+1)*1000+index);
    Json attr=Json::array();for(const auto& [key,value]:attrs.items()) {attr.push_back(string(key));attr.push_back(string(value.get<std::string>()));}
    n["attributes"].push_back(std::move(attr));return index;
  }
  int layout(int doc,int node,Json box,int paint=1,const std::string& text="") {
    auto& l=snapshot["documents"][doc]["layout"];int index=static_cast<int>(l["nodeIndex"].size());
    l["nodeIndex"].push_back(node);l["bounds"].push_back(box);l["paintOrders"].push_back(paint);l["text"].push_back(string(text));
    Json values=Json::array();for(const auto& key:styles)values.push_back(string(default_style(key)));l["styles"].push_back(std::move(values));
    l["blendedBackgroundColors"].push_back(string("rgb(255, 255, 255)"));l["textColorOpacities"].push_back(1);return index;
  }
  void style(int doc,int layout,const std::string& key,const std::string& value) {
    auto it=std::find(styles.begin(),styles.end(),key);require(it!=styles.end(),"Test requested unknown style");
    snapshot["documents"][doc]["layout"]["styles"][layout][it-styles.begin()]=string(value);
  }
  void box(int doc,int layout,Json bounds,int start,int length) {
    auto& b=snapshot["documents"][doc]["textBoxes"];b["layoutIndex"].push_back(layout);b["bounds"].push_back(bounds);b["start"].push_back(start);b["length"].push_back(length);
  }
  std::pair<int,int> button(int doc,int parent,const std::string& text="Visible button",int x=20,int y=20) {
    int n=node(doc,parent,"BUTTON",{{"aria-label","HIDDEN_ARIA_SENTINEL"},{"title","HIDDEN_TITLE_SENTINEL"}});
    layout(doc,n,{x,y,180,36},2);
    int t=node(doc,n,"#text");int l=layout(doc,t,{x+8,y+8,150,20},2,text);box(doc,l,{x+8,y+8,150,20},0,static_cast<int>(text.size()));return {n,l};
  }
  Fixture() {document("main");int root=node(0,-1,"#document");int html=node(0,root,"HTML");layout(0,html,{0,0,800,600},0);}
  Json run() {return xenon::visible_snapshot(snapshot,viewports);}
  static const Json& output(const Json& result,int node,int doc=0,const std::string& frame="main") {
    static const Json empty;const auto key=std::to_string((doc+1)*1000+node);
    if(!result["frames"].contains(frame) || !result["frames"][frame]["nodes"].contains(key))return empty;
    return result["frames"][frame]["nodes"][key];
  }
};
void visible_text_and_hidden_metadata() {
  Fixture f;auto [button,text]=f.button(0,1);f.snapshot["documents"][0]["title"]=f.string("HIDDEN_DOCUMENT_TITLE");
  auto result=f.run();require(Fixture::output(result,button)["name"]=="Visible button","Rendered button label missing");
  require(result.dump().find("HIDDEN_")==std::string::npos,"Invisible source metadata leaked");
  int icon=f.node(0,1,"BUTTON",{{"aria-label","HIDDEN_ICON_LABEL"}});f.layout(0,icon,{250,20,32,32},3);
  result=f.run();require(Fixture::output(result,icon)["name"]=="","Icon-only control invented a readable label");
}
void chromium_layout_view_and_parent_blending() {
  Fixture f;int view=f.layout(0,0,{0,0,800,600},0);f.snapshot["documents"][0]["layout"]["styles"][view]=Json::array();
  auto [button,text]=f.button(0,1);f.snapshot["documents"][0]["layout"]["blendedBackgroundColors"][text]=-1;
  auto result=f.run();require(Fixture::output(result,button)["name"]=="Visible button","Chromium LayoutView or parent-only blended background suppressed ordinary text");
  require(result["partial"]==false && result["omitted"].empty(),"Valid unstyled LayoutView was reported as incomplete evidence");
  f.snapshot["documents"][0]["layout"]["blendedBackgroundColors"]=Json::array();
  result=f.run();require(Fixture::output(result,button)["name"]=="","Missing all background evidence was guessed as white");
  f.style(0,0,"background-color","rgb(255, 255, 255)");
  result=f.run();require(Fixture::output(result,button)["name"]=="Visible button","Known containing opaque background was not used");
  f.layout(0,button,{30,25,40,20},2,"Fragment");
  result=f.run();require(result["partial"]==true && result["omitted"].contains("unsupportedFragmentedLayout"),"Unsupported repeated layout was not reported");
  require(!result["omitted"].contains("malformedLayout"),"Valid repeated layout was incorrectly called malformed");
}
void inherited_style_and_clipping() {
  for(const auto& [key,value]:std::vector<std::pair<std::string,std::string>>{{"opacity","0.01"},{"transform","matrix(1, 0, 0, 1, 0, 0)"},{"filter","blur(2px)"},{"clip-path","inset(99%)"}}) {
    Fixture f;int parent=f.node(0,1,"DIV");int layout=f.layout(0,parent,{10,10,220,100});f.style(0,layout,key,value);f.button(0,parent,"HIDDEN_STYLE_TEXT");
    auto result=f.run();require(result.dump().find("HIDDEN_STYLE_TEXT")==std::string::npos,"Ancestor paint style leaked invisible text");require(result["partial"]==true,"Omission not reported");
  }
  Fixture clipped;int parent=clipped.node(0,1,"DIV");int layout=clipped.layout(0,parent,{0,0,1,1});clipped.style(0,layout,"overflow-x","hidden");clipped.style(0,layout,"overflow-y","hidden");clipped.button(0,parent,"HIDDEN_CLIPPED_TEXT");
  require(clipped.run().dump().find("HIDDEN_CLIPPED_TEXT")==std::string::npos,"One-pixel clip leaked text");
  Fixture offscreen;offscreen.button(0,1,"HIDDEN_OFFSCREEN_TEXT",900,20);require(offscreen.run().dump().find("HIDDEN_OFFSCREEN_TEXT")==std::string::npos,"Off-viewport text leaked");
  Fixture overridden;int p=overridden.node(0,1,"DIV");int l=overridden.layout(0,p,{0,0,400,200});overridden.style(0,l,"visibility","hidden");auto [visible,t]=overridden.button(0,p);
  require(Fixture::output(overridden.run(),visible)["name"]=="Visible button","Explicit visible descendant was wrongly hidden");
}
void pixels_and_paint_order() {
  Fixture f;f.button(0,1,"HIDDEN_COVERED_TEXT");int overlay=f.node(0,1,"DIV",{{"style","pointer-events:none"}});int layout=f.layout(0,overlay,{0,0,250,100},100);f.style(0,layout,"background-color","rgb(255, 255, 255)");
  require(f.run().dump().find("HIDDEN_COVERED_TEXT")==std::string::npos,"Pointer-events:none cover leaked underlying text");
  for(const auto& [key,value]:std::vector<std::pair<std::string,std::string>>{{"-webkit-text-fill-color","rgb(255, 255, 255)"},{"font-size","1px"},{"letter-spacing","-8px"},{"text-shadow","1px 1px rgb(0, 0, 0)"},{"text-overflow","ellipsis"}}) {
    Fixture g;auto [button,text]=g.button(0,1,"HIDDEN_UNREADABLE_TEXT");g.style(0,text,key,value);
    require(g.run().dump().find("HIDDEN_UNREADABLE_TEXT")==std::string::npos,"Unsupported unreadable text leaked");
  }
  Fixture transparent;transparent.button(0,1);int o=transparent.node(0,1,"DIV");transparent.layout(0,o,{0,0,250,100},100);
  require(transparent.run().dump().find("Visible button")!=std::string::npos,"Transparent ordinary layout box hid text");
  Fixture border;border.button(0,1,"HIDDEN_BORDER_COVER");int bn=border.node(0,1,"DIV");int bl=border.layout(0,bn,{0,0,250,100},100);border.style(0,bl,"border-top-width","100px");
  require(border.run().dump().find("HIDDEN_BORDER_COVER")==std::string::npos,"Border-only cover leaked text");
  Fixture shadow;shadow.button(0,1,"HIDDEN_SHADOW_COVER");int sn=shadow.node(0,1,"DIV");int sl=shadow.layout(0,sn,{500,500,1,1},100);shadow.style(0,sl,"box-shadow","0px 0px 0px 10000px rgb(255, 255, 255)");
  require(shadow.run().dump().find("HIDDEN_SHADOW_COVER")==std::string::npos,"Unbounded shadow cover leaked text");
  Fixture clipped;auto [shown,text]=clipped.button(0,1);int cp=clipped.node(0,1,"DIV");int cpl=clipped.layout(0,cp,{0,0,1,1},10);clipped.style(0,cpl,"overflow-x","hidden");clipped.style(0,cpl,"overflow-y","hidden");int ct=clipped.node(0,cp,"#text");int ctl=clipped.layout(0,ct,{0,0,300,80},10,"HIDDEN_CLIPPED_OCCLUDER");clipped.box(0,ctl,{0,0,300,80},0,23);
  auto proof=clipped.run();require(Fixture::output(proof,shown)["name"]=="Visible button","Clipped-away text falsely occluded a visible button");require(proof.dump().find("HIDDEN_")==std::string::npos,"Clipped occluder text leaked");
}
void corner_and_center_geometry_are_distinct() {
  Fixture corner;auto [button,text]=corner.button(0,1);int n=corner.node(0,1,"DIV");int l=corner.layout(0,n,{20,20,1,1},100);corner.style(0,l,"background-color","rgb(255, 255, 255)");
  auto visible=corner.run();require(Fixture::output(visible,button)["name"]=="Visible button","One-pixel corner cover removed a readable control");
  require(Fixture::output(visible,button)["geometryEvidence"]=="unoccluded_center_patch","Control geometry proof was not explicit");
  corner.snapshot["documents"][0]["layout"]["bounds"][l]={105,33,10,10};
  auto hidden=corner.run();require(Fixture::output(hidden,button).is_null(),"Covered center was reported as actionable geometry");
  require(hidden.dump().find("Visible button")==std::string::npos,"A partly covered text box leaked its full text");
}
void bounded_box_shadows() {
  // A late-painted decorative shadow (including one below the viewport)
  // must not suppress unrelated visible controls or their rendered labels.
  for(const auto& shadow:std::vector<std::string>{
      "rgb(136, 136, 136) 0px 0px 1px 0px",
      "rgba(0, 0, 0, 0.3) 0px 2px 6px 0px",
      "rgb(0, 0, 0) 0px -2px 0px 0px inset",
      "rgb(0, 0, 0) 1px 2px 3px -2px, rgba(0, 0, 0, 0.5) -3px -4px 6px 2px"}) {
    Fixture f;auto [button,text]=f.button(0,1);
    int n=f.node(0,1,"DIV"),l=f.layout(0,n,{500,900,20,20},100);f.style(0,l,"box-shadow",shadow);
    auto result=f.run();const auto& output=Fixture::output(result,button);
    require(!output.is_null() && output["name"]=="Visible button","A distant bounded box shadow hid unrelated viewport evidence");
  }
  for(const auto& shadow:std::vector<std::string>{
      "rgb(255, 255, 255) -400px -870px 0px 0px",
      "rgb(255, 255, 255) 0px 0px 0px 1000px",
      "rgb(0, 0, 0) 0px 0px 1px 0px, rgb(255, 255, 255) -400px -870px 0px 0px",
      "rgb(0, 0, 0) 0px 0px -1px 0px",
      "color(display-p3 1 1 1) 0px 0px 1px 0px",
      "rgb(0, 0, 0) 0px 0px 1px 0px garbage"}) {
    Fixture f;auto [button,text]=f.button(0,1,"HIDDEN_SHADOW_TEXT");
    int n=f.node(0,1,"DIV"),l=f.layout(0,n,{500,900,20,20},100);f.style(0,l,"box-shadow",shadow);
    auto result=f.run();require(Fixture::output(result,button).is_null(),"Covering or unsupported shadow exposed actionable geometry");
    require(result.dump().find("HIDDEN_SHADOW_TEXT")==std::string::npos,"Covering or unsupported shadow exposed text");
  }
  Fixture blur;auto [button,text]=blur.button(0,1,"HIDDEN_BLUR_TEXT");
  int n=blur.node(0,1,"DIV"),l=blur.layout(0,n,{205,20,20,40},100);blur.style(0,l,"box-shadow","rgb(255, 255, 255) 0px 0px 80px 0px");
  require(Fixture::output(blur.run(),button).is_null(),"Blur ink outside the layout box was ignored");
  require(blur.run().dump().find("HIDDEN_BLUR_TEXT")==std::string::npos,"Blur ink outside the layout box exposed text");
  Fixture clipped;auto [visible,t]=clipped.button(0,1);
  int parent=clipped.node(0,1,"DIV"),pl=clipped.layout(0,parent,{500,500,20,20},10);
  clipped.style(0,pl,"overflow-x","hidden");clipped.style(0,pl,"overflow-y","hidden");
  n=clipped.node(0,parent,"DIV");l=clipped.layout(0,n,{500,500,20,20},100);clipped.style(0,l,"box-shadow","rgb(255, 255, 255) 0px 0px 0px 1000px");
  require(Fixture::output(clipped.run(),visible)["name"]=="Visible button","Clipped-away bounded shadow suppressed unrelated text");
  for(const auto& [key,value]:std::vector<std::pair<std::string,std::string>>{{"transform","matrix(2, 0, 0, 2, 0, 0)"},{"perspective","100px"},{"zoom","2"}}) {
    Fixture scaled;scaled.button(0,1,"HIDDEN_SCALED_SHADOW");
    int p=scaled.node(0,1,"DIV"),layout=scaled.layout(0,p,{500,500,20,20},10);scaled.style(0,layout,key,value);
    int child=scaled.node(0,p,"DIV"),cl=scaled.layout(0,child,{500,500,20,20},100);scaled.style(0,cl,"box-shadow","rgb(0, 0, 0) 0px 0px 1px 0px");
    require(scaled.run().dump().find("HIDDEN_SCALED_SHADOW")==std::string::npos,"Shadow bounds were guessed through a transformed ancestor");
  }
  Fixture fragmented;fragmented.button(0,1,"HIDDEN_FRAGMENT_SHADOW");
  n=fragmented.node(0,1,"DIV");fragmented.layout(0,n,{500,500,20,20},100);
  l=fragmented.layout(0,n,{500,900,20,20},100);fragmented.style(0,l,"box-shadow","rgb(0, 0, 0) 0px 0px 1px 0px");
  require(fragmented.run().dump().find("HIDDEN_FRAGMENT_SHADOW")==std::string::npos,"Unsupported fragmented shadow ancestry was guessed");
}
void editable_values_and_visible_labels() {
  Fixture f;int label=f.node(0,1,"LABEL",{{"for","field"}});f.layout(0,label,{20,100,180,30});int lt=f.node(0,label,"#text");int tl=f.layout(0,lt,{20,100,160,20},1,"Visible account");f.box(0,tl,{20,100,160,20},0,15);
  int input=f.node(0,1,"INPUT",{{"id","field"},{"type","text"},{"aria-label","HIDDEN_LABEL"}});f.layout(0,input,{20,140,180,30},2);
  int ua=f.node(0,input,"#text");int ul=f.layout(0,ua,{22,142,160,20},3,"HIDDEN_EDITABLE_VALUE");f.box(0,ul,{22,142,160,20},0,21);
  int edit=f.node(0,1,"DIV",{{"contenteditable","true"}});f.layout(0,edit,{300,100,200,80});int et=f.node(0,edit,"#text");int el=f.layout(0,et,{302,102,160,20},2,"HIDDEN_CONTENTEDITABLE");f.box(0,el,{302,102,160,20},0,22);
  auto result=f.run();require(result.dump().find("HIDDEN_")==std::string::npos,"Editable value or hidden label leaked");
  require(Fixture::output(result,input)["name"]=="Visible account","Visible associated label missing");
  require(Fixture::output(result,input)["inputType"]=="text","Safe input type missing");
  for(size_t i=0;i<f.snapshot["documents"][0]["layout"]["nodeIndex"].size();++i)if(f.snapshot["documents"][0]["layout"]["nodeIndex"][i]==input){f.style(0,static_cast<int>(i),"overflow-x","clip");f.style(0,static_cast<int>(i),"overflow-y","clip");f.style(0,static_cast<int>(i),"border-left-width","2px");f.style(0,static_cast<int>(i),"border-top-width","2px");}
  require(!Fixture::output(f.run(),input).is_null(),"An input's own overflow incorrectly clipped its border-box geometry");
}
void svg_text_requires_visual_evidence() {
  Fixture f;int svg=f.node(0,1,"svg");f.layout(0,svg,{20,20,200,100});int text=f.node(0,svg,"text",{{"fill","transparent"}});f.layout(0,text,{25,25,180,20});
  int child=f.node(0,text,"#text");int layout=f.layout(0,child,{25,25,180,20},2,"HIDDEN_SVG_PAINT");f.box(0,layout,{25,25,180,20},0,16);
  auto result=f.run();require(result.dump().find("HIDDEN_")==std::string::npos,"SVG fill was misread as HTML text color");
  require(result["omitted"].contains("unsupportedTextPaint"),"SVG text omission not reported");require(!Fixture::output(result,svg).is_null(),"SVG geometry disappeared with its unsupported text");
}
void utf16_and_unicode_controls() {
  Fixture f;int t=f.node(0,1,"#text");const std::string value="A\xF0\x9F\x99\x82" "B";int l=f.layout(0,t,{20,20,60,20},1,value);f.box(0,l,{30,20,20,20},1,2);
  auto result=f.run();require(Fixture::output(result,t)["name"]=="\xF0\x9F\x99\x82","UTF-16 text-box indices split a supplementary code point");
  f.snapshot["documents"][0]["textBoxes"]["start"][0]=2;f.snapshot["documents"][0]["textBoxes"]["length"][0]=1;
  require(Fixture::output(f.run(),t)["name"]=="","Surrogate-half range was emitted");
  for(const std::string value:{"HIDDEN_\xE2\x80\x8B" "ZERO_WIDTH", "HIDDEN_\xE2\x80\xAE" "BIDI", "HIDDEN_\xF3\xA0\x81\xA1" "TAG"}) {
    Fixture g;g.button(0,1,value);auto out=g.run();require(out.dump().find("HIDDEN_")==std::string::npos,"Invisible Unicode controls were forwarded");require(out["omitted"].contains("unsafeUnicode"),"Unsafe Unicode omission not reported");
  }
}
void child_frames_and_malformed_evidence() {
  Fixture f;int owner=f.node(0,1,"IFRAME");f.layout(0,owner,{10,10,400,200},5);int child=f.document("child");int root=f.node(child,-1,"#document");int html=f.node(child,root,"HTML");f.layout(child,html,{0,0,400,200},0);auto [button,text]=f.button(child,html,"Frame button");
  f.snapshot["documents"][0]["nodes"]["contentDocumentIndex"]={{"index",{owner}},{"value",{child}}};
  auto result=f.run();require(result["frames"].contains("child"),"Visible iframe viewport not derived");require(Fixture::output(result,button,child,"child")["name"]=="Frame button","Child-frame visible text missing");require(result["frames"]["child"]["ownerBackendNodeId"]==1000+owner,"Frame owner provenance missing");
  f.snapshot["documents"][0]["layout"]["bounds"][1]={900,10,400,200};require(!f.run()["frames"].contains("child"),"Offscreen iframe exposed child content");
  Fixture malformed;malformed.button(0,1);malformed.snapshot["documents"][0]["textBoxes"]["start"][0]=99999;
  require(malformed.run()["partial"]==true,"Malformed text range did not fail closed");
  malformed.snapshot["documents"][0]["layout"].erase("paintOrders");require(malformed.run()["frames"]["main"]["nodes"].empty(),"Missing paint evidence did not fail closed");
  Json too_many={{"strings",Json::array()},{"documents",Json::array()}};for(int i=0;i<65;++i)too_many["documents"].push_back(Json::object());
  require(xenon::visible_snapshot(too_many,Json::object())["omitted"].contains("documentLimit"),"Document work bound missing");
}
}
int main(int argc,char** argv) {
  try {
    // Optional offline replay accepts only a caller-selected synthetic snapshot;
    // no browser diagnostics or raw website snapshots are enabled in production.
    if(argc==2){std::ifstream input(argv[1]);Json fixture;input>>fixture;std::cout<<xenon::visible_snapshot(fixture.at("snapshot"),fixture.at("viewports")).dump(2)<<'\n';return 0;}
    visible_text_and_hidden_metadata();chromium_layout_view_and_parent_blending();inherited_style_and_clipping();pixels_and_paint_order();corner_and_center_geometry_are_distinct();bounded_box_shadows();editable_values_and_visible_labels();svg_text_requires_visual_evidence();utf16_and_unicode_controls();child_frames_and_malformed_evidence();
    std::cout<<"Visible evidence regression tests passed\n";return 0;
  }catch(const std::exception& e){std::cerr<<"Visible evidence test failed: "<<e.what()<<'\n';return 1;}
}
