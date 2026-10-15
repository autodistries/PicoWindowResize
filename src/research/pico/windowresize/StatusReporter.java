package research.pico.windowresize;

import android.app.Application;
import android.os.*;
import java.util.concurrent.atomic.AtomicBoolean;
import de.robv.android.xposed.XposedHelpers;

/** Never perform cross-process diagnostics calls on compositor/input threads. */
final class StatusReporter {
    private final Handler worker;
    private final AtomicBoolean pending=new AtomicBoolean();
    StatusReporter(){HandlerThread thread=new HandlerThread("PicoResizeStatus");thread.start();worker=new Handler(thread.getLooper());}
    void report(Bundle status){
        if(!pending.compareAndSet(false,true))return;
        worker.post(()->{try{
            Application app=(Application)XposedHelpers.callStaticMethod(XposedHelpers.findClass("android.app.ActivityThread",null),"currentApplication");
            if(app!=null)app.getContentResolver().call(SizeStore.URI,"reportStatus",null,status);
        }catch(Throwable ignored){/* App may be updating; next heartbeat retries. */}
        finally{pending.set(false);}});
    }
}
