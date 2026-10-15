package research.pico.windowresize;

final class SaveOrder {
    static boolean accept(long currentGeneration,long requestGeneration,long submitted,
                          long clearedAt,int currentBoot,int clearBoot){
        if(submitted<=0)return false;
        if(currentBoot==clearBoot&&submitted<=clearedAt)return false;
        return requestGeneration<0||requestGeneration==currentGeneration;
    }
}
