#include "drag.h"
#include <jni.h>
#include <link.h>
#include <elf.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <atomic>
#include <algorithm>
#include <time.h>

static RayFunction original=nullptr;
static std::atomic<bool> active{false};
static std::atomic<unsigned long> attempts{0},successes{0};
static int installed=0;
static const unsigned char expectedId[]={0x49,0xae,0xdb,0x5b,0x2b,0x34,0x86,0x37,0x52,0x60,0x05,0x7e,0x0f,0x27,0x3f,0x3b,0x42,0x30,0xfd,0x6f};
static uintptr_t libraryBase=0;
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
    if(active.load(std::memory_order_relaxed)&&id>0&&source&&libraryBase){
        int device=((int(*)(void*))(libraryBase+0x1cf538))(source);
        if(device==capturedDevice.load(std::memory_order_relaxed)){
            // Pinned ControllerInputSource: last OpenXR trigger-click currentState.
            if(*(uintptr_t*)source==libraryBase+0x3528f0&&!*((unsigned char*)source+0xb38)){
                triggerReleased.store(true,std::memory_order_release);
                originalHit(manager,panels,info,source);return;
            }
            void* panel=((void*(*)(void*,int))(libraryBase+0x1e7cf8))((void*)(libraryBase+0x368e88),id);
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
    return hit&&fabsf(remap(y,500,e.fy)+30)<2;
}
static bool buildMatches(const dl_phdr_info* info){
    for(int i=0;i<info->dlpi_phnum;i++){
        const auto& ph=info->dlpi_phdr[i];if(ph.p_type!=PT_NOTE)continue;
        auto p=(const unsigned char*)(info->dlpi_addr+ph.p_vaddr);
        auto end=p+ph.p_memsz;
        while(p+sizeof(Elf64_Nhdr)<=end){
            auto n=(const Elf64_Nhdr*)p;p+=sizeof(*n);
            size_t names=(n->n_namesz+3)&~size_t(3),desc=(n->n_descsz+3)&~size_t(3);
            if(names>size_t(end-p)||desc>size_t(end-p)-names)break;
            if(n->n_type==NT_GNU_BUILD_ID&&n->n_namesz==4&&!memcmp(p,"GNU",4)
               &&n->n_descsz==sizeof(expectedId)&&!memcmp(p+names,expectedId,sizeof(expectedId)))return true;
            p+=names+desc;
        }
    }return false;
}
static int locate(dl_phdr_info* info,size_t,void*){
    if(!info->dlpi_name||!strstr(info->dlpi_name,"libxrshell.so"))return 0;
    if(!buildMatches(info)){installed=-1;return 1;}
    uintptr_t base=info->dlpi_addr;
    auto hitSlot=(uintptr_t*)(base+0x362a00);
    // Pinned ELF R_AARCH64_JUMP_SLOT for rayHitCylinderLayer, not executable code patching.
    auto slot=(uintptr_t*)(base+0x362a60);
    if(__atomic_load_n(slot,__ATOMIC_ACQUIRE)!=base+0x1fcaa0||__atomic_load_n(hitSlot,__ATOMIC_ACQUIRE)!=base+0x1c4da0){installed=-2;return 1;}
    original=(RayFunction)(base+0x1fcaa0);
    if(!selfTest()){installed=-3;original=nullptr;return 1;}
    long pageSize=sysconf(_SC_PAGESIZE);if(pageSize<=0){installed=-4;return 1;}
    void* page=(void*)((uintptr_t)slot&~(uintptr_t(pageSize)-1));
    if(mprotect(page,pageSize,PROT_READ|PROT_WRITE)){installed=-4;return 1;}
    libraryBase=base;originalHit=(HitFunction)(base+0x1c4da0);
    __atomic_store_n(slot,(uintptr_t)&retry,__ATOMIC_RELEASE);
    __atomic_store_n(hitSlot,(uintptr_t)&hitProxy,__ATOMIC_RELEASE);
    if(mprotect(page,pageSize,PROT_READ)){
        // Restore original dispatch before reporting failure; helper remains resident.
        __atomic_store_n(slot,(uintptr_t)original,__ATOMIC_RELEASE);
        __atomic_store_n(hitSlot,(uintptr_t)originalHit,__ATOMIC_RELEASE);
        mprotect(page,pageSize,PROT_READ);installed=-5;return 1;
    }
    installed=1;return 1;
}
extern "C" JNIEXPORT jint JNICALL Java_research_pico_windowresize_NativeBorder_install(JNIEnv*,jclass){
    if(!installed)dl_iterate_phdr(locate,nullptr);
    return installed;
}
extern "C" JNIEXPORT void JNICALL Java_research_pico_windowresize_NativeBorder_setEnabled(JNIEnv*,jclass,jboolean value){active.store(value==JNI_TRUE,std::memory_order_release);if(!value)capturedId.store(0,std::memory_order_release);}
extern "C" JNIEXPORT jlongArray JNICALL Java_research_pico_windowresize_NativeBorder_stats(JNIEnv* env,jclass){
    jlong data[]={installed,(jlong)attempts.load(),(jlong)successes.load()};
    jlongArray result=env->NewLongArray(3);if(result)env->SetLongArrayRegion(result,0,3,data);return result;
}

// begin/anchor are called through XREngine.queueTask, never on UI/Binder threads.
extern "C" JNIEXPORT jboolean JNICALL Java_research_pico_windowresize_NativeBorder_begin(JNIEnv*,jclass,jint id,jint device,jint w,jint h,jint sx,jint sy){
    if(installed!=1||!active.load()||id<=0||(device!=1&&device!=2)||w<=0||h<=0)return false;
    void* panel=((void*(*)(void*,int))(libraryBase+0x1e7cf8))((void*)(libraryBase+0x368e88),id);
    if(!panel||*(int*)((char*)panel+0x14)!=3002)return false;
    void* layer=*(void**)((char*)panel+0x58);if(!layer)return false;
    if(*(uintptr_t*)layer!=libraryBase+0x3533f0)return false;
    void* transform=*(void**)((char*)layer+8);if(!transform)return false;
    Cylinder c=((Cylinder(*)(void*))(libraryBase+0x1d4e88))(layer);
    if(!std::isfinite(c.radius)||!std::isfinite(c.angle)||!std::isfinite(c.aspect)||c.radius<=0||c.angle<=0||c.aspect<=0)return false;
    Drag next{};next.id=id;next.w=w;next.h=h;next.sx=sx;next.sy=sy;next.initial=c;
    next.position=((Vec3(*)(void*))(libraryBase+0x1ffe30))(transform);
    next.rotation=((Quat(*)(void*))(libraryBase+0x1ffc38))(transform);
    drag=next;anchorUntil=0;targetWidth=w;targetHeight=h;dragSerial=0;capturedDevice=device;triggerReleased=false;
    capturedId.store(id,std::memory_order_release);return true;
}
extern "C" JNIEXPORT void JNICALL Java_research_pico_windowresize_NativeBorder_end(JNIEnv*,jclass){anchorUntil.store(nowNs()+1000000000);capturedId.store(0,std::memory_order_release);}
extern "C" JNIEXPORT jintArray JNICALL Java_research_pico_windowresize_NativeBorder_dragSize(JNIEnv* env,jclass){
    jint data[]={capturedId.load(),targetWidth.load(),targetHeight.load(),(jint)dragSerial.load(std::memory_order_acquire),triggerReleased.load(std::memory_order_acquire)?1:0};
    jintArray result=env->NewIntArray(5);if(result)env->SetIntArrayRegion(result,0,5,data);return result;
}
extern "C" JNIEXPORT void JNICALL Java_research_pico_windowresize_NativeBorder_anchor(JNIEnv*,jclass,jint id){
    if(id!=drag.id||!libraryBase||!active.load()||(capturedId.load()!=id&&nowNs()>anchorUntil.load()))return;
    void* panel=((void*(*)(void*,int))(libraryBase+0x1e7cf8))((void*)(libraryBase+0x368e88),id);
    if(!panel)return;void* layer=*(void**)((char*)panel+0x58);if(!layer)return;
    void* transform=*(void**)((char*)layer+8);if(!transform)return;
    Cylinder c=((Cylinder(*)(void*))(libraryBase+0x1d4e88))(layer);
    if(!std::isfinite(c.radius)||!std::isfinite(c.angle)||!std::isfinite(c.aspect)||c.radius<=0||c.angle<=0||c.aspect<=0)return;
    Quat rotation=resizedRotation(drag,c);
    Vec3 position=anchoredPosition(drag,c,rotation);
    ((void(*)(void*,const Quat&))(libraryBase+0x1ff8a8))(transform,rotation);
    ((void(*)(void*,const Vec3&))(libraryBase+0x1ff5b0))(transform,position);
}
