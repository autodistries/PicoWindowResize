#pragma once
#include "layout.h"
#include <link.h>
#include <dlfcn.h>
#include <elf.h>
#include <cstring>
#include <limits>

static constexpr const char* raySymbol="_Z19rayHitCylinderLayerN3glm3vecILi3EfLNS_9qualifierE0EEES2_S2_NS_3quaIfLS1_0EEE12CylinderInfoNS0_ILi2EfLS1_0EEERbRS2_RfS9_RS4_5ARect";
static constexpr const char* hitSymbol="_ZN12InputManager19getRayPanelHitPointERKNSt6__ndk16vectorIP5PanelNS0_9allocatorIS3_EEEER7hitInfoP15InputSourceBase";
struct ModuleView {
    const dl_phdr_info& info;
    bool contains(uintptr_t address,size_t bytes,unsigned flags=PF_R)const{
        if(bytes>std::numeric_limits<uintptr_t>::max()-address)return false;
        for(int i=0;i<info.dlpi_phnum;i++){
            const auto& p=info.dlpi_phdr[i];uintptr_t first=info.dlpi_addr+p.p_vaddr;
            if(p.p_type==PT_LOAD&&(p.p_flags&flags)==flags&&address>=first
               &&address+bytes<=first+p.p_memsz)return true;
        }return false;
    }
    uintptr_t dynamicPointer(uintptr_t value)const{
        if(contains(value,1))return value;
        if(value>std::numeric_limits<uintptr_t>::max()-info.dlpi_addr)return 0;
        value+=info.dlpi_addr;return contains(value,1)?value:0;
    }
    // Read the loaded export table directly: LSPosed and XRShell use different
    // Android linker namespaces, so dlopen/dlsym cannot see the existing shell.
    void* symbol(const char* name,unsigned flags=PF_R|PF_X,size_t bytes=4)const{
        const Elf64_Dyn* dynamic=nullptr;size_t count=0;
        for(int i=0;i<info.dlpi_phnum;i++)if(info.dlpi_phdr[i].p_type==PT_DYNAMIC){
            auto& p=info.dlpi_phdr[i];dynamic=(const Elf64_Dyn*)(info.dlpi_addr+p.p_vaddr);count=p.p_memsz/sizeof(*dynamic);
        }
        if(!dynamic||!contains((uintptr_t)dynamic,count*sizeof(*dynamic)))return nullptr;
        uintptr_t strings=0,symbols=0,gnu=0,sysv=0;size_t stringSize=0,symbolSize=0;
        for(size_t i=0;i<count&&dynamic[i].d_tag!=DT_NULL;i++){
            auto& d=dynamic[i];switch(d.d_tag){
                case DT_STRTAB:strings=dynamicPointer(d.d_un.d_ptr);break;
                case DT_STRSZ:stringSize=d.d_un.d_val;break;
                case DT_SYMTAB:symbols=dynamicPointer(d.d_un.d_ptr);break;
                case DT_SYMENT:symbolSize=d.d_un.d_val;break;
                case DT_GNU_HASH:gnu=dynamicPointer(d.d_un.d_ptr);break;
                case DT_HASH:sysv=dynamicPointer(d.d_un.d_ptr);break;
            }
        }
        if(!strings||!symbols||!stringSize||symbolSize!=sizeof(Elf64_Sym)||!contains(strings,stringSize))return nullptr;
        auto atIndex=[&](size_t index)->void*{
            if(index>(std::numeric_limits<uintptr_t>::max()-symbols)/sizeof(Elf64_Sym))return nullptr;
            uintptr_t address=symbols+index*sizeof(Elf64_Sym);if(!contains(address,sizeof(Elf64_Sym)))return nullptr;
            auto& s=*(const Elf64_Sym*)address;
            auto binding=ELF64_ST_BIND(s.st_info),type=ELF64_ST_TYPE(s.st_info),visibility=(s.st_other&3);
            if(s.st_shndx==SHN_UNDEF||s.st_shndx>=SHN_LORESERVE
               ||(binding!=STB_GLOBAL&&binding!=STB_WEAK)||(type!=STT_FUNC&&type!=STT_OBJECT)
               ||(visibility!=STV_DEFAULT&&visibility!=STV_PROTECTED)||s.st_name>=stringSize)return nullptr;
            auto text=(const char*)(strings+s.st_name);
            if(!memchr(text,0,stringSize-s.st_name)||strcmp(text,name)||s.st_size<bytes
               ||s.st_value>std::numeric_limits<uintptr_t>::max()-info.dlpi_addr)return nullptr;
            address=info.dlpi_addr+s.st_value;
            return contains(address,bytes,flags)?(void*)address:nullptr;
        };
        if(gnu&&contains(gnu,16)){
            auto header=(const uint32_t*)gnu;uint32_t buckets=header[0],first=header[1],bloom=header[2];
            if(!buckets||buckets>1048576||!bloom||bloom>1048576)return nullptr;
            uintptr_t bucket=gnu+16+size_t(bloom)*sizeof(Elf64_Xword);
            if(!contains(bucket,size_t(buckets)*4))return nullptr;
            uintptr_t chain=bucket+size_t(buckets)*4;
            uint32_t hash=5381;for(auto p=(const unsigned char*)name;*p;p++)hash=hash*33+*p;
            uint32_t index=((const uint32_t*)bucket)[hash%buckets];if(index<first)return nullptr;
            for(size_t n=0;n<1048576;n++,index++){
                uintptr_t address=chain+size_t(index-first)*4;
                if(!contains(address,4))return nullptr;
                uint32_t value=*(const uint32_t*)address;
                if((value|1)==(hash|1))if(void* found=atIndex(index))return found;
                if(value&1)return nullptr;
            }
            return nullptr;
        }
        if(sysv&&contains(sysv,8)){
            uint32_t count=((const uint32_t*)sysv)[1];if(count>1048576)return nullptr;
            for(uint32_t i=0;i<count;i++)if(void* found=atIndex(i))return found;
        }
        return nullptr;
    }
    uintptr_t* jumpSlot(const char* name)const{
        const Elf64_Dyn* dynamic=nullptr;size_t count=0;
        for(int i=0;i<info.dlpi_phnum;i++)if(info.dlpi_phdr[i].p_type==PT_DYNAMIC){
            auto& p=info.dlpi_phdr[i];dynamic=(const Elf64_Dyn*)(info.dlpi_addr+p.p_vaddr);count=p.p_memsz/sizeof(*dynamic);
        }
        if(!dynamic||!contains((uintptr_t)dynamic,count*sizeof(*dynamic)))return nullptr;
        uintptr_t strings=0,symbols=0,relocs=0;size_t stringSize=0,relocSize=0,symbolSize=0;bool rela=false;
        for(size_t i=0;i<count&&dynamic[i].d_tag!=DT_NULL;i++){
            auto& d=dynamic[i];switch(d.d_tag){
                case DT_STRTAB:strings=dynamicPointer(d.d_un.d_ptr);break;
                case DT_SYMTAB:symbols=dynamicPointer(d.d_un.d_ptr);break;
                case DT_JMPREL:relocs=dynamicPointer(d.d_un.d_ptr);break;
                case DT_STRSZ:stringSize=d.d_un.d_val;break;
                case DT_PLTRELSZ:relocSize=d.d_un.d_val;break;
                case DT_SYMENT:symbolSize=d.d_un.d_val;break;
                case DT_PLTREL:rela=d.d_un.d_val==DT_RELA;break;
            }
        }
        if(!rela||symbolSize!=sizeof(Elf64_Sym)||!stringSize||!relocSize||relocSize%sizeof(Elf64_Rela)
           ||!contains(strings,stringSize)||!contains(relocs,relocSize))return nullptr;
        uintptr_t* found=nullptr;
        for(size_t i=0;i<relocSize/sizeof(Elf64_Rela);i++){
            auto& r=((const Elf64_Rela*)relocs)[i];if(ELF64_R_TYPE(r.r_info)!=R_AARCH64_JUMP_SLOT)continue;
            size_t index=ELF64_R_SYM(r.r_info);
            if(index>(std::numeric_limits<uintptr_t>::max()-symbols)/sizeof(Elf64_Sym))return nullptr;
            uintptr_t at=symbols+index*sizeof(Elf64_Sym);if(!contains(at,sizeof(Elf64_Sym)))return nullptr;
            auto& s=*(const Elf64_Sym*)at;if(s.st_name>=stringSize)return nullptr;
            auto text=(const char*)(strings+s.st_name);if(!memchr(text,0,stringSize-s.st_name))return nullptr;
            if(strcmp(text,name))continue;
            uintptr_t address=info.dlpi_addr+r.r_offset;
            if(address%alignof(uintptr_t)||!contains(address,sizeof(uintptr_t))||found)return nullptr;
            found=(uintptr_t*)address;
        }return found;
    }
};
