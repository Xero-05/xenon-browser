#pragma once
#include "xenon/ui_theme.hpp"

namespace xenon::ui {
// Paint-only overlays. The transparent surfaces never receive page input.
constexpr int agent_pointer_width=46,agent_pointer_height=50,agent_pointer_hotspot=12;
inline Gdiplus::Color overlay_color(COLORREF tint,BYTE alpha){return {alpha,GetRValue(tint),GetGValue(tint),GetBValue(tint)};}
inline void agent_pointer(Gdiplus::Graphics& graphics,float scale,COLORREF tint,bool contrast){
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  graphics.ScaleTransform(scale,scale);
  const Gdiplus::PointF points[]{{12,12},{13,35},{20,27},{33,26}};
  Gdiplus::GraphicsPath path;path.AddPolygon(points,4);
  // Broad, translucent strokes fade away from the solid colored outline.
  if(!contrast)for(int spread=8;spread>=1;--spread){
    Gdiplus::Pen glow(overlay_color(tint,static_cast<BYTE>(4+(8-spread)*2)),2.0f+spread*2.0f);
    glow.SetLineJoin(Gdiplus::LineJoinRound);graphics.DrawPath(&glow,&path);
  }
  Gdiplus::SolidBrush fill(vector_color(contrast?palette().surface:RGB(255,255,255)));
  Gdiplus::Pen outline(vector_color(tint),2.2f);outline.SetLineJoin(Gdiplus::LineJoinRound);
  graphics.FillPath(&fill,&path);graphics.DrawPath(&outline,&path);
  graphics.ResetTransform();
}
inline void agent_frame_path(Gdiplus::GraphicsPath& path,RECT bounds,float scale,float radius,const RECT* tab=nullptr){
  const float x=bounds.left+scale,y=bounds.top+scale,right=bounds.right-scale,bottom=bounds.bottom-scale;
  if(right<=x||bottom<=y)return;
  const float r=std::min(radius,std::min(right-x,bottom-y)/2);
  if(r<=0){path.AddRectangle(Gdiplus::RectF(x,y,right-x,bottom-y));return;}
  path.AddArc(x,y,r*2,r*2,180,90);path.AddArc(right-r*2,y,r*2,r*2,270,90);
  path.AddArc(right-r*2,bottom-r*2,r*2,r*2,0,90);path.AddArc(x,bottom-r*2,r*2,r*2,90,90);
  if(tab){
    const float left=tab->left+scale,top=tab->top+scale,base=tab->bottom-scale;
    const float corner=std::min(8*scale,(base-top)/2),join=8*scale;
    // One continuous perimeter replaces the page's left edge at the opening.
    // These concave arcs match tab_junction's solid native border exactly.
    path.AddLine(x,bottom-r,x,base+join);
    path.AddArc(x-join*2,base,join*2,join*2,0,-90);
    path.AddLine(x-join,base,left+corner,base);
    path.AddArc(left,base-corner*2,corner*2,corner*2,90,90);
    path.AddArc(left,top,corner*2,corner*2,180,90);
    path.AddLine(left+corner,top,x-join,top);
    path.AddArc(x-join*2,top-join*2,join*2,join*2,90,-90);
    path.AddLine(x,top-join,x,y+r);
  }
  path.CloseFigure();
}
inline void inward_agent_glow(Gdiplus::Graphics& graphics,RECT bounds,float scale,float radius,COLORREF tint,const RECT* tab=nullptr){
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  Gdiplus::GraphicsPath path;agent_frame_path(path,bounds,scale,radius,tab);
  const auto state=graphics.Save();graphics.SetClip(&path,Gdiplus::CombineModeIntersect);
  // Clip the soft strokes to the connected shape: no outer halo, vertical
  // seam at the tab opening, or overlapping glow from separate rectangles.
  for(int spread=14;spread>=2;--spread){Gdiplus::Pen pen(overlay_color(tint,static_cast<BYTE>(16-spread)),2*spread*scale);
    pen.SetLineJoin(Gdiplus::LineJoinRound);graphics.DrawPath(&pen,&path);}
  graphics.Restore(state);
}
// UpdateLayeredWindow needs premultiplied alpha, not a color key: antialiasing
// and the glow must blend with arbitrary website colors without a matte fringe.
template<class Paint> bool paint_agent_layer(HWND window,SIZE size,Paint paint){
  if(size.cx<=0||size.cy<=0||!vector_rendering())return false;
  BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=size.cx;info.bmiHeader.biHeight=-size.cy;
  info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
  void* pixels{};auto bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);auto dc=CreateCompatibleDC(nullptr);
  if(!bitmap||!dc){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return false;}
  auto old=SelectObject(dc,bitmap);
  {Gdiplus::Bitmap surface(size.cx,size.cy,size.cx*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));
    Gdiplus::Graphics graphics(&surface);graphics.Clear(Gdiplus::Color(0,0,0,0));paint(graphics);graphics.Flush(Gdiplus::FlushIntentionSync);}
  POINT source{};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
  const bool updated=UpdateLayeredWindow(window,nullptr,nullptr,&size,dc,&source,0,&blend,ULW_ALPHA)!=FALSE;
  SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return updated;
}
}
