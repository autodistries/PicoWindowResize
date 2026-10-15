#include "drag.h"
#include "resolve.h"
#include "abi.h"
#include <cstdio>
#include <jni.h>
#include <link.h>
#include <elf.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <atomic>
#include <algorithm>
#include <time.h>
#include <android/log.h>

static RayFunction original=nullptr;
static std::atomic<bool> active{false};
static std::atomic<unsigned long> attempts{0},successes{0};
static int installed=0;
static unsigned checksPassed=0;
static char loadedBuildId[129]="unavailable";
struct NativeApi {
    void* manager=nullptr;uintptr_t cylinderVptr=0,controllerVptr=0;
    void* (*panelById)(void*,int)=nullptr;
    int (*deviceId)(void*)=nullptr;
    Cylinder (*cylinderInfo)(void*)=nullptr;
    Vec3 (*position)(void*)=nullptr;Quat (*orientation)(void*)=nullptr;
    void (*setPosition)(void*,const Vec3&)=nullptr;
    void (*setOrientation)(void*,const Quat&)=nullptr;
    bool (*triggerValue)(void*,float&)=nullptr;
    PanelLayout layout;
};
static NativeApi api;
static std::atomic<bool> sawTriggerHeld{false};
static void* getPanel(int id){return api.panelById(api.manager,id);}
static void* getLayer(void* panel){return *(void**)((char*)panel+api.layout.layer);}
static void* getTransform(void* layer){return *(void**)((char*)layer+api.layout.transform);}
struct PanelVector {void** begin;void** end;void** capacity;};
using HitFunction=void(*)(void*,const PanelVector&,void*,void*);
static HitFunction originalHit=nullptr;
static std::atomic<int> capturedId{0},capturedDevice{0},targetWidth{0},targetHeight{0};
static std::atomic<unsigned long> dragSerial{0};
static std::atomic<bool> triggerReleased{false};
static thread_local bool forced=false;
static std::atomic<long long> anchorUntil{0};
static long long nowNs(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000000000+t.tv_nsec;}
static Drag drag{}; // Read/write only on engine thread, published through capturedId.
static void updateDrag(Vec3 direction,Vec3 origin){
    int w,h;if(!projectDrag(drag,direction,origin,w,h))return;
    targetWidth.store(w,std::memory_order_relaxed);targetHeight.store(h,std::memory_order_relaxed);
    dragSerial.fetch_add(1,std::memory_order_release);
}
static void hitProxy(void* manager,const PanelVector& panels,void* info,void* source){
    int id=capturedId.load(std::memory_order_acquire);
    if(active.load(std::memory_order_relaxed)&&id>0&&source&&api.manager){
        int device=api.deviceId(source);
        if(device==capturedDevice.load(std::memory_order_relaxed)){
            if(*(uintptr_t*)source==api.controllerVptr){
                float value=NAN;
                if(api.triggerValue(source,value)&&std::isfinite(value)&&value>=0&&value<=1){
                    if(value>.1f)sawTriggerHeld.store(true,std::memory_order_relaxed);
                    else if(sawTriggerHeld.load(std::memory_order_relaxed)){
                        triggerReleased.store(true,std::memory_order_release);
                        originalHit(manager,panels,info,source);return;
                    }
                }
            }
            void* panel=getPanel(id);
            if(panel){
                PanelVector single{&panel,&panel+1,&panel+1};
                forced=true;originalHit(manager,single,info,source);forced=false;return;
            }
            capturedId.store(0,std::memory_order_release);
        }
    }
    originalHit(manager,panels,info,source);
}
static void retry(Vec3 direction,Vec3 origin,Vec3 center,Quat rotation,Cylinder cylinder,Vec2 size,
                  bool& hit,Vec3& point,float& x,float& y,Quat& normal,Rect rect){
    if(forced&&capturedId.load(std::memory_order_relaxed)>0)updateDrag(direction,origin);
    original(direction,origin,center,rotation,cylinder,size,hit,point,x,y,normal,rect);
    if(hit||!active.load(std::memory_order_relaxed))return;
    Expansion expand;
    Rect effective=rect;
    if(forced&&capturedId.load(std::memory_order_relaxed)>0){
        // Broad interaction only while this controller owns the drag; rendered geometry unchanged.
        float fx=std::min(1+8192/size.x,2.95f/cylinder.angle),fy=1+8192/size.y;
        if(!std::isfinite(fx)||fx<1||cylinder.radius<=0||cylinder.aspect<=0)return;
        expand={{cylinder.radius,cylinder.angle*fx,cylinder.aspect*fx/fy},fx,fy};
        effective={-4096,-4096,(int)size.x+4096,(int)size.y+4096};
    }else if(!expansion(cylinder,size,rect,expand))return;
    attempts.fetch_add(1,std::memory_order_relaxed);
    bool extra=false;Vec3 extraPoint{};float u=0,v=0;Quat extraNormal{};
    original(direction,origin,center,rotation,expand.cylinder,size,extra,extraPoint,u,v,extraNormal,
             {0,0,(int)size.x,(int)size.y});
    if(!extra)return;
    u=remap(u,size.x,expand.fx);v=remap(v,size.y,expand.fy);
    if(!within(u,v,effective))return;
    // A fallback must never re-enable a stock-rejected point inside app content.
    if(!forced&&u>=0&&u<=size.x&&v>=0&&v<=size.y)return;
    hit=true;point=extraPoint;x=u;y=v;normal=extraNormal;
    successes.fetch_add(1,std::memory_order_relaxed);
}
static bool selfTest(){
    // Calls the actual firmware function before touching GOT: verifies aggregate ABI + geometry.
    Vec3 center{},point{};Quat identity{1,0,0,0},normal{};Cylinder c{2,1,2};Vec2 size{1000,500};
    bool hit=false;float x=0,y=0;
    original({0,0,-1},center,center,identity,c,size,hit,point,x,y,normal,{0,0,1000,500});
    if(!hit||fabsf(x-500)>1||fabsf(y-250)>1||fabsf(point.z+2)>.01f)return false;
    Expansion e;if(!expansion(c,size,{-48,-48,1048,548},e))return false;
    const float theta=.53f;
    Vec3 end{sinf(theta),0,-cosf(theta)};
    original(end,center,center,identity,c,size,hit,point,x,y,normal,{-48,-48,1048,548});
    if(hit)return false; // outside stock arc
    original(end,center,center,identity,e.cylinder,size,hit,point,x,y,normal,{0,0,1000,500});
    if(!hit||fabsf(remap(x,1000,e.fx)-1030)>2||fabsf(remap(y,500,e.fy)-250)>1)return false;
    original({0,.56f/sqrtf(4+.56f*.56f),-2/sqrtf(4+.56f*.56f)},center,center,identity,c,size,hit,point,x,y,normal,{-48,-48,1048,548});
    if(hit)return false; // outside stock height (1 metre)
    original({0,.56f/sqrtf(4+.56f*.56f),-2/sqrtf(4+.56f*.56f)},center,center,identity,e.cylinder,size,hit,point,x,y,normal,{0,0,1000,500});
    if(!hit||fabsf(remap(y,500,e.fy)+30)>=2)return false;
    Quat yaw{cosf(.2f),0,sinf(.2f),0};Vec3 translated{.4f,-.2f,.3f};
    Vec3 direction=rotate(yaw,{0,0,-1}),expected=rotate(yaw,{0,0,-2});
    original(direction,translated,translated,yaw,c,size,hit,point,x,y,normal,{0,0,1000,500});
    return hit&&fabsf(x-500)<1&&fabsf(y-250)<1
        &&fabsf(point.x-translated.x-expected.x)<.01f
        &&fabsf(point.y-translated.y-expected.y)<.01f
        &&fabsf(point.z-translated.z-expected.z)<.01f;
}
static bool resolveApi(const ModuleView& view){
    NativeApi next;
    next.manager=view.symbol("_ZZN7XRShell5Utils9SingletonI12PanelManagerE11getInstanceEvE8instance",PF_R,sizeof(void*));
    auto cv=view.symbol("_ZTV13CylinderLayer",PF_R,3*sizeof(void*));
    auto iv=view.symbol("_ZTV21ControllerInputSource",PF_R,3*sizeof(void*));
    if(!next.manager||!cv||!iv){__android_log_print(ANDROID_LOG_ERROR,"PicoBorderResize","missing manager/vtables: %p %p %p",next.manager,cv,iv);return false;}
    next.cylinderVptr=(uintptr_t)cv+2*sizeof(void*);next.controllerVptr=(uintptr_t)iv+2*sizeof(void*);
    next.panelById=(decltype(next.panelById))view.symbol("_ZN12PanelManager12getPanelByIdEi");
    next.deviceId=(decltype(next.deviceId))view.symbol("_ZNK15InputSourceBase11getDeviceIdEv");
    next.cylinderInfo=(decltype(next.cylinderInfo))view.symbol("_ZN13CylinderLayer15getCylinderInfoEv",PF_R|PF_X,sizeof(cylinderReturn));
    next.position=(decltype(next.position))view.symbol("_ZN11XRTransform16getWorldPositionEv",PF_R|PF_X,sizeof(positionReturn));
    next.orientation=(decltype(next.orientation))view.symbol("_ZN11XRTransform19getWorldOrientationEv",PF_R|PF_X,sizeof(orientationReturn));
    next.setPosition=(decltype(next.setPosition))view.symbol("_ZN11XRTransform16setWorldPositionERKN3glm3vecILi3EfLNS0_9qualifierE0EEE");
    next.setOrientation=(decltype(next.setOrientation))view.symbol("_ZN11XRTransform19setWorldOrientationERKN3glm3quaIfLNS0_9qualifierE0EEE");
    next.triggerValue=(decltype(next.triggerValue))view.symbol("_ZNK21ControllerInputSource15getTriggerValueERf");
    auto valid=(const uint32_t*)view.symbol("_ZNK5Panel7isValidEv",PF_R|PF_X,8*sizeof(uint32_t));
    auto keyboard=(const uint32_t*)view.symbol("_ZN12PanelManager10isKeyboardEP5Panel",PF_R|PF_X,9*sizeof(uint32_t));
    if(!next.panelById||!next.deviceId||!next.cylinderInfo||!next.position||!next.orientation
       ||!next.setPosition||!next.setOrientation||!next.triggerValue||!valid||!keyboard)return false;
    checksPassed|=1;
    if(!deriveLayout(valid,keyboard,next.layout))return false;
    checksPassed|=2;
    if(!accessorContract((const uint32_t*)next.cylinderInfo,cylinderReturn)
       ||!accessorContract((const uint32_t*)next.position,positionReturn)
       ||!accessorContract((const uint32_t*)next.orientation,orientationReturn))return false;
    checksPassed|=4;
    api=next;return true;
}
static int pageProtection(uintptr_t address){
    FILE* maps=fopen("/proc/self/maps","r");if(!maps)return -1;
    char line[1024],mode[5];unsigned long first,last;int result=-1;
    while(fgets(line,sizeof(line),maps))if(sscanf(line,"%lx-%lx %4s",&first,&last,mode)==3&&address>=first&&address<last){
        result=(mode[0]=='r'?PROT_READ:0)|(mode[1]=='w'?PROT_WRITE:0)|(mode[2]=='x'?PROT_EXEC:0);break;
    }
    fclose(maps);return result;
}
static bool patchSlots(uintptr_t* raySlot,uintptr_t* hitSlot){
    long size=sysconf(_SC_PAGESIZE);if(size<=0||(size&(size-1)))return false;
    uintptr_t pages[2]={(uintptr_t)raySlot&~(uintptr_t(size)-1),(uintptr_t)hitSlot&~(uintptr_t(size)-1)};
    int count=pages[0]==pages[1]?1:2,protection[2];
    for(int i=0;i<count;i++){
        protection[i]=pageProtection(pages[i]);
        if(protection[i]<0||!(protection[i]&PROT_READ)||(protection[i]&PROT_EXEC))return false;
    }
    int opened=0;
    for(;opened<count;opened++)if(mprotect((void*)pages[opened],size,protection[opened]|PROT_WRITE))break;
    if(opened!=count){for(int i=0;i<opened;i++)mprotect((void*)pages[i],size,protection[i]);return false;}
    __atomic_store_n(raySlot,(uintptr_t)&retry,__ATOMIC_RELEASE);
    __atomic_store_n(hitSlot,(uintptr_t)&hitProxy,__ATOMIC_RELEASE);
    bool restored=true;for(int i=0;i<count;i++)if(mprotect((void*)pages[i],size,protection[i]))restored=false;
    if(!restored){
        bool writable=true;for(int i=0;i<count;i++)if(mprotect((void*)pages[i],size,protection[i]|PROT_WRITE))writable=false;
        if(writable){__atomic_store_n(raySlot,(uintptr_t)original,__ATOMIC_RELEASE);__atomic_store_n(hitSlot,(uintptr_t)originalHit,__ATOMIC_RELEASE);}
        for(int i=0;i<count;i++)mprotect((void*)pages[i],size,protection[i]);
    }
    return restored;
}
static void readBuildId(const ModuleView& view){
    for(int i=0;i<view.info.dlpi_phnum;i++){
        auto& p=view.info.dlpi_phdr[i];if(p.p_type!=PT_NOTE)continue;
        uintptr_t address=view.info.dlpi_addr+p.p_vaddr;
        size_t size=p.p_memsz;if(!view.contains(address,size))continue;
        for(size_t at=0;at<=size&&size-at>=sizeof(Elf64_Nhdr);){
            auto n=(const Elf64_Nhdr*)(address+at);at+=sizeof(*n);
            size_t names=(size_t(n->n_namesz)+3)&~size_t(3),desc=(size_t(n->n_descsz)+3)&~size_t(3);
            if(names>size-at||desc>size-at-names)break;
            if(n->n_type==NT_GNU_BUILD_ID&&n->n_namesz==4&&!memcmp((void*)(address+at),"GNU",4)
               &&n->n_descsz>0&&n->n_descsz<=64){
                auto bytes=(const unsigned char*)(address+at+names);
                for(unsigned j=0;j<n->n_descsz;j++)snprintf(loadedBuildId+j*2,3,"%02x",bytes[j]);
                return;
            }
            at+=names+desc;
        }
    }
}
static int locate(dl_phdr_info* info,size_t,void*){
    const char* name=info->dlpi_name;if(!name)return 0;
    const char* leaf=strrchr(name,'/');leaf=leaf?leaf+1:name;
    if(strcmp(leaf,"libxrshell.so"))return 0;
    ModuleView view{*info};readBuildId(view);
    original=(RayFunction)view.symbol(raySymbol);
    originalHit=(HitFunction)view.symbol(hitSymbol);
    auto slot=view.jumpSlot(raySymbol),hitSlot=view.jumpSlot(hitSymbol);
    __android_log_print(ANDROID_LOG_INFO,"PicoBorderResize","discovery ray=%p hit=%p raySlot=%p hitSlot=%p",(void*)original,(void*)originalHit,(void*)slot,(void*)hitSlot);
    if(slot&&hitSlot)checksPassed|=8;
    bool ready=original&&originalHit&&slot&&hitSlot&&resolveApi(view);
    if(!ready){installed=-1;return 1;}
    if(__atomic_load_n(slot,__ATOMIC_ACQUIRE)!=(uintptr_t)original||__atomic_load_n(hitSlot,__ATOMIC_ACQUIRE)!=(uintptr_t)originalHit){installed=-2;return 1;}
    checksPassed|=16;
    if(!selfTest()){installed=-3;return 1;}
    checksPassed|=32;
    if(!patchSlots(slot,hitSlot)){installed=-4;return 1;}
    checksPassed|=64;installed=1;return 1;
}
extern "C" JNIEXPORT jint JNICALL Java_research_pico_windowresize_NativeBorder_install(JNIEnv*,jclass){
    if(!installed)dl_iterate_phdr(locate,nullptr);
    return installed;
}
extern "C" JNIEXPORT jstring JNICALL Java_research_pico_windowresize_NativeBorder_diagnostics(JNIEnv* env,jclass){
    const char* labels[]={"Native exports and vtables","Panel layout contract","Accessor ABI contracts","ELF hook relocations","Original hook targets","Ray ABI and geometry self-test","Hook installation"};
    char text[2048];int used=snprintf(text,sizeof(text),"Loaded ELF build ID: %s\n",loadedBuildId);
    for(unsigned i=0;i<7;i++)used+=snprintf(text+used,sizeof(text)-used,"%s: %s\n",labels[i],
        (checksPassed&(1u<<i))?"PASS":(installed==0?"WAITING":"NOT PASSED"));
    return env->NewStringUTF(text);
}
extern "C" JNIEXPORT void JNICALL Java_research_pico_windowresize_NativeBorder_setEnabled(JNIEnv*,jclass,jboolean value){active.store(value==JNI_TRUE,std::memory_order_release);if(!value)capturedId.store(0,std::memory_order_release);}
extern "C" JNIEXPORT jlongArray JNICALL Java_research_pico_windowresize_NativeBorder_stats(JNIEnv* env,jclass){
    jlong data[]={installed,(jlong)attempts.load(),(jlong)successes.load()};
    jlongArray result=env->NewLongArray(3);if(result)env->SetLongArrayRegion(result,0,3,data);return result;
}

