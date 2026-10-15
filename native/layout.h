#pragma once
#include <cstddef>
#include <cstdint>

struct PanelLayout {size_t type=0,layer=0,transform=0;};
// AArch64 instruction contracts, not addresses: reject a changed implementation.
inline bool deriveLayout(const uint32_t* valid,const uint32_t* keyboard,PanelLayout& out){
    constexpr uint32_t loadMask=0xffc003ff;
    if((valid[0]&loadMask)!=0xf9400008||valid[1]!=0xb40000a8
       ||(valid[2]&loadMask)!=0xf9400108||valid[3]!=0xf100011f
       ||valid[4]!=0x1a9f07e0||valid[5]!=0xd65f03c0
       ||valid[6]!=0x2a1f03e0||valid[7]!=0xd65f03c0)return false;
    if(keyboard[0]!=0xb4000100||(keyboard[1]&loadMask)!=0xb9400008
       ||keyboard[2]!=0x510fb509||keyboard[3]!=0x7100093f
       ||keyboard[4]!=0x1a9f27e9||keyboard[5]!=0x710fa51f
       ||keyboard[6]!=0x1a9f17e8||keyboard[7]!=0x2a090100||keyboard[8]!=0xd65f03c0)return false;
    PanelLayout p{((keyboard[1]>>10)&4095)*4u,((valid[0]>>10)&4095)*8u,((valid[2]>>10)&4095)*8u};
    if(!p.type||p.type>256||!p.layer||p.layer>512||!p.transform||p.transform>256)return false;
    out=p;return true;
}
