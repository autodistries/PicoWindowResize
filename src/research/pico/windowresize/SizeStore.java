package research.pico.windowresize;

import android.content.*;
import android.database.Cursor;
import android.net.Uri;
import android.os.*;

/** Size data stays private to this app; only own UID and Android system UID may call. */
public final class SizeStore extends ContentProvider {
    static final Uri URI=Uri.parse("content://research.pico.windowresize.sizes");
    static boolean valid(int w,int h){return w>=480&&w<=2560&&h>=320&&h<=1440;}
    private int boot;
    @Override public boolean onCreate(){boot=android.provider.Settings.Global.getInt(getContext().getContentResolver(),"boot_count",0);return true;}
    @Override public synchronized Bundle call(String method,String pkg,Bundle args){
        int uid=Binder.getCallingUid();
        if(uid!=1000&&uid!=android.os.Process.myUid())throw new SecurityException("System caller required");
        SharedPreferences prefs=getContext().getSharedPreferences("app_sizes",0);
        Bundle result=new Bundle();
        long generation=prefs.getLong("generation",0);
        result.putLong("generation",generation);
        if("all".equals(method)){
            for(java.util.Map.Entry<String,?> entry:prefs.getAll().entrySet())
                if(entry.getValue() instanceof Integer)result.putInt(entry.getKey(),(Integer)entry.getValue());
                else if(entry.getValue() instanceof Float)result.putFloat(entry.getKey(),(Float)entry.getValue());
            return result;
        }
        if("clear".equals(method)){
            if(uid!=android.os.Process.myUid())throw new SecurityException("App caller required");
            if(!prefs.edit().clear().putLong("generation",generation+1).putLong("clearedAt",SystemClock.elapsedRealtime()).putInt("clearBoot",boot).commit())throw new IllegalStateException("Could not clear sizes");
            result.putLong("generation",generation+1);
            getContext().getContentResolver().notifyChange(URI,null);return result;
        }
        if(pkg==null||pkg.length()>255||!pkg.matches("[A-Za-z0-9_]+(\\.[A-Za-z0-9_]+)+"))return result;
        if("save".equals(method)&&args!=null){
            if(!SaveOrder.accept(generation,args.getLong("generation",-1),args.getLong("submitted",0),prefs.getLong("clearedAt",0),boot,prefs.getInt("clearBoot",-1)))return result;
            int w=args.getInt("width"),h=args.getInt("height");
            float scale=args.getFloat("scale",Float.NaN);
            if(valid(w,h)&&Float.isFinite(scale)&&scale>0&&scale<=10){prefs.edit().putInt(pkg+"/w",w).putInt(pkg+"/h",h).putFloat(pkg+"/scale",scale).apply();result.putBoolean("accepted",true);}
        }else if("load".equals(method)){
            int w=prefs.getInt(pkg+"/w",0),h=prefs.getInt(pkg+"/h",0);
            if(valid(w,h)){result.putInt("width",w);result.putInt("height",h);}
        }
        return result;
    }
    @Override public Cursor query(Uri u,String[] p,String s,String[] a,String o){return null;}
    @Override public String getType(Uri u){return null;}
    @Override public Uri insert(Uri u,ContentValues v){throw new UnsupportedOperationException();}
    @Override public int delete(Uri u,String s,String[] a){throw new UnsupportedOperationException();}
    @Override public int update(Uri u,ContentValues v,String s,String[] a){throw new UnsupportedOperationException();}
}
