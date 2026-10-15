#pragma once
#include <cmath>
struct Vec3 {float x,y,z;};
// Firmware GLM build uses w,x,y,z quaternion storage (confirmed in cylinder disassembly).
struct Quat {float w,x,y,z;};
struct Vec2 {float x,y;};
struct Cylinder {float radius,angle,aspect;};
struct Rect {int left,top,right,bottom;};
using RayFunction=void(*)(Vec3,Vec3,Vec3,Quat,Cylinder,Vec2,bool&,Vec3&,float&,float&,Quat&,Rect);
struct Expansion {Cylinder cylinder;float fx,fy;};
inline bool expansion(Cylinder c,Vec2 size,Rect rect,Expansion& out){
    if(!std::isfinite(c.radius)||!std::isfinite(c.angle)||!std::isfinite(c.aspect)
       ||!std::isfinite(size.x)||!std::isfinite(size.y)||c.radius<=0||c.angle<=0||c.aspect<=0
       ||size.x<128||size.y<128||size.x>4096||size.y>4096)return false;
    if(rect.left!=-48||rect.top!=-48||rect.right!=size.x+48||rect.bottom!=size.y+48)return false;
    float fx=1+96/size.x,fy=1+96/size.y;
    // Firmware asin-based front-cylinder mapping needs an arc narrower than pi.
    if(c.angle*fx>=3.0f)return false;
    out={{c.radius,c.angle*fx,c.aspect*fx/fy},fx,fy};return true;
}
inline float remap(float value,float extent,float factor){return (value-extent*.5f)*factor+extent*.5f;}
inline bool within(float x,float y,Rect rect){
    return std::isfinite(x)&&std::isfinite(y)&&x>rect.left&&x<rect.right&&y>rect.top&&y<rect.bottom;
}
