#pragma once
#include "cylinder.h"
#include <algorithm>
struct Drag {
    int id,w,h,sx,sy; Cylinder initial; Vec3 position; Quat rotation;
    bool first=true;float firstX=0,firstY=0;
};
inline Vec3 rotate(Quat q,Vec3 v){
    Vec3 u{q.x,q.y,q.z};
    Vec3 cross{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
    Vec3 twice{2*cross.x,2*cross.y,2*cross.z};
    return {v.x+q.w*twice.x+u.y*twice.z-u.z*twice.y,
            v.y+q.w*twice.y+u.z*twice.x-u.x*twice.z,
            v.z+q.w*twice.z+u.x*twice.y-u.y*twice.x};
}
inline Vec3 edgeOffset(Cylinder c,int sx,int sy){
    return {-sx*c.radius*sinf(c.angle*.5f),sy*c.radius*c.angle/c.aspect*.5f,
            sx?c.radius*(1-cosf(c.angle*.5f)):0};
}
inline Quat multiply(Quat a,Quat b){
    return {a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,
            a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};
}
// Follow the original cylinder around its center as the opposite edge stays fixed.
// Starting-pose math prevents accumulated rotation and asynchronous resize feedback.
inline Quat resizedRotation(const Drag& d,Cylinder c){
    float yaw=d.sx*(d.initial.angle-c.angle)*.5f;
    return multiply(d.rotation,{cosf(yaw*.5f),0,sinf(yaw*.5f),0});
}
inline Vec3 anchoredPosition(const Drag& d,Cylinder c,Quat rotation){
    Vec3 oldEdge=rotate(d.rotation,edgeOffset(d.initial,d.sx,d.sy));
    Vec3 newEdge=rotate(rotation,edgeOffset(c,d.sx,d.sy));
    return {d.position.x+oldEdge.x-newEdge.x,
            d.position.y+oldEdge.y-newEdge.y,
            d.position.z+oldEdge.z-newEdge.z};
}
inline bool projectDrag(Drag& drag,Vec3 direction,Vec3 origin,int& outW,int& outH){
    if(drag.w<=0||drag.h<=0||drag.initial.radius<=0||drag.initial.angle<=0||drag.initial.aspect<=0)return false;
    Vec3 normal=rotate(drag.rotation,{0,0,1});
    float dot=direction.x*normal.x+direction.y*normal.y+direction.z*normal.z;
    if(fabsf(dot)<.01f)return false;
    Vec3 delta{drag.position.x-origin.x,drag.position.y-origin.y,drag.position.z-origin.z};
    float distance=(delta.x*normal.x+delta.y*normal.y+delta.z*normal.z)/dot;
    if(!std::isfinite(distance)||distance<=0)return false;
    Vec3 local=rotate({drag.rotation.w,-drag.rotation.x,-drag.rotation.y,-drag.rotation.z},
        {origin.x+distance*direction.x-drag.position.x,
         origin.y+distance*direction.y-drag.position.y,
         origin.z+distance*direction.z-drag.position.z});
    if(drag.first){drag.firstX=local.x;drag.firstY=local.y;drag.first=false;}
    float pixel=drag.initial.radius*drag.initial.angle/drag.w;
    float pixelY=drag.initial.radius*drag.initial.angle/drag.initial.aspect/drag.h;
    if(pixel<=0||pixelY<=0)return false;
    if(!std::isfinite(local.x)||!std::isfinite(local.y)||!std::isfinite(pixel)||!std::isfinite(pixelY))return false;
    float desiredW=drag.sx?drag.w+drag.sx*(local.x-drag.firstX)/pixel:drag.w;
    float desiredH=drag.sy?drag.h-drag.sy*(local.y-drag.firstY)/pixelY:drag.h;
    outW=int(lroundf(std::max(480.f,std::min(2560.f,desiredW))));
    outH=int(lroundf(std::max(320.f,std::min(1440.f,desiredH))));
    return true;
}
