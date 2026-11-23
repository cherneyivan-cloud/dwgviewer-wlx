#include <windows.h>
#include <stdio.h>
int main(void){
  HDC sdc=GetDC(NULL); HDC dc=CreateCompatibleDC(sdc);
  BITMAPINFO bi; memset(&bi,0,sizeof bi);
  bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth=2000; bi.bmiHeader.biHeight=-300;
  bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
  void *bits=NULL; HBITMAP bm=CreateDIBSection(sdc,&bi,DIB_RGB_COLORS,&bits,NULL,0);
  HBITMAP old=(HBITMAP)SelectObject(dc,bm);
  RECT r={0,0,2000,300}; HBRUSH b=CreateSolidBrush(RGB(0,0,0)); FillRect(dc,&r,b); DeleteObject(b);
  int gm=SetGraphicsMode(dc,GM_ADVANCED);
  XFORM w={0}; w.eM11=0.5f; w.eM22=1;
  BOOL ok=SetWorldTransform(dc,&w);
  XFORM g; memset(&g,0,sizeof g); BOOL gok=GetWorldTransform(dc,&g);
  printf("gm=%d set=%d get=%d M11=%.3f M22=%.3f Dx=%.1f\n", gm, ok, gok, g.eM11, g.eM22, g.eDx);
  HPEN p=CreatePen(PS_SOLID,6,RGB(255,255,255)); HPEN op=(HPEN)SelectObject(dc,p);
  MoveToEx(dc,20,130,NULL); LineTo(dc,1200,130);
  int minx=2000,maxx=-1; for(int y=120;y<140;y++) for(int x=0;x<2000;x++){ if(GetPixel(dc,x,y)!=0x00000000){ if(x<minx)minx=x; if(x>maxx)maxx=x; } }
  printf("line ink x=%d..%d\n", minx, maxx);
  SelectObject(dc,op); DeleteObject(p); SelectObject(dc,old); DeleteObject(bm); DeleteDC(dc); ReleaseDC(NULL,sdc);
  return 0;
}
