package research.pico.windowresize;
final class NativeBorder {
    static native int install();
    static native String diagnostics();
    static native void setEnabled(boolean enabled);
    static native long[] stats();
    static native boolean begin(int panel,int device,int width,int height,int sx,int sy);
    static native void end();
    static native int[] dragSize();
    static native void anchor(int panel);
}
