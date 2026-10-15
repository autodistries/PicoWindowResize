#include "../native/resolve.h"
#include "../native/abi.h"
#include <cstdio>
#include <cstdlib>
#include <array>

static int checks;
static void check(bool ok){if(!ok){fprintf(stderr,"discovery check %d failed\n",checks+1);exit(1);}checks++;}
int main(){
    uint32_t accessor[sizeof(positionReturn)/sizeof(uint32_t)];
    memcpy(accessor,positionReturn,sizeof(accessor));
    check(accessorContract(accessor,positionReturn));
    accessor[0]^=1;check(!accessorContract(accessor,positionReturn));
    check(abiInstruction(0x94012345)==abiInstruction(0x94054321));
    check(abiInstruction(0xb0000008)==abiInstruction(0xd0000408));
    uint32_t valid[]={0xf9402c08,0xb40000a8,0xf9400508,0xf100011f,0x1a9f07e0,0xd65f03c0,0x2a1f03e0,0xd65f03c0};
    uint32_t keyboard[]={0xb4000100,0xb9401408,0x510fb509,0x7100093f,0x1a9f27e9,0x710fa51f,0x1a9f17e8,0x2a090100,0xd65f03c0};
    PanelLayout p;check(deriveLayout(valid,keyboard,p)&&p.type==20&&p.layer==88&&p.transform==8);
    valid[0]=(valid[0]&0xffc003ff)|(16<<10);valid[2]=(valid[2]&0xffc003ff)|(3<<10);keyboard[1]=(keyboard[1]&0xffc003ff)|(9<<10);
    check(deriveLayout(valid,keyboard,p)&&p.type==36&&p.layer==128&&p.transform==24);
    valid[1]^=32;check(!deriveLayout(valid,keyboard,p));valid[1]^=32;
    keyboard[2]^=1;check(!deriveLayout(valid,keyboard,p));keyboard[2]^=1;
    valid[0]=(valid[0]&0xffc003ff)|(4095<<10);check(!deriveLayout(valid,keyboard,p));

    alignas(16) std::array<unsigned char,4096> image{};uintptr_t base=(uintptr_t)image.data();
    Elf64_Phdr ph[2]{};ph[0].p_type=PT_LOAD;ph[0].p_memsz=image.size();ph[0].p_flags=PF_R|PF_W;
    ph[1].p_type=PT_DYNAMIC;ph[1].p_vaddr=0x80;ph[1].p_memsz=10*sizeof(Elf64_Dyn);
    dl_phdr_info info{};info.dlpi_addr=base;info.dlpi_phdr=ph;info.dlpi_phnum=2;ModuleView view{info};
    auto d=(Elf64_Dyn*)(base+0x80);char* strings=(char*)(base+0x200);strcpy(strings+1,raySymbol);
    size_t second=strlen(raySymbol)+2;strcpy(strings+second,hitSymbol);
    d[0].d_tag=DT_STRTAB;d[0].d_un.d_ptr=0x200;d[1].d_tag=DT_STRSZ;d[1].d_un.d_val=512;
    d[2].d_tag=DT_SYMTAB;d[2].d_un.d_ptr=0x500;d[3].d_tag=DT_SYMENT;d[3].d_un.d_val=sizeof(Elf64_Sym);
    d[4].d_tag=DT_JMPREL;d[4].d_un.d_ptr=0x600;d[5].d_tag=DT_PLTRELSZ;d[5].d_un.d_val=2*sizeof(Elf64_Rela);
    d[6].d_tag=DT_PLTREL;d[6].d_un.d_val=DT_RELA;
    auto s=(Elf64_Sym*)(base+0x500);s[1].st_name=1;s[2].st_name=second;
    auto r=(Elf64_Rela*)(base+0x600);r[0].r_offset=0x800;r[0].r_info=ELF64_R_INFO(1,R_AARCH64_JUMP_SLOT);
    r[1].r_offset=0x808;r[1].r_info=ELF64_R_INFO(2,R_AARCH64_JUMP_SLOT);
    check(view.jumpSlot(raySymbol)==(uintptr_t*)(base+0x800));check(view.jumpSlot(hitSymbol)==(uintptr_t*)(base+0x808));
    d[0].d_un.d_ptr=base+0x200;check(view.jumpSlot(raySymbol)==(uintptr_t*)(base+0x800));
    r[0].r_offset=0x980;check(view.jumpSlot(raySymbol)==(uintptr_t*)(base+0x980));
    r[0].r_offset=0x981;check(!view.jumpSlot(raySymbol));r[0].r_offset=0x980;
    r[1].r_info=r[0].r_info;check(!view.jumpSlot(raySymbol));r[1].r_info=ELF64_R_INFO(2,R_AARCH64_JUMP_SLOT);
    s[1].st_name=512;check(!view.jumpSlot(raySymbol));s[1].st_name=1;
    r[0].r_info=ELF64_R_INFO(500,R_AARCH64_JUMP_SLOT);check(!view.jumpSlot(raySymbol));r[0].r_info=ELF64_R_INFO(1,R_AARCH64_JUMP_SLOT);
    d[6].d_un.d_val=DT_REL;check(!view.jumpSlot(raySymbol));d[6].d_un.d_val=DT_RELA;
    check(!view.contains(base+4090,16));check(!view.contains(base,4,PF_R|PF_X));
    d[7].d_tag=DT_GNU_HASH;d[7].d_un.d_ptr=0xa00;
    auto hash=[](const char* name){uint32_t h=5381;while(*name)h=h*33+(unsigned char)*name++;return h;};
    auto table=(uint32_t*)(base+0xa00);table[0]=1;table[1]=1;table[2]=1;table[3]=5;
    table[6]=1;table[7]=hash(raySymbol)&~1u;table[8]=hash(hitSymbol)|1u;
    s[1].st_shndx=s[2].st_shndx=1;s[1].st_info=s[2].st_info=ELF64_ST_INFO(STB_GLOBAL,STT_FUNC);
    s[1].st_value=0xc00;s[2].st_value=0xc08;s[1].st_size=s[2].st_size=8;
    check(view.symbol(raySymbol,PF_R)==(void*)(base+0xc00));
    check(view.symbol(hitSymbol,PF_R)==(void*)(base+0xc08));
    check(!view.symbol("missing",PF_R));check(!view.symbol(raySymbol,PF_R|PF_X));
    check(!view.symbol(raySymbol,PF_R,16));
    s[1].st_shndx=SHN_UNDEF;check(!view.symbol(raySymbol,PF_R));s[1].st_shndx=1;
    s[1].st_info=ELF64_ST_INFO(STB_GLOBAL,STT_GNU_IFUNC);check(!view.symbol(raySymbol,PF_R));s[1].st_info=ELF64_ST_INFO(STB_GLOBAL,STT_FUNC);
    s[1].st_other=STV_HIDDEN;check(!view.symbol(raySymbol,PF_R));s[1].st_other=STV_DEFAULT;
    s[1].st_name=512;check(!view.symbol(raySymbol,PF_R));s[1].st_name=1;
    table[2]=0;check(!view.symbol(raySymbol,PF_R));table[2]=1;
    d[7].d_un.d_ptr=base+0xa00;check(view.symbol(raySymbol,PF_R)==(void*)(base+0xc00));
    d[7].d_tag=DT_HASH;table[1]=3;check(view.symbol(hitSymbol,PF_R)==(void*)(base+0xc08));
    printf("%d symbol/relocation/layout discovery assertions passed\n",checks);
}
