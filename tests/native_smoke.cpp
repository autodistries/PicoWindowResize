#include "../native/borderresize.cpp"
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
static int checks=0;
static void check(bool value,const char* what){checks++;if(!value){fprintf(stderr,"FAIL: %s\n",what);exit(1);}}
int main(){
    void* library=dlopen("/system/priv-app/XRShell/XRShell.apk!/lib/arm64-v8a/libxrshell.so",RTLD_NOW);
    if(!library){fprintf(stderr,"dlopen: %s\n",dlerror());return 2;}
    original=(RayFunction)dlsym(library,"_Z19rayHitCylinderLayerN3glm3vecILi3EfLNS_9qualifierE0EEES2_S2_NS_3quaIfLS1_0EEE12CylinderInfoNS0_ILi2EfLS1_0EEERbRS2_RfS9_RS4_5ARect");
    check(original,"firmware cylinder symbol");
    // Print ABI probe before fail-closed self-test to make any layout mismatch diagnosable.
    Vec3 zero{},point{};Quat q{1,0,0,0},normal{};Cylinder c{2,1,2};Vec2 size{1000,500};bool hit=false;float x=0,y=0;
    original({0,0,-1},zero,zero,q,c,size,hit,point,x,y,normal,{0,0,1000,500});
    printf("ABI probe hit=%d xy=%f,%f point=%f,%f,%f\n",hit,x,y,point.x,point.y,point.z);
    check(selfTest(),"real firmware ABI/expanded geometry self-test");
    Rect margin{-48,-48,1048,548};
    active=true;
    retry({0,0,-1},zero,zero,q,c,size,hit,point,x,y,normal,margin);
    check(hit&&fabsf(x-500)<1&&fabsf(y-250)<1,"inside preserved");
    for(float side:{-1.f,1.f}){
        float theta=.53f*side;
        retry({sinf(theta),0,-cosf(theta)},zero,zero,q,c,size,hit,point,x,y,normal,margin);
        check(hit&&fabsf(x-(500+530*side))<2,"outside side delivered");
    }
    retry({0,.56f/sqrtf(4+.56f*.56f),-2/sqrtf(4+.56f*.56f)},zero,zero,q,c,size,hit,point,x,y,normal,margin);
    check(hit&&fabsf(y+30)<2,"outside top delivered");
    retry({0,-.56f/sqrtf(4+.56f*.56f),-2/sqrtf(4+.56f*.56f)},zero,zero,q,c,size,hit,point,x,y,normal,margin);
    check(hit&&fabsf(y-530)<2,"outside bottom delivered");
    retry({sinf(.6f),0,-cosf(.6f)},zero,zero,q,c,size,hit,point,x,y,normal,margin);
    check(!hit,"beyond margin excluded");
    retry({sinf(.53f),0,-cosf(.53f)},zero,zero,q,c,size,hit,point,x,y,normal,{0,0,1000,500});
    check(!hit,"unmarked panels excluded");
    active=false;
    retry({sinf(.53f),0,-cosf(.53f)},zero,zero,q,c,size,hit,point,x,y,normal,margin);
    check(!hit,"disabled passthrough");
    active=true;forced=true;capturedId=99;
    retry({sinf(.6f),0,-cosf(.6f)},zero,zero,q,c,size,hit,point,x,y,normal,margin);
    check(hit&&x>1048,"owned capture survives beyond narrow band");
    forced=false;capturedId=0;
    drag=Drag{};drag.w=1000;drag.h=500;drag.sx=1;drag.initial=c;drag.position={0,0,-2};drag.rotation=q;
    updateDrag({0,0,-1},zero);updateDrag({.2f/sqrtf(4.04f),0,-2/sqrtf(4.04f)},zero);
    check(targetWidth==1100&&targetHeight==500,"right edge changes width only");
    updateDrag({.2f/sqrtf(4.04f),0,-2/sqrtf(4.04f)},zero);
    check(targetWidth==1100,"stationary ray produces no size feedback");
    drag.first=true;drag.sx=-1;updateDrag({0,0,-1},zero);updateDrag({-.2f/sqrtf(4.04f),0,-2/sqrtf(4.04f)},zero);
    check(targetWidth==1100&&targetHeight==500,"left edge independent resize");
    drag.first=true;drag.sx=0;drag.sy=-1;updateDrag({0,0,-1},zero);updateDrag({0,.1f/sqrtf(4.01f),-2/sqrtf(4.01f)},zero);
    check(targetWidth==1000&&targetHeight==550,"top edge changes height only");
    drag.first=true;drag.sx=1;drag.sy=1;updateDrag({0,0,-1},zero);updateDrag({.2f/sqrtf(4.05f),-.1f/sqrtf(4.05f),-2/sqrtf(4.05f)},zero);
    check(targetWidth==1100&&targetHeight==550,"corner changes both dimensions");
    updateDrag({20/sqrtf(804),-20/sqrtf(804),-2/sqrtf(804)},zero);
    check(targetWidth==2560&&targetHeight==1440,"display size ceilings");
    updateDrag({-20/sqrtf(804),20/sqrtf(804),-2/sqrtf(804)},zero);
    check(targetWidth==480&&targetHeight==320,"display size floors");
    Cylinder resized{2,1.2f,2.4f};
    for(int sx:{-1,1}){
        Vec3 before=edgeOffset(c,sx,0),after=edgeOffset(resized,sx,0);
        Vec3 shift{before.x-after.x,before.y-after.y,before.z-after.z};
        check(fabsf(shift.x+after.x-before.x)<.00001f&&fabsf(shift.z+after.z-before.z)<.00001f,"opposite cylinder edge remains anchored");
    }
    check(installed==0,"geometry test did not patch GOT");
    dl_iterate_phdr(locate,nullptr);
    check(installed==1,"symbol/layout checks, self-test and GOT install in isolated process");
    check(checksPassed==127,"all seven diagnostic check groups passed");
    check(strcmp(loadedBuildId,"unavailable")!=0&&strlen(loadedBuildId)>=32,"loaded ELF build ID discovered");
    alignas(16) unsigned char transform[4096]{};
    ((void(*)(void*))dlsym(library,"_ZN11XRTransformC1Ev"))(transform);
    Vec3 desired{1.2f,-.4f,-2.3f};
    api.setPosition(transform,desired);
    Vec3 actual=api.position(transform);
    check(fabsf(actual.x-desired.x)<.0001f&&fabsf(actual.y-desired.y)<.0001f&&fabsf(actual.z-desired.z)<.0001f,"actual firmware world position setter/getter ABI");
    Quat orientation=api.orientation(transform);
    check(fabsf(orientation.w-1)<.0001f&&fabsf(orientation.x)<.0001f&&fabsf(orientation.y)<.0001f&&fabsf(orientation.z)<.0001f,"actual firmware quaternion storage ABI");
    Quat wanted{cosf(.2f),0,sinf(.2f),0};
    api.setOrientation(transform,wanted);
    orientation=api.orientation(transform);
    check(fabsf(orientation.w-wanted.w)<.0001f&&fabsf(orientation.y-wanted.y)<.0001f&&fabsf(orientation.x)<.0001f&&fabsf(orientation.z)<.0001f,"actual firmware world orientation setter/getter ABI");
    ((void(*)(void*))dlsym(library,"_ZN11XRTransformD1Ev"))(transform);
    printf("native install status=%d\n",installed);
    printf("%d native assertions passed (firmware ray checks + drag geometry); isolated-process GOT tested; compositor untouched\n",checks);
}
