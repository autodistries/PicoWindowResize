package research.pico.windowresize;
public class ResizeTests {
 static int count;
 static void check(boolean b){count++;if(!b)throw new AssertionError("check "+count);}
 public static void main(String[] args){
 check(ResizeMath.border(-10,100,1000,500));
 check(ResizeMath.border(1010,100,1000,500));
 check(!ResizeMath.border(500,200,1000,500));
 check(!ResizeMath.border(-49,200,1000,500));
 check(!ResizeMath.border(Float.NaN,0,1000,500));
 check(!ResizeMath.border(0,0,0,500));
 check(java.util.Arrays.equals(ResizeMath.edges(-10,100,1000,500),new int[]{-1,0}));
 check(java.util.Arrays.equals(ResizeMath.edges(1010,100,1000,500),new int[]{1,0}));
 check(java.util.Arrays.equals(ResizeMath.edges(500,-10,1000,500),new int[]{0,-1}));
 check(java.util.Arrays.equals(ResizeMath.edges(1010,510,1000,500),new int[]{1,1}));
 check(java.util.Arrays.equals(ResizeMath.edges(500,250,1000,500),new int[]{0,0}));
 System.out.println(count+" resize geometry assertions passed");
 }
}
