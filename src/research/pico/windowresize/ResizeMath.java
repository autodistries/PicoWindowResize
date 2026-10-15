package research.pico.windowresize;
/** Start a drag only outside content, and select independent edge axes. */
public final class ResizeMath {
    public static boolean border(float x,float y,int w,int h) {
        return w>0 && h>0 && Float.isFinite(x) && Float.isFinite(y)
            && x>=-48 && y>=-48 && x<=w+48 && y<=h+48
            && (x<0 || y<0 || x>w || y>h);
    }
    public static int[] edges(float x,float y,int w,int h){
        if(!border(x,y,w,h))return new int[]{0,0};
        return new int[]{x<0?-1:x>w?1:0,y<0?-1:y>h?1:0};
    }
}
