package research.pico.windowresize;

import android.os.Bundle;
import android.os.Binder;
import java.util.concurrent.ConcurrentHashMap;
import de.robv.android.xposed.*;

/** Existing protected SystemExt provider; requests accepted only from system XRShell. */
final class DisplayResizeBridge {
    static final String METHOD="pico_border_resize_display";
    private static final String BASE="com.bytedance.nativeshell.appmanager.";
    private static final ConcurrentHashMap<Integer,Request> pending=new ConcurrentHashMap<>();
    private static volatile Bundle savedSizes=new Bundle();
    private static android.os.Handler storage;
    private static final java.util.concurrent.atomic.AtomicLong cacheEpoch=new java.util.concurrent.atomic.AtomicLong();
    private static final Object cacheLock=new Object();
    private static void refresh(android.content.Context context){
        long epoch=cacheEpoch.get();
        try{Bundle all=context.getContentResolver().call(SizeStore.URI,"all",null,null);
            synchronized(cacheLock){if(all!=null&&epoch==cacheEpoch.get())savedSizes=all;}}
        catch(Throwable failure){XposedBridge.log("PICO Border Resize: async size load failed "+failure);}
    }
    private static final class Request {int display,width,height,panel;boolean remember;long generation,submitted;}
    static void install(ClassLoader cl){
        final XSharedPreferences prefs=new XSharedPreferences("research.pico.windowresize","resize");
        android.os.HandlerThread thread=new android.os.HandlerThread("PicoResizeStorage");thread.start();
        storage=new android.os.Handler(thread.getLooper());
        XposedHelpers.findAndHookMethod(BASE+"AppManagerService",cl,"setContext",android.content.Context.class,new XC_MethodHook(){
            @Override protected void afterHookedMethod(MethodHookParam p){
                final android.content.Context context=((android.content.Context)p.args[0]).getApplicationContext();
                storage.post(()->{
                    try{context.getContentResolver().registerContentObserver(SizeStore.URI,false,new android.database.ContentObserver(null){
                        @Override public void onChange(boolean selfChange){
                            synchronized(cacheLock){cacheEpoch.incrementAndGet();savedSizes=new Bundle();}storage.post(()->refresh(context));
                        }
                    });}catch(Throwable failure){XposedBridge.log("PICO Border Resize: cache observer unavailable "+failure);}
                });
                storage.postDelayed(new Runnable(){public void run(){
                    refresh(context);
                    storage.postDelayed(this,5000);
                }},5000);
            }
        });
        XposedHelpers.findAndHookMethod(BASE+"AppContainer",cl,"createVirtualDisplay",String.class,int.class,int.class,int.class,int.class,boolean.class,new XC_MethodHook(){
            @Override protected void beforeHookedMethod(MethodHookParam p){
                try{
                    if(!XposedHelpers.findClass(BASE+"AppRecord",cl).isInstance(p.thisObject)||XposedHelpers.getIntField(p.thisObject,"mType")!=3002)return;
                    prefs.reload();if(!prefs.getBoolean("enabled",false)||!prefs.getBoolean("remember",true))return;
                    android.content.ComponentName component=(android.content.ComponentName)XposedHelpers.getObjectField(p.thisObject,"mComponentName");
                    Bundle size=savedSizes;String pkg=component.getPackageName();
                    int w=size.getInt(pkg+"/w"),h=size.getInt(pkg+"/h");
                    if(!SizeStore.valid(w,h))return;
                    float scale=size.getFloat(pkg+"/scale",Float.NaN);
                    if(!Float.isFinite(scale)||scale<=0||scale>10){
                        // Legacy dimension-only records: retain stock scale from original dimensions.
                        boolean fixed=XposedHelpers.getBooleanField(p.thisObject,"mHasWindowLayout")||XposedHelpers.getBooleanField(p.thisObject,"mIsSystem");
                        scale=fixed?1f:((Number)XposedHelpers.callStaticMethod(XposedHelpers.findClass(BASE+"AppRecord",cl),"calculateScale",(int)p.args[1],(int)p.args[2],XposedHelpers.getBooleanField(p.thisObject,"mIsPortrait"))).floatValue();
                    }
                    XposedHelpers.setAdditionalInstanceField(p.thisObject,"picoRestoredScale",scale);
                    p.args[1]=w;p.args[2]=h;
                    XposedHelpers.setIntField(p.thisObject,"mWidth",w);XposedHelpers.setIntField(p.thisObject,"mHeight",h);
                    XposedBridge.log("PICO Border Resize: restored "+component.getPackageName()+" "+w+"x"+h);
                }catch(Throwable failure){XposedBridge.log("PICO Border Resize: restore size unavailable "+failure);}
            }
        });
        // AppRecord calculates its scale immediately before calling this superclass method.
        XposedHelpers.findAndHookMethod(BASE+"AppContainer",cl,"requestAllocateSurface",new XC_MethodHook(){
            @Override protected void beforeHookedMethod(MethodHookParam p){
                Object scale=XposedHelpers.getAdditionalInstanceField(p.thisObject,"picoRestoredScale");
                if(scale instanceof Float)XposedHelpers.setFloatField(p.thisObject,"mScale",(Float)scale);
            }
        });
        XposedHelpers.findAndHookMethod(BASE+"AppRecord",cl,"resizeVirtualDisplay",boolean.class,new XC_MethodHook(){
            @Override protected void beforeHookedMethod(MethodHookParam p){XposedHelpers.removeAdditionalInstanceField(p.thisObject,"picoRestoredScale");}
        });
        XposedHelpers.findAndHookMethod(BASE+"InterfaceProvider",cl,"handleMethod",String.class,String.class,Bundle.class,String.class,new XC_MethodHook(){
            @Override protected void beforeHookedMethod(MethodHookParam p){
                if(!METHOD.equals(p.args[0]))return;
                Bundle result=new Bundle();p.setResult(result);
                if(Binder.getCallingUid()!=1000||!"com.picoxr.xrshell".equals(p.args[3]))return;
                prefs.reload();if(!prefs.getBoolean("enabled",false))return;
                Bundle args=(Bundle)p.args[2];if(args==null)return;
                if(args.getBoolean("ping",false)){result.putBoolean("accepted",true);return;}
                Request r=new Request();r.display=args.getInt("displayId");r.panel=args.getInt("panelId");r.width=args.getInt("width");r.height=args.getInt("height");
                if(r.display<=0||r.panel<=0||r.width<480||r.width>2560||r.height<320||r.height>1440)return;
                r.remember=prefs.getBoolean("remember",true);
                r.generation=savedSizes.getLong("generation",-1);
                r.submitted=args.getLong("submitted",0);
                Request previous=pending.put(r.display,r);
                if(previous==null)XposedHelpers.callStaticMethod(XposedHelpers.findClass(BASE+"util.UiThread",cl),"post",(Runnable)()->apply(cl,r.display));
                result.putBoolean("accepted",true);
            }
        });
        XposedBridge.log("PICO Border Resize: SystemExt real-display resize bridge installed");
    }
    private static void apply(ClassLoader cl,int display){
        Request r=pending.remove(display);if(r==null)return;
        try {
            Object service=XposedHelpers.callStaticMethod(XposedHelpers.findClass(BASE+"AppManagerService",cl),"getInstance");
            Object root=XposedHelpers.callMethod(service,"getRootContainer");
            Object record=XposedHelpers.callMethod(root,"resolveContainerByDisplayIdLocked",display);
            if(record==null||!XposedHelpers.findClass(BASE+"AppRecord",cl).isInstance(record)
                    ||XposedHelpers.getIntField(record,"mType")!=3002
                    ||XposedHelpers.getIntField(record,"mClientId")!=r.panel
                    ||XposedHelpers.getObjectField(record,"mVirtualDisplay")==null)return;
            int oldW=XposedHelpers.getIntField(record,"mWidth"),oldH=XposedHelpers.getIntField(record,"mHeight");
            if(oldW==r.width&&oldH==r.height)return;
            // Stock resize() omits these assignments; its DataModule update needs current values.
            XposedHelpers.setIntField(record,"mWidth",r.width);XposedHelpers.setIntField(record,"mHeight",r.height);
            try {XposedHelpers.callMethod(record,"resize",r.width,r.height);}
            catch(Throwable failure){
                XposedHelpers.setIntField(record,"mWidth",oldW);XposedHelpers.setIntField(record,"mHeight",oldH);
                try {XposedHelpers.callMethod(record,"resize",oldW,oldH);}catch(Throwable rollback){XposedBridge.log("PICO Border Resize: rollback failed "+rollback);}
                throw failure;
            }
            try{
                if(r.remember){
                    android.content.Context context=(android.content.Context)XposedHelpers.getObjectField(record,"mContext");
                    android.content.ComponentName component=(android.content.ComponentName)XposedHelpers.getObjectField(record,"mComponentName");
                    Bundle size=new Bundle();size.putInt("width",r.width);size.putInt("height",r.height);
                    size.putFloat("scale",XposedHelpers.getFloatField(record,"mScale"));
                    size.putLong("generation",r.generation);
                    size.putLong("submitted",r.submitted);
                    storage.post(()->{try{context.getContentResolver().call(SizeStore.URI,"save",component.getPackageName(),size);
                        refresh(context);
                    }catch(Throwable failure){XposedBridge.log("PICO Border Resize: async save failed "+failure);}});
                }
            }catch(Throwable failure){XposedBridge.log("PICO Border Resize: save size failed "+failure);}
        }catch(Throwable failure){XposedBridge.log("PICO Border Resize: display resize failed "+failure);}
    }
}
