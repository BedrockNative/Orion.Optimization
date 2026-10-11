#include <amethyst/Memory.hpp>
#include <orion/GameHelpers.hpp>
#include <Zydis/Zydis.h>
#include <atomic>
#include "parser-signatures.hpp"
namespace orion {
namespace {
GameHelpers helpers;
std::atomic<bool> ready{};
void* resolve(const char* text) {
    auto signature=hat::parse_signature(text);
    if(!signature.has_value())throw std::runtime_error("Invalid game helper signature");
    auto address=FindUniqueSignature(signature.value(),true);
    if(!address)throw std::runtime_error("Missing or ambiguous game helper signature");
    return reinterpret_cast<void*>(address);
}
void* deallocator(void* destroy) {
    // The first direct call in the verified descriptor destructor frees its string.
    // Resolve that call's relative target instead of matching adjacent thunk bytes.
    ZydisDecoder decoder; ZydisDecoderInit(&decoder,ZYDIS_MACHINE_MODE_LONG_64,ZYDIS_STACK_WIDTH_64);
    auto base=reinterpret_cast<uintptr_t>(destroy);
    for(size_t offset=0;offset<128;) {
        ZydisDecodedInstruction insn;ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
        if(!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder,reinterpret_cast<void*>(base+offset),15,&insn,operands)))break;
        if(insn.mnemonic==ZYDIS_MNEMONIC_RET)break;
        if(insn.mnemonic==ZYDIS_MNEMONIC_CALL) {
            if(operands[0].type!=ZYDIS_OPERAND_TYPE_IMMEDIATE || !operands[0].imm.is_relative)break;
            auto target=base+offset+insn.length+operands[0].imm.value.s;
            if(target<GetMinecraftBaseAddress() || target>=GetMinecraftBaseAddress()+GetMinecraftSize())break;
            return reinterpret_cast<void*>(target);
        }
        offset+=insn.length;
    }
    throw std::runtime_error("Unrecognized game descriptor destructor");
}
}
const GameHelpers& gameHelpers(){return helpers;}
bool gameHelpersReady(){return ready.load(std::memory_order_acquire);}
void initializeGameHelpers(){
    GameHelpers h;
    h.parse=resolve(parser_signatures::parse);h.copy=resolve(parser_signatures::copy);
    h.grow=resolve(parser_signatures::grow);h.destroy=resolve(parser_signatures::destroy);
    h.fullPath=resolve(parser_signatures::fullPath);h.free=deallocator(h.destroy);
    helpers=h;ready.store(true,std::memory_order_release);
}
}
