package research.pico.windowresize;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/** Coalesce within one window without discarding another window's final request. */
final class LatestRequests<T> {
    private final ConcurrentHashMap<Long,T> values=new ConcurrentHashMap<>();
    void put(int display,int panel,T value){values.put(((long)display<<32)|(panel&0xffffffffL),value);}
    boolean isEmpty(){return values.isEmpty();}
    T poll(){
        for(Map.Entry<Long,T> e:values.entrySet())if(values.remove(e.getKey(),e.getValue()))return e.getValue();
        return null;
    }
}
