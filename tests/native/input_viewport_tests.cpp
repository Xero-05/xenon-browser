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

  // A pointer left at an unpinched corner is pulled into the pinched view.
  const auto [clamped_x,clamped_y]=input_viewport_clamp(pinched,873,645);
  require(inside_input_viewport(pinched,clamped_x,clamped_y)&&std::abs(clamped_x-458.49)<1e-9&&std::abs(clamped_y-336.99)<1e-9,"Pointer moves are clamped to the pinched view");
  const auto [kept_x,kept_y]=input_viewport_clamp(pinched,100.5,200.25);
  require(kept_x==100.5&&kept_y==200.25,"Points inside the pinched view are unchanged");
  const auto [low_x,low_y]=input_viewport_clamp(pinched,-5,std::nan(""));
  require(low_x==0&&low_y==0,"Negative or non-finite moves are clamped to the view origin");
  const auto [document_x,document_y]=input_viewport_document_point(pinched,10,20);
  require(std::abs(document_x-239.25)<1e-9&&std::abs(document_y-188.5)<1e-9,"Document hit points add the pinch pan offset");

  // A person's pointer at window point (600,400) is input (300,200) at 2x,
  // with or without a pan: the visual viewport is drawn at the window corner.
  require(input_viewport_scale(unpinched)==1&&input_viewport_scale(pinched)==2,"The pinch scale comes from the visual viewport");
  const auto [window_x,window_y]=input_viewport_from_window(pinched,600,400);
  const auto [panned_x,panned_y]=input_viewport_from_window(panned,600,400);
  require(window_x==300&&window_y==200&&panned_x==300&&panned_y==200,"Window points divide by the pinch scale without a pan offset");
  require(inside_input_viewport(pinched,window_x,window_y)&&!inside_input_viewport(pinched,600,400),"Only the converted window point is inside the pinched view");
  const auto [unpinched_x,unpinched_y]=input_viewport_from_window(unpinched,600,400);
  require(unpinched_x==600&&unpinched_y==400,"Unpinched window points are input points");
  const auto [inspected_x,inspected_y]=input_viewport_document_point(pinched,window_x,window_y);
  require(std::abs(inspected_x-529.25)<1e-9&&std::abs(inspected_y-368.5)<1e-9,"An inspected window point hits the document under it");
  require(input_viewport_scale(Json())==1&&input_viewport_scale(Json::object())==1,"Without a viewport, window points pass through unscaled");

  for(const auto* key:{"cssLayoutViewport","cssVisualViewport"}){auto missing=metrics(1,0,0);missing.erase(key);require(input_viewport(missing).empty(),"A missing viewport fails closed");}
  for(const auto* key:{"scale","clientWidth","clientHeight"}){auto zero=metrics(1,0,0);zero["cssVisualViewport"][key]=0;require(input_viewport(zero).empty(),"An empty or unscaled visual viewport fails closed");}
  auto text=metrics(1,0,0);text["cssVisualViewport"]["offsetX"]="0";require(input_viewport(text).empty(),"Non-numeric metrics fail closed");
  require(!inside_input_viewport(Json(),1,1)&&!inside_input_viewport(Json::object(),1,1),"No screenshot viewport accepts no coordinates");
  std::cout<<"Input viewport passed: pinch and pan identity, visual bounds, window conversion, fractional edges and fail-closed metrics\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
