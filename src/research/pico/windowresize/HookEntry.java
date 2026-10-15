package research.pico.windowresize;

import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import java.util.*;
import de.robv.android.xposed.*;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

/** Phoenix 5.13.7 only. XRShell and SystemExt hooks; native patches last for the process. */
public final class HookEntry implements IXposedHookLoadPackage, IXposedHookZygoteInit {
    private static String modulePath;
    @Override public void initZygote(StartupParam startup){ modulePath=startup.modulePath; }
    private boolean nativeLoaded;
    private int nativeState;
    private static final String ENGINE="com.picoxr.xrshell.engine.XREngine";
    private static final String PROVIDER="com.picoxr.xrshell.event.EventWindowProvider";
    private static final String EVENT="com.picoxr.xrshell.event.EventDispatcher$EventData";
    private final Map<Integer,Panel> panels=new java.util.concurrent.ConcurrentHashMap<>();
    private XSharedPreferences prefs;
    private Class<?> engineClass;
    private Class<?> providerClass;
    private long motionCount, outsideCount, pressCount, captureCount;
    private String lastPress="none";
    private boolean enabled, ownRange, broken;
    private Capture capture;
    private Handler timer;
    private ResizeClient resizeClient;
    private final Runnable dragTick=new Runnable(){
        @Override public void run(){synchronized(HookEntry.this){
            try{if(capture!=null&&enabled)pollDrag(false);}catch(Throwable failure){
                XposedBridge.log("PICO Border Resize: drag stopped "+failure);finishCapture();
            }
            if(capture!=null)timer.postDelayed(this,32);
        }}
    };
    private static final class Panel {
        int id; volatile int w,h; volatile boolean expanded; volatile int[] range;
    }
    private static final class Capture {
        Panel panel; int device,display,sx,sy,lastW,lastH; long last,write,started;
    }
    @Override public void handleLoadPackage(XC_LoadPackage.LoadPackageParam load) {
        if (load.packageName.equals("research.pico.windowresize")) {
            XposedHelpers.findAndHookMethod(MainActivity.class.getName(),load.classLoader,"isModuleActive",new XC_MethodReplacement(){
                @Override protected Object replaceHookedMethod(MethodHookParam p){return true;}});
            return;
        }
        if(load.packageName.equals("com.picovr.systemext")&&load.isFirstApplication){
            try{DisplayResizeBridge.install(load.classLoader);}catch(Throwable failure){XposedBridge.log("PICO Border Resize: SystemExt bridge failed "+failure);}return;
        }
        if (!load.packageName.equals("com.picoxr.xrshell") || !load.isFirstApplication) return;
        try { install(load.classLoader); XposedBridge.log("PICO Border Resize: hooks installed; default OFF"); }
        catch(Throwable t){ broken=true; XposedBridge.log("PICO Border Resize: disabled, incompatible firmware: "+t); }
    }
    private Object engine(){return XposedHelpers.callStaticMethod(engineClass,"getInstance");}
    private void install(ClassLoader cl) {
        engineClass=XposedHelpers.findClass(ENGINE,cl);
        providerClass=XposedHelpers.findClass(PROVIDER,cl);
        Class<?> data=XposedHelpers.findClass(EVENT,cl);
        // Verify critical methods before registering any hooks.
        XposedHelpers.findMethodExact(engineClass,"dumpPanelInfo");
        XposedHelpers.findMethodExact(engineClass,"setWindowTouchRange",int.class,int.class,int.class,int.class,int.class);
        XposedHelpers.findMethodExact(data,"recycle");
        if(modulePath==null)throw new IllegalStateException("Missing module APK path");
        System.load(modulePath+"!/lib/arm64-v8a/libborderresize.so");
        nativeLoaded=true;
        resizeClient=new ResizeClient();
        prefs=new XSharedPreferences("research.pico.windowresize","resize");
        // Hook JNI boundary: optimized Java wrappers and tiny provider methods can be inlined.
        // Engine callbacks must not take the dispatch lock (dumpPanelInfo waits on engine).
        XposedHelpers.findAndHookMethod(engineClass,"nativeCreatePanel",int.class,int.class,int.class,int.class,int.class,int.class,new XC_MethodHook(){
            @Override protected void afterHookedMethod(MethodHookParam p){
                int id=(int)p.args[0]; panels.remove(id);
                XposedBridge.log("PICO Border Resize: native create id="+id+" type="+p.args[1]+" shape="+p.args[5]);
                if ((int)p.args[1]==3002 && (int)p.args[5]==0){Panel panel=new Panel();panel.id=id;panels.put(id,panel);}
            } });
        XposedHelpers.findAndHookMethod(engineClass,"nativeCreatePanelSurface",int.class,int.class,int.class,float.class,new XC_MethodHook(){
            @Override protected void afterHookedMethod(MethodHookParam p){
                Panel panel=panels.get((int)p.args[0]); if(panel==null||p.getResult()==null)return;
                panel.w=(int)p.args[1];panel.h=(int)p.args[2];
                if(panel.range==null)panel.range=new int[]{0,0,panel.w,panel.h};
            } });
        XposedHelpers.findAndHookMethod(PROVIDER,cl,"updateWindow",int.class,int.class,int.class,int.class,new XC_MethodHook(){
            @Override protected void afterHookedMethod(MethodHookParam p){ synchronized(HookEntry.this){
                Panel panel=panels.get((int)p.args[0]); if(panel==null)return;
                final int anchorId=panel.id;
                XposedHelpers.callMethod(engine(),"queueTask",(Runnable)()->NativeBorder.anchor(anchorId));
                panel.w=(int)p.args[2]; panel.h=(int)p.args[3];
                panel.range=new int[]{0,0,panel.w,panel.h}; panel.expanded=false; applyRange(panel);
            }} });
        XposedHelpers.findAndHookMethod(engineClass,"setWindowTouchRange",int.class,int.class,int.class,int.class,int.class,new XC_MethodHook(){
            @Override protected void beforeHookedMethod(MethodHookParam p){ synchronized(HookEntry.this){
                if(ownRange)return; Panel panel=panels.get((int)p.args[0]); if(panel==null)return;
                panel.range=new int[]{(int)p.args[1],(int)p.args[2],(int)p.args[3],(int)p.args[4]};
                panel.expanded=enabled&&!broken&&panel.w>0&&panel.h>0;
                if(panel.expanded){p.args[1]=-48;p.args[2]=-48;p.args[3]=panel.w+48;p.args[4]=panel.h+48;}
            }} });
        XposedHelpers.findAndHookMethod("com.picoxr.xrshell.event.EventDispatcher",cl,"dispatchEvent",data,new XC_MethodHook(){
            @Override protected void beforeHookedMethod(MethodHookParam p){ synchronized(HookEntry.this){
                Object event=p.args[0]; if(event==null)return;
                try { if(consume(event)){XposedHelpers.callMethod(event,"recycle");p.setResult(true);} }
                catch(Throwable t){broken=true;finishCapture();XposedBridge.log("PICO Border Resize: stopped after error "+t);}
            }} });
        XposedHelpers.findAndHookMethod(PROVIDER,cl,"removeWindow",int.class,new XC_MethodHook(){
            @Override protected void beforeHookedMethod(MethodHookParam p){ synchronized(HookEntry.this){
                Panel panel=panels.remove((int)p.args[0]);if(capture!=null&&capture.panel==panel)finishCapture();
            }} });
        XposedHelpers.findAndHookMethod(PROVIDER,cl,"updateWindowVisible",int.class,boolean.class,new XC_MethodHook(){
            @Override protected void beforeHookedMethod(MethodHookParam p){ synchronized(HookEntry.this){
                if(!(boolean)p.args[1]&&capture!=null&&capture.panel.id==(int)p.args[0])finishCapture();
            }} });
        XposedHelpers.findAndHookMethod("com.picoxr.xrshell.XRShellProvider",cl,"dump",
                java.io.FileDescriptor.class,java.io.PrintWriter.class,String[].class,new XC_MethodHook(){
            @Override protected void afterHookedMethod(MethodHookParam p){synchronized(HookEntry.this){
                java.io.PrintWriter out=(java.io.PrintWriter)p.args[1];
                out.println("BorderResize "+Version.NAME+" enabled="+enabled+" broken="+broken+" prefs="+prefs.getFile());
                if(nativeLoaded){out.println("nativeStatus,attempts,hits="+Arrays.toString(NativeBorder.stats()));
                    out.println("nativeDrag panel,width,height,serial="+Arrays.toString(NativeBorder.dragSize()));}
                if(capture!=null)out.println("resize capture display="+capture.display+" lastRequested="+capture.lastW+"x"+capture.lastH);
                out.println("motion="+motionCount+" outside="+outsideCount+" presses="+pressCount+" captures="+captureCount+" lastPress="+lastPress);
                for(Panel panel:panels.values())out.println("resizePanel="+panel.id+" size="+panel.w+"x"+panel.h+" expanded="+panel.expanded+" original="+Arrays.toString(panel.range));
            }} });
        timer=new Handler(Looper.getMainLooper());
        timer.post(new Runnable(){public void run(){ synchronized(HookEntry.this){
            try {if(nativeState==0){nativeState=NativeBorder.install();
                    if(nativeState!=0)XposedBridge.log("PICO Border Resize: native status="+nativeState);
                    if(nativeState<0)broken=true;
                }
                prefs.reload();boolean next=!broken&&nativeState==1&&prefs.getBoolean("enabled",false);
                if(next!=enabled)XposedBridge.log("PICO Border Resize: enabled="+next+" panels="+panels.size());
                if(next!=enabled){enabled=next;NativeBorder.setEnabled(next);}
                Object provider=XposedHelpers.callStaticMethod(providerClass,"getInstance");
                for(Panel panel:panels.values()){
                    if(!enabled&&!panel.expanded)continue;
                    Object window=XposedHelpers.callMethod(provider,"getWindow",panel.id);
                    if(window!=null){int w=XposedHelpers.getIntField(window,"mWidth"),h=XposedHelpers.getIntField(window,"mHeight");
                        if(w>0&&h>0&&(w!=panel.w||h!=panel.h)){
                            panel.w=w;panel.h=h;panel.range=new int[]{0,0,w,h};panel.expanded=false;
                            final int anchorId=panel.id;XposedHelpers.callMethod(engine(),"queueTask",(Runnable)()->NativeBorder.anchor(anchorId));
                        }
                    }
                    applyRange(panel);
                }
                if(capture!=null){
                    long now=SystemClock.uptimeMillis();
                    if(!enabled||now-capture.last>10000||now-capture.started>60000)finishCapture();
                }
            } catch(Throwable t){enabled=false;if(nativeLoaded)NativeBorder.setEnabled(false);XposedBridge.log("PICO Border Resize preferences/range: "+t);}
        }timer.postDelayed(this,500);}});
    }
    private void applyRange(Panel panel){
        if(panel.range==null||panel.w<=0||panel.h<=0)return;
        boolean desired=enabled&&!broken;
        if(panel.expanded==desired)return;
        ownRange=true;
        try {int[] r=desired?new int[]{-48,-48,panel.w+48,panel.h+48}:panel.range;
            XposedHelpers.callMethod(engine(),"setWindowTouchRange",panel.id,r[0],r[1],r[2],r[3]);panel.expanded=desired;
            XposedBridge.log("PICO Border Resize: range id="+panel.id+" expanded="+desired+" bounds="+Arrays.toString(r));
        } finally {ownRange=false;}
    }
    private static int i(Object event,String field){return XposedHelpers.getIntField(event,field);}
    private void requestSize(Capture c,int w,int h){
        c.lastW=w;c.lastH=h;c.write=SystemClock.uptimeMillis();
        resizeClient.submit(c.display,c.panel.id,w,h,accepted->{
            if(!accepted)synchronized(HookEntry.this){
                // Replies from an ended/replaced capture must not mutate its successor.
                if(capture==c&&c.lastW==w&&c.lastH==h){c.lastW=-1;c.lastH=-1;}
            }
        });
    }
    private void pollDrag(boolean force){
        Capture c=capture;if(c==null)return;
        int[] size=NativeBorder.dragSize();
        if(size==null||size.length<4)return;
        if(size[0]!=c.panel.id){if(!force)finishCapture();return;}
        if(!force&&size.length>=5&&size[4]!=0){finishCapture();return;}
        long now=SystemClock.uptimeMillis();
        if((force||now-c.write>=120)&&(size[1]!=c.lastW||size[2]!=c.lastH)){
            requestSize(c,size[1],size[2]);
        }
    }
    private void finishCapture(){
        if(capture!=null){try{pollDrag(true);}catch(Throwable failure){XposedBridge.log("PICO Border Resize: final resize failed "+failure);}}
        capture=null;if(nativeLoaded)NativeBorder.end();
        if(timer!=null)timer.removeCallbacks(dragTick);
    }
    private boolean consume(Object e) {
        if(!XposedHelpers.getBooleanField(e,"isMotionEvent"))return false;
        int action=i(e,"action"),device=i(e,"deviceId"),button=i(e,"buttonId"),id=i(e,"clientId");
        long now=SystemClock.uptimeMillis();
        motionCount++;
        if(capture!=null&&capture.device==device){
            if((action==1||action==-1000)&&button==10004){finishCapture();return true;}
            if(action==0&&button==10004){finishCapture();}
            else if(action>=0||action==-1000){
                // Native loss-of-hit CANCEL/HOVER_EXIT must not release an owned trigger drag.
                capture.last=now;return true;
            }
        }
        if(action<0)return false;
        Panel observed=panels.get(id);
        if(observed!=null&&ResizeMath.border(XposedHelpers.getFloatField(e,"x"),XposedHelpers.getFloatField(e,"y"),observed.w,observed.h))outsideCount++;
        if(action==0){pressCount++;lastPress="id="+id+" device="+device+" button="+button+" x="+XposedHelpers.getFloatField(e,"x")+" y="+XposedHelpers.getFloatField(e,"y");}
        if(!enabled||broken||capture!=null||action!=0||button!=10004||(device!=1&&device!=2))return false;
        Panel panel=panels.get(id);if(panel==null||!panel.expanded)return false;
        float x=XposedHelpers.getFloatField(e,"x"),y=XposedHelpers.getFloatField(e,"y");
        if(!ResizeMath.border(x,y,panel.w,panel.h))return false;
        Capture c=new Capture();c.panel=panel;c.device=device;c.last=now;c.started=now;
        int[] edges=ResizeMath.edges(x,y,panel.w,panel.h);c.sx=edges[0];c.sy=edges[1];c.lastW=panel.w;c.lastH=panel.h;
        Object window=XposedHelpers.callMethod(XposedHelpers.callStaticMethod(providerClass,"getInstance"),"getWindow",id);
        c.display=XposedHelpers.getIntField(window,"mDisplayId");if(c.display<=0)return false;
        java.util.concurrent.CountDownLatch ready=new java.util.concurrent.CountDownLatch(1);
        java.util.concurrent.atomic.AtomicBoolean started=new java.util.concurrent.atomic.AtomicBoolean(),cancelled=new java.util.concurrent.atomic.AtomicBoolean();
        XposedHelpers.callMethod(engine(),"queueTask",(Runnable)()->{try{
            if(!cancelled.get())started.set(NativeBorder.begin(id,device,panel.w,panel.h,c.sx,c.sy));
            if(cancelled.get())NativeBorder.end();
        }finally{ready.countDown();}});
        try{if(!ready.await(1,java.util.concurrent.TimeUnit.SECONDS)||!started.get()){cancelled.set(true);NativeBorder.end();return false;}}
        catch(InterruptedException failure){Thread.currentThread().interrupt();cancelled.set(true);NativeBorder.end();return false;}
        capture=c;captureCount++;
        timer.post(dragTick);
        XposedBridge.log("PICO Border Resize: capture panel="+id+" device="+device);
        return true;
    }
}
