package research.pico.windowresize;
import android.app.Activity;
import android.os.Bundle;
import android.content.SharedPreferences;
import android.widget.*;

public final class MainActivity extends Activity {
    private TextView compatibility;
    private final android.os.Handler refreshTimer=new android.os.Handler(android.os.Looper.getMainLooper());
    private final java.util.concurrent.ExecutorService statusWorker=java.util.concurrent.Executors.newSingleThreadExecutor();
    private final java.util.concurrent.atomic.AtomicBoolean refreshing=new java.util.concurrent.atomic.AtomicBoolean();
    private String binaryStatus;
    private final Runnable refreshTick=new Runnable(){public void run(){refreshStatus();refreshTimer.postDelayed(this,5000);}};
    private void refreshStatus(){
        if(!refreshing.compareAndSet(false,true))return;
        statusWorker.execute(()->{String text;
            try{
                if(binaryStatus==null)binaryStatus=Compatibility.binary(this);
                Bundle status=getContentResolver().call(SizeStore.URI,"status",null,null);
                text="Compatibility checks\n\n"+binaryStatus+"\n"+Compatibility.live(status)
                    +"\nKnown baseline: PICO 4 / Phoenix, OS 5.13.3 / 5.13.7 / 5.13.8. A different binary may pass live contracts; PASS covers checked hooks, not every possible device behavior.";
            }catch(Throwable failure){text="Compatibility checks unavailable: "+failure;}
            final String result=text;
            refreshing.set(false);runOnUiThread(()->{if(!isDestroyed())compatibility.setText(result);});
        });
    }
    @Override protected void onResume(){super.onResume();refreshTimer.post(refreshTick);}
    @Override protected void onPause(){refreshTimer.removeCallbacks(refreshTick);super.onPause();}
    @Override protected void onDestroy(){refreshTimer.removeCallbacks(refreshTick);statusWorker.shutdownNow();super.onDestroy();}
    @Override public void dump(String prefix,java.io.FileDescriptor fd,java.io.PrintWriter out,String[] args){
        super.dump(prefix,fd,out,args);
        if(compatibility!=null)out.println(prefix+compatibility.getText());
    }
    public boolean isModuleActive() { return false; }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this); layout.setOrientation(1); layout.setPadding(28,28,28,28);
        TextView info = new TextView(this);
        info.setText("PICO Border Resize — "+Version.NAME+"\n\nSelect this app and com.picoxr.xrshell + com.picovr.systemext in LSPosed scope, then reboot. Native capture stays on grabbed panel until trigger release.\n\nHold trigger just outside a border, then move outward/inward. Drag an edge to change width or height; corners change both. Android app content reflows. Opposite edge anchored. Last resized size can be remembered per app and restored on reopening.\n\nNew toggle defaults OFF. Disable to restore original touch bounds; existing size stays until panel closes; saved sizes remain available.\n\nModule active: " + isModuleActive());
        layout.addView(info);
        Switch toggle = new Switch(this); toggle.setText("Enable outside-border resizing"); layout.addView(toggle);
        try {
            SharedPreferences prefs = getSharedPreferences("resize", MODE_WORLD_READABLE);
            toggle.setChecked(prefs.getBoolean("enabled",false));
            Switch remember=new Switch(this);remember.setText("Remember window size per app");layout.addView(remember);
            remember.setChecked(prefs.getBoolean("remember",true));
            remember.setOnCheckedChangeListener((view,checked)->prefs.edit().putBoolean("remember",checked).commit());
            Button clear=new Button(this);clear.setText("Forget all saved window sizes");layout.addView(clear);
            clear.setOnClickListener(view->{getContentResolver().call(SizeStore.URI,"clear",null,null);Toast.makeText(this,"Saved sizes cleared. Existing windows keep their size.",Toast.LENGTH_LONG).show();});
            toggle.setOnCheckedChangeListener((view, checked) -> prefs.edit().putBoolean("enabled",checked).commit());
        } catch (SecurityException failure) {
            toggle.setEnabled(false); info.append("\nEnable module + self scope and restart this app to unlock settings.");
        }
        android.view.View divider=new android.view.View(this);divider.setBackgroundColor(0xff777777);
        LinearLayout.LayoutParams line=new LinearLayout.LayoutParams(-1,1);line.setMargins(0,24,0,24);layout.addView(divider,line);
        compatibility=new TextView(this);compatibility.setText("Checking compatibility…");compatibility.setTextIsSelectable(true);layout.addView(compatibility);
        Button refresh=new Button(this);refresh.setText("Refresh compatibility status");refresh.setOnClickListener(view->refreshStatus());layout.addView(refresh);
        ScrollView scroll=new ScrollView(this);scroll.addView(layout);setContentView(scroll);
    }
}
