package research.pico.windowresize;
import android.app.Activity;
import android.os.Bundle;
import android.content.SharedPreferences;
import android.widget.*;

public final class MainActivity extends Activity {
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
        setContentView(layout);
    }
}
