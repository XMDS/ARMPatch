#include <unistd.h>
#include <stdlib.h>
#include <vector>
#include <dlfcn.h>
#include <sys/mman.h>

#ifdef __XDL
    #include "xdl.h"
#endif
#ifdef __USEDOBBY
    #include "AML_PrecompiledLibs/include/dobby.h"
#endif
#ifdef __USEGLOSS
    #include "AML_PrecompiledLibs/include/Gloss.h"
#endif

#ifdef __arm__
    #define __32BIT
    #define DETHUMB(_a) (((uintptr_t)_a) & ~0x1)
    #define RETHUMB(_a) (((uintptr_t)_a) | 0x1)
    #define THUMBMODE(_a) ((((uintptr_t)_a) & 0x1)||ARMPatch::bThumbMode||(ARMPatch::GetSymAddrXDL((uintptr_t)_a) & 0x1))
    extern "C" bool MSHookFunction(void* symbol, void* replace, void** result);
#elif defined __aarch64__
    #define __64BIT
    #define DETHUMB(_a)
    #define RETHUMB(_a)
    #define THUMBMODE(_a) (false)
    extern "C" bool A64HookFunction(void *const symbol, void *const replace, void **result);
    #define cacheflush(c, n, zeroarg) __builtin___clear_cache((char*)(c), (char*)(n))
#else
    #error This lib is supposed to work on ARM only!
#endif

