package research.pico.windowresize;

import android.app.Application;
import android.net.Uri;
import android.os.*;
import de.robv.android.xposed.*;

/** One worker, latest size per window. A stalled Binder call cannot hold input locks. */
final class ResizeClient {
    interface Reply {void finished(boolean accepted);}
    private static final class Request {
        final Bundle args;final Reply reply;
        Request(Bundle args,Reply reply){this.args=args;this.reply=reply;}
    }
    private final LatestRequests<Request> pending=new LatestRequests<>();
    private final Handler worker;
    private final java.util.concurrent.atomic.AtomicBoolean scheduled=new java.util.concurrent.atomic.AtomicBoolean();
    ResizeClient(){HandlerThread t=new HandlerThread("PicoResizeRequests");t.start();worker=new Handler(t.getLooper());}
    void submit(int display,int panel,int width,int height,Reply reply){
        Bundle args=new Bundle();args.putInt("displayId",display);args.putInt("panelId",panel);args.putInt("width",width);args.putInt("height",height);args.putLong("submitted",SystemClock.elapsedRealtime());
        pending.put(display,panel,new Request(args,reply));schedule();
    }
    private void schedule(){if(scheduled.compareAndSet(false,true))worker.post(this::drain);}
    private void drain(){
        try{
            Request r;
            while((r=pending.poll())!=null){
                boolean accepted=false;
                try{
                    Application app=(Application)XposedHelpers.callStaticMethod(XposedHelpers.findClass("android.app.ActivityThread",null),"currentApplication");
                    if(app!=null){Bundle result=app.getContentResolver().call(Uri.parse("content://com.picovr.systemext.interface"),DisplayResizeBridge.METHOD,null,r.args);accepted=result!=null&&result.getBoolean("accepted",false);}
                }catch(Throwable failure){XposedBridge.log("PICO Border Resize: async request failed "+failure);}
                r.reply.finished(accepted);
            }
        }finally{scheduled.set(false);if(!pending.isEmpty())schedule();}
    }
}
