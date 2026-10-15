#include "../native/drag.h"
#include <cstdio>
#include <cstdlib>
static int n;
static void check(bool b){if(!b){fprintf(stderr,"failed check %d\n",n+1);exit(1);}n++;}
static Vec3 unit(Vec3 v){float d=sqrtf(v.x*v.x+v.y*v.y+v.z*v.z);return {v.x/d,v.y/d,v.z/d};}
static bool near(Vec3 a,Vec3 b){return fabsf(a.x-b.x)<.00001f&&fabsf(a.y-b.y)<.00001f&&fabsf(a.z-b.z)<.00001f;}
static Drag setup(int sx,int sy){Drag d{};d.w=1000;d.h=500;d.sx=sx;d.sy=sy;d.initial={2,1,2};d.position={0,0,-2};d.rotation={1,0,0,0};return d;}
int main(){
 int w=0,h=0;Vec3 origin{};Drag d=setup(1,0);
 check(projectDrag(d,{0,0,-1},origin,w,h)&&w==1000&&h==500);
 check(projectDrag(d,unit({.2f,0,-2}),origin,w,h)&&w==1100&&h==500);
 check(projectDrag(d,unit({.2f,0,-2}),origin,w,h)&&w==1100&&h==500);
 d=setup(-1,0);projectDrag(d,{0,0,-1},origin,w,h);check(projectDrag(d,unit({-.2f,0,-2}),origin,w,h)&&w==1100&&h==500);
 d=setup(0,-1);projectDrag(d,{0,0,-1},origin,w,h);check(projectDrag(d,unit({0,.1f,-2}),origin,w,h)&&w==1000&&h==550);
 d=setup(1,1);projectDrag(d,{0,0,-1},origin,w,h);check(projectDrag(d,unit({.2f,-.1f,-2}),origin,w,h)&&w==1100&&h==550);
 check(projectDrag(d,unit({20,-20,-2}),origin,w,h)&&w==2560&&h==1440);
 check(projectDrag(d,unit({-20,20,-2}),origin,w,h)&&w==480&&h==320);
 check(!projectDrag(d,{1,0,0},origin,w,h));
 check(!projectDrag(d,{0,0,1},origin,w,h));
 check(!projectDrag(d,{NAN,0,-1},origin,w,h));
 d=setup(1,0);d.rotation={cosf(.4f),0,sinf(.4f),0};d.position=rotate(d.rotation,d.position);
 projectDrag(d,rotate(d.rotation,{0,0,-1}),origin,w,h);
 check(projectDrag(d,rotate(d.rotation,unit({.2f,0,-2})),origin,w,h)&&w==1100&&h==500);
 Vec3 a=edgeOffset({2,1,2},1,0),b=edgeOffset({2,1.2f,2.4f},1,0);
 check(fabsf((a.x-b.x)-.170434f)<.00001f&&fabsf((a.z-b.z)+.104494f)<.00001f);
 for(int sx:{-1,1}){
  d=setup(sx,0);Cylinder c{2,.3f,.6f};Quat q=resizedRotation(d,c);Vec3 p=anchoredPosition(d,c,q);
  Vec3 e=rotate(q,edgeOffset(c,sx,0)),old=rotate(d.rotation,edgeOffset(d.initial,sx,0));
  check(near({p.x+e.x,p.y+e.y,p.z+e.z},{d.position.x+old.x,d.position.y+old.y,d.position.z+old.z}));
  Vec3 center=rotate(q,{0,0,c.radius});
  check(near({p.x+center.x,p.y+center.y,p.z+center.z},{0,0,0}));
  check(near(rotate(q,{0,0,1}),unit({-p.x,-p.y,-p.z})));
  check(near(anchoredPosition(d,d.initial,resizedRotation(d,d.initial)),d.position));
 }
 printf("%d native drag/edge-anchor host assertions passed\n",n);
}
