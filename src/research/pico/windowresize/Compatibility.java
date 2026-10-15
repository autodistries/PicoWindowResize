package research.pico.windowresize;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.os.Bundle;
import android.os.SystemClock;
import java.security.MessageDigest;
import java.util.zip.ZipFile;
import java.util.zip.ZipEntry;
import java.io.InputStream;

final class Compatibility {
    static final long SHELL_VERSION=100001366L;
    static final String ELF_SHA="89ab1a63c3b11d2a3f7746725440cfeda05d7579535287b5203eff5f83430c5d";
    static final String BUILD_ID="49aedb5b2b3486375260057e0f273f3b4230fd6f";
    static String binary(Context context){
        try{
            PackageInfo shell=context.getPackageManager().getPackageInfo("com.picoxr.xrshell",0);
            StringBuilder out=new StringBuilder("XRShell: ").append(shell.versionName).append(" (").append(shell.getLongVersionCode()).append(")\n");
            out.append("Expected version: 1.0.0 (").append(SHELL_VERSION).append(") — ")
                .append(shell.getLongVersionCode()==SHELL_VERSION&&"1.0.0".equals(shell.versionName)?"MATCH":"DIFFERENT").append('\n');
            try(ZipFile apk=new ZipFile(shell.applicationInfo.sourceDir)){
                ZipEntry entry=apk.getEntry("lib/arm64-v8a/libxrshell.so");
                if(entry==null)return out.append("ELF binary: unavailable in APK\n").toString();
                MessageDigest hash=MessageDigest.getInstance("SHA-256");
                try(InputStream input=apk.getInputStream(entry)){byte[] buffer=new byte[65536];int size;while((size=input.read(buffer))!=-1)hash.update(buffer,0,size);}
                StringBuilder hex=new StringBuilder();for(byte b:hash.digest())hex.append(String.format(java.util.Locale.ROOT,"%02x",b&255));
                out.append("Installed ELF binary: ").append(ELF_SHA.contentEquals(hex)?"MATCH":"DIFFERENT").append("\nSHA-256: ").append(hex).append('\n');
            }
            return out.toString();
        }catch(Exception failure){return "XRShell binary check unavailable: "+failure+"\n";}
    }
    static String live(Bundle status){
        if(status==null||!status.containsKey("received"))return "Live checks: no XRShell report yet. Enable XRShell scope and reboot, then refresh.\n";
        long age=Math.max(0,SystemClock.elapsedRealtime()-status.getLong("received"));
        boolean fresh=age<=15000,version=Version.NAME.equals(status.getString("moduleVersion"));
        StringBuilder out=new StringBuilder("Live report: ").append(fresh?"CURRENT":"STALE — do not rely on these results").append(" (").append(age/1000).append("s old)\n");
        out.append("Module loaded in XRShell: ").append(status.getString("moduleVersion","unknown"));
        if(!version)out.append(" — restart headset to load ").append(Version.NAME);
        out.append("\nJava shell hooks: ").append(status.getBoolean("javaReady")?"PASS":"NOT PASSED").append('\n');
        String nativeInfo=status.getString("native","Native checks unavailable\n");
        if(nativeInfo.contains("Loaded ELF build ID: "+BUILD_ID+"\n"))out.append("Loaded build ID: MATCH\n");
        else out.append("Loaded build ID: DIFFERENT / UNAVAILABLE\n");
        out.append(nativeInfo);
        boolean ready=fresh&&version&&status.getBoolean("javaReady")&&status.getInt("nativeState",0)==1&&!status.getBoolean("broken");
        out.append("Compatibility result: ").append(ready?"PASS — all checked targets accepted":"NOT READY").append('\n');
        out.append("Border resizing: ").append(status.getBoolean("enabled")?"ON":"OFF").append('\n');
        String error=status.getString("error","");if(!error.isEmpty())out.append("Error: ").append(error).append('\n');
        return out.toString();
    }
}