// begin/anchor are called through XREngine.queueTask, never on UI/Binder threads.
extern "C" JNIEXPORT jboolean JNICALL Java_research_pico_windowresize_NativeBorder_begin(JNIEnv*,jclass,jint id,jint device,jint w,jint h,jint sx,jint sy){
    if(installed!=1||!active.load()||id<=0||(device!=1&&device!=2)||w<=0||h<=0)return false;
    void* panel=getPanel(id);
    if(!panel||*(int*)((char*)panel+api.layout.type)!=3002)return false;
    void* layer=getLayer(panel);if(!layer)return false;
    if(*(uintptr_t*)layer!=api.cylinderVptr)return false;
    void* transform=getTransform(layer);if(!transform)return false;
    Cylinder c=api.cylinderInfo(layer);
    if(!std::isfinite(c.radius)||!std::isfinite(c.angle)||!std::isfinite(c.aspect)||c.radius<=0||c.angle<=0||c.aspect<=0)return false;
    Drag next{};next.id=id;next.w=w;next.h=h;next.sx=sx;next.sy=sy;next.initial=c;
    next.position=api.position(transform);
    next.rotation=api.orientation(transform);
    drag=next;anchorUntil=0;targetWidth=w;targetHeight=h;dragSerial=0;capturedDevice=device;triggerReleased=false;sawTriggerHeld=false;
    capturedId.store(id,std::memory_order_release);return true;
}
extern "C" JNIEXPORT void JNICALL Java_research_pico_windowresize_NativeBorder_end(JNIEnv*,jclass){anchorUntil.store(nowNs()+1000000000);capturedId.store(0,std::memory_order_release);}
extern "C" JNIEXPORT jintArray JNICALL Java_research_pico_windowresize_NativeBorder_dragSize(JNIEnv* env,jclass){
    jint data[]={capturedId.load(),targetWidth.load(),targetHeight.load(),(jint)dragSerial.load(std::memory_order_acquire),triggerReleased.load(std::memory_order_acquire)?1:0};
    jintArray result=env->NewIntArray(5);if(result)env->SetIntArrayRegion(result,0,5,data);return result;
}
extern "C" JNIEXPORT void JNICALL Java_research_pico_windowresize_NativeBorder_anchor(JNIEnv*,jclass,jint id){
    if(id!=drag.id||!api.manager||!active.load()||(capturedId.load()!=id&&nowNs()>anchorUntil.load()))return;
    void* panel=getPanel(id);
    if(!panel||*(int*)((char*)panel+api.layout.type)!=3002)return;
    void* layer=getLayer(panel);if(!layer||*(uintptr_t*)layer!=api.cylinderVptr)return;
    void* transform=getTransform(layer);if(!transform)return;
    Cylinder c=api.cylinderInfo(layer);
    if(!std::isfinite(c.radius)||!std::isfinite(c.angle)||!std::isfinite(c.aspect)||c.radius<=0||c.angle<=0||c.aspect<=0)return;
    Quat rotation=resizedRotation(drag,c);
    Vec3 position=anchoredPosition(drag,c,rotation);
    api.setOrientation(transform,rotation);
    api.setPosition(transform,position);
}