#ifndef NO_HOOKDEFINES
    /* Just a hook declaration */
    #define DECL_HOOK(_ret, _name, ...)                             \
        _ret (*_name)(__VA_ARGS__);                                 \
        _ret HookOf_##_name(__VA_ARGS__)
    /* Just a hook declaration with return type = void */
    #define DECL_HOOKv(_name, ...)                                  \
        void (*_name)(__VA_ARGS__);                                 \
        void HookOf_##_name(__VA_ARGS__)
    /* Just a hook of a function */
    #define HOOK(_name, _fnAddr)                                    \
        ARMPatch::Hook((void*)(_fnAddr), (void*)(&HookOf_##_name), (void**)(&_name));
    /* Just a hook of a function (but simpler usage) */
    #define HOOKSYM(_name, _libHndl, _fnSym)                        \
        ARMPatch::Hook((void*)(ARMPatch::GetSym(_libHndl, _fnSym)), (void*)(&HookOf_##_name), (void**)(&_name));
    /* Just a hook of a function located in PLT section (by address!) */
    #define HOOKPLT(_name, _fnAddr)                                 \
        ARMPatch::HookPLT((void*)(_fnAddr), (void*)(&HookOf_##_name), (void**)(&_name));
    #define HOOKPLTSYM(_name, _libHndl, _fnSym)                         \
        do {                                                                  \
            size_t size = 0;                                                  \
            uintptr_t* addr_list = ARMPatch::GetGot(_libHndl, _fnSym, &size); \
            for (size_t i = 0; i < size; i++)                                   \
                ARMPatch::HookPLT((void*)(addr_list[i]), (void*)(&HookOf_##_name), (void**)(&_name)); \
            free(addr_list);                                                    \
            addr_list = nullptr;                                                \
        } while (0)
#endif

#define ARMPATCH_VER 3
    
#ifdef __32BIT
enum ARMRegister : char
{
    ARM_REG_R0 = 0,
    ARM_REG_R1,
    ARM_REG_R2,
    ARM_REG_R3,
    ARM_REG_R4,
    ARM_REG_R5,
    ARM_REG_R6,
    ARM_REG_R7,
    ARM_REG_R8,
    ARM_REG_R9,
    ARM_REG_R10,
    ARM_REG_R11,
    ARM_REG_R12,
    ARM_REG_SP,
    ARM_REG_LR,
    ARM_REG_PC,
    
    ARM_REG_INVALID
};
#include "../AArchASMHelper/ARMv7_ASMHelper.h"
#include "../AArchASMHelper/Thumbv7_ASMHelper.h"
#elif defined __64BIT
enum ARMRegister : char
{
    ARM_REG_W0,  ARM_REG_W1,  ARM_REG_W2,  ARM_REG_W3,
    ARM_REG_W4,  ARM_REG_W5,  ARM_REG_W6,  ARM_REG_W7,
    ARM_REG_W8,  ARM_REG_W9,  ARM_REG_W10, ARM_REG_W11,
    ARM_REG_W12, ARM_REG_W13, ARM_REG_W14, ARM_REG_W15,
    ARM_REG_W16, ARM_REG_W17, ARM_REG_W18, ARM_REG_W19,
    ARM_REG_W20, ARM_REG_W21, ARM_REG_W22, ARM_REG_W23,
    ARM_REG_W24, ARM_REG_W25, ARM_REG_W26, ARM_REG_W27,
    ARM_REG_W28, ARM_REG_W29, ARM_REG_W30, ARM_REG_W31,

    ARM_REG_X0,  ARM_REG_X1,  ARM_REG_X2,  ARM_REG_X3,
    ARM_REG_X4,  ARM_REG_X5,  ARM_REG_X6,  ARM_REG_X7,
    ARM_REG_X8,  ARM_REG_X9,  ARM_REG_X10, ARM_REG_X11,
    ARM_REG_X12, ARM_REG_X13, ARM_REG_X14, ARM_REG_X15,
    ARM_REG_X16, ARM_REG_X17, ARM_REG_X18, ARM_REG_X19,
    ARM_REG_X20, ARM_REG_X21, ARM_REG_X22, ARM_REG_X23,
    ARM_REG_X24, ARM_REG_X25, ARM_REG_X26, ARM_REG_X27,
    ARM_REG_X28, ARM_REG_X29, ARM_REG_X30, ARM_REG_X31,
    
    ARM_REG_INVALID
};
#include "../AArchASMHelper/ARMv8_ASMHelper.h"
#endif

struct bytePattern
{
    struct byteEntry
    {
        uint8_t nValue;
        bool bUnknown;
    };
    std::vector<byteEntry> vBytes;
};

namespace ARMPatch
{
    extern bool bThumbMode;
    
    const char* GetPatchVerStr();
    int GetPatchVerInt();
    /*
        Get library's start address
        soLib - name of a loaded library
    */
    uintptr_t GetLib(const char* soLib);
    /*
        Get library's handle
        soLib - name of a loaded library
    */
    void* GetLibHandle(const char* soLib);
    /*
        Get library's handle
        libAddr - an address of anything inside a library
    */
    void* GetLibHandle(uintptr_t libAddr);
    /*
        Close library's handle
        handle - HANDLE (NOTICE THIS!!!) of a library (u can obtain it using dlopen)
    */
    void CloseLibHandle(void* handle);
    /*
        Get library's end address
        soLib - name of a loaded library
    */
    uintptr_t GetLibLength(const char* soLib);
    /*
        Get library's function address by symbol (__unwind???)
        handle - HANDLE (NOTICE THIS!!!) of a library (u can obtain it using dlopen)
        sym - name of a function
    */
    uintptr_t GetSym(void* handle, const char* sym);
    /*
        Get library's function address by symbol (__unwind???)
        libAddr - an address of anything inside a library
        sym - name of a function
        @XMDS requested this
    */
    uintptr_t GetSym(uintptr_t libAddr, const char* sym);
    
    /*
        Reprotect memory to allow reading/writing/executing
        addr - address
        len - range
    */
    int Unprotect(uintptr_t addr, size_t len = PAGE_SIZE);
    
    /*
        Write to memory (reprotects it)
        dest - where to start?
        src - address of an info to write
        size - size of an info
    */
    void Write(uintptr_t dest, uintptr_t src, size_t size);
    inline void Write(uintptr_t dest, const char* data)
    {
        Write(dest, (uintptr_t)data, strlen(data));
    }
    inline void Write(uintptr_t dest, const char* data, size_t size)
    {
        Write(dest, (uintptr_t)data, size);
    }
    inline void Write(uintptr_t dest, uint64_t data)
    {
        uint64_t dataPtr = data;
        Write(dest, (uintptr_t)&dataPtr, 8);
    }
    inline void Write(uintptr_t dest, uint32_t data)
    {
        uint32_t dataPtr = data;
        Write(dest, (uintptr_t)&dataPtr, 4);
    }
    inline void Write(uintptr_t dest, uint16_t data)
    {
        uint16_t dataPtr = data;
        Write(dest, (uintptr_t)&dataPtr, 2);
    }
    inline void Write(uintptr_t dest, uint8_t data)
    {
        uint8_t dataPtr = data;
        Write(dest, (uintptr_t)&dataPtr, 1);
    }
    
    /*
        Read memory (reprotects it)
        src - where to read from?
        dest - where to write a readed info?
        size - size of an info
    */
    void Read(uintptr_t src, uintptr_t dest, size_t size);
    
    /*
        Place NotOPerator instruction (reprotects it)
        addr - where to put
        count - how much times to put
    */
    int WriteNOP(uintptr_t addr, size_t count = 1);
    
    /*
        Place 4-byte NotOPerator instruction (reprotects it)
        Similar to WriteNOP but differs for Thumb execution mode
        addr - where to put
        count - how much times to put
    */
    int WriteNOP4(uintptr_t addr, size_t count = 1);
    
    /*
        Place RET instruction (RETURN, function end, reprotects it)
        addr - where to put
    */
    int WriteRET(uintptr_t addr);
    
    /*
        Place MOV instruction (register)
        addr - where to put
        from - src register
        to - dest register
        is_t16 - thumb16 mov
    */
    void WriteMOV(uintptr_t addr, ARMRegister from, ARMRegister to, bool is_t16 = false);
    
    /*
        Place absolute jump instruction (moves directly to the function with the same stack!)
        Very fast and very lightweight!
        addr - where to redirect
        to - redirect to what?
        _4byte - use b instruction jump (GlossHook)
    */
    int Redirect(uintptr_t addr, uintptr_t to, bool _4byte = false);
    
    /*
        ByteScanner
        pattern - pattern.
        soLib - library's name
    */
    uintptr_t GetAddressFromPattern(const char* pattern, const char* soLib);
    
    /*
        ByteScanner
        pattern - pattern.
        libStart - library's start address
        scanLen - how much to scan from libStart
    */
    uintptr_t GetAddressFromPattern(const char* pattern, uintptr_t libStart, size_t scanLen);
    
    /*
        Cydia's Substrate / Rprop's / dobby / Gloss Inline Hook (use hook instead of hookInternal, ofc reprotects it!)
        addr - what to hook?
        func - Call that function instead of an original
        original - Original function!
        _4byte - use b instruction jump (GlossHook)
    */
    bool hookInternal(void* addr, void* func, void** original, bool _4byte = false);
    template<class A, class B>
    bool Hook(A addr, B func) { return hookInternal((void*)addr, (void*)func, (void**)NULL); }
    template<class A, class B, class C>
    bool Hook(A addr, B func, C original) { return hookInternal((void*)addr, (void*)func, (void**)original); }
    
    /*
        A simple hook of a .got .plt section functions (use HookPLT instead of hookPLTInternal, ofc reprotects it!)
        addr - what to hook?
        func - Call that function instead of an original
        original - Original function!
    */
    bool hookPLTInternal(void* addr, void* func, void** original);
    template<class A, class B>
    bool HookPLT(A addr, B func) { return hookPLTInternal((void*)addr, (void*)func, (void**)NULL); }
    template<class A, class B, class C>
    bool HookPLT(A addr, B func, C original) { return hookPLTInternal((void*)addr, (void*)func, (void**)original); }
    
    /*
        If it`s a thumb code? A simple ass check
        addr - what to check?
    */
    bool IsThumbAddr(uintptr_t addr);
    
    // xDL part
    uintptr_t GetLibXDL(void* handle);
    uintptr_t GetSymAddrXDL(uintptr_t libaddr);
    size_t GetSymSizeXDL(uintptr_t libaddr);
    const char* GetSymNameXDL(uintptr_t libaddr);
    
    // GlossHook part START
    
    // Better place branch
    
    /*
        Place B instruction (reprotects it)
        addr - where to put
        dest - Jump to what?
        is_t16 - Thumb16 B
    */
    int WriteB(uintptr_t addr, uintptr_t dest, bool is_t16 = false);
    
    /*
        Place BL instruction (reprotects it)
        addr - where to put
        dest - Jump to what?
        is_width - Thumb BL.W
    */
    void WriteBL(uintptr_t addr, uintptr_t dest, bool is_width = false);
    
    /*
        Place BLX instruction (reprotects it)
        addr - where to put
        dest - Jump to what?
        is_width - Thumb BLX.W
    */
    void WriteBLX(uintptr_t addr, uintptr_t dest, bool is_width = false);
    
    /*
        Get library's .got address by symbol 
        handle - HANDLE (NOTICE THIS!!!) of a library (only xdl)
        sym - name of a function
        size - addr array size
        return - .got addr array save
    */
    uintptr_t* GetGot(void* handle, const char* sym, size_t* size);
    
     /*
        ByteScanner
        pattern - pattern.
        soLib - library's name
        section - section's name (default: ".text")
    */
    uintptr_t GetAddressFromPattern(const char* pattern, const char* soLib, const char* section);

    /*
        A branch hook (BL BLX)
        addr - what to hook?
        func - Call that function instead of an original
        original - Original function!
    */
    bool hookBranchLinkInternal(void* addr, void* func, void** original);
    template<class A, class B>
    bool HookBL(A addr, B func) { return hookBranchLinkInternal((void*)addr, (void*)func, (void**)NULL); }
    template<class A, class B, class C>
    bool HookBL(A addr, B func, C original) { return hookBranchLinkInternal((void*)addr, (void*)func, (void**)original); }

    bool hookBranchLinkXInternal(void* addr, void* func, void** original);
    template<class A, class B>
    bool HookBLX(A addr, B func) { return hookBranchLinkXInternal((void*)addr, (void*)func, (void**)NULL); }
    template<class A, class B, class C>
    bool HookBLX(A addr, B func, C original) { return hookBranchLinkXInternal((void*)addr, (void*)func, (void**)original); }

    /*
        Get the address BRANCH refers to
        addr - what to check?
    */
    uintptr_t GetBranchDest(uintptr_t addr);

    // GlossHook part END
}
