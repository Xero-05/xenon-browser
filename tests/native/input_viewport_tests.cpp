#include "xenon/input_viewport.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace xenon;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
// Page.getLayoutMetrics shapes from Chromium 154 at 100% display scale: a
// 932x689 view with classic 15 px scrollbars, unpinched and after a 2x pinch
// at its center. Pinch leaves the layout viewport unchanged.
Json metrics(double scale,double offset_x,double offset_y){
  return {{"cssLayoutViewport",{{"pageX",0},{"pageY",0},{"clientWidth",917},{"clientHeight",674}}},
    {"cssVisualViewport",{{"offsetX",offset_x},{"offsetY",offset_y},{"pageX",offset_x},{"pageY",offset_y},{"clientWidth",917/scale},{"clientHeight",674/scale},{"scale",scale},{"zoom",1}}},
    {"visualViewport",{{"clientWidth",917/scale},{"clientHeight",674/scale},{"scale",scale}}}};
}
int main(){try{
  const auto unpinched=input_viewport(metrics(1,0,0)),pinched=input_viewport(metrics(2,229.25,168.5)),panned=input_viewport(metrics(2,300,168.5));
  require(!unpinched.empty()&&!pinched.empty()&&!panned.empty(),"Complete layout metrics produce an input viewport");
  require(unpinched.at("layout")==pinched.at("layout"),"The fixture pinch keeps the layout viewport");
  require(unpinched!=pinched,"A pinch changes the screenshot's viewport identity");
  require(pinched!=panned,"Panning a pinched view changes the identity without a layout scroll");
  require(input_viewport(metrics(1,0,0))==unpinched,"Equal metrics keep the same identity");

  require(inside_input_viewport(unpinched,0,0)&&inside_input_viewport(unpinched,916.9,673.9),"The whole unpinched page area is valid");
  require(!inside_input_viewport(unpinched,917,10)&&!inside_input_viewport(unpinched,10,674),"Scrollbar pixels past the page area are rejected");
  require(!inside_input_viewport(unpinched,-0.1,10)&&!inside_input_viewport(unpinched,10,-0.1),"Negative coordinates are rejected");
  require(!inside_input_viewport(unpinched,std::nan(""),10)&&!inside_input_viewport(unpinched,10,std::numeric_limits<double>::infinity()),"Non-finite coordinates are rejected");
  require(inside_input_viewport(pinched,458,336.9),"Pinched coordinates span the visual viewport");
  require(!inside_input_viewport(pinched,644,462)&&!inside_input_viewport(pinched,458.5,10),"Points inside the layout viewport but outside the pinched view are rejected");

  // At 125% the visual viewport keeps fractional CSS pixels; the layout viewport rounds them down.
  auto fractional=metrics(1,0,0);fractional["cssLayoutViewport"]["clientWidth"]=1052;fractional["cssVisualViewport"]["clientWidth"]=1052.8;
  require(inside_input_viewport(input_viewport(fractional),1052.5,10),"The fractional last CSS pixel of the page remains valid");

  const auto [document_x,document_y]=input_viewport_document_point(pinched,10,20);
  require(std::abs(document_x-239.25)<1e-9&&std::abs(document_y-188.5)<1e-9,"Document hit points add the pinch pan offset");

  for(const auto* key:{"cssLayoutViewport","cssVisualViewport"}){auto missing=metrics(1,0,0);missing.erase(key);require(input_viewport(missing).empty(),"A missing viewport fails closed");}
  for(const auto* key:{"scale","clientWidth","clientHeight"}){auto zero=metrics(1,0,0);zero["cssVisualViewport"][key]=0;require(input_viewport(zero).empty(),"An empty or unscaled visual viewport fails closed");}
  auto text=metrics(1,0,0);text["cssVisualViewport"]["offsetX"]="0";require(input_viewport(text).empty(),"Non-numeric metrics fail closed");
  require(!inside_input_viewport(Json(),1,1)&&!inside_input_viewport(Json::object(),1,1),"No screenshot viewport accepts no coordinates");
  std::cout<<"Input viewport passed: pinch and pan identity, visual bounds, fractional edges and fail-closed metrics\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
