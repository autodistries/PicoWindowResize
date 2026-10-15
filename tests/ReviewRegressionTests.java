package research.pico.windowresize;

import java.util.HashSet;
import java.util.Arrays;

public final class ReviewRegressionTests {
    private static int checks;
    private static void check(boolean value){if(!value)throw new AssertionError("check "+(checks+1));checks++;}
    public static void main(String[] args){
        LatestRequests<String> q=new LatestRequests<>();
        q.put(1,10,"A-moving");q.put(1,10,"A-final");q.put(2,20,"B-final");q.put(3,20,"C-final");
        HashSet<String> actual=new HashSet<>();String v;while((v=q.poll())!=null)actual.add(v);
        check(actual.equals(new HashSet<>(Arrays.asList("A-final","B-final","C-final"))));
        check(q.isEmpty());
        q.put(1,10,"A");check("A".equals(q.poll()));q.put(1,10,"A-next");check("A-next".equals(q.poll()));
        check(SaveOrder.accept(0,-1,10,0,1,-1)); // Save before initial cache load.
        check(!SaveOrder.accept(2,1,120,100,1,1)); // Old storage generation.
        check(!SaveOrder.accept(2,2,90,100,1,1)); // Old IPC request arrives after Clear.
        check(!SaveOrder.accept(2,-1,100,100,1,1)); // Invalidation/clear boundary.
        check(SaveOrder.accept(2,-1,101,100,1,1)); // New drag while cache reloads.
        check(SaveOrder.accept(2,2,101,100,1,1));
        check(SaveOrder.accept(2,2,10,100,2,1)); // Uptime resets after reboot.
        check(!SaveOrder.accept(0,-1,0,0,1,-1)); // Missing provenance.
        System.out.println(checks+" async queue/save ordering regression assertions passed");
    }
}
