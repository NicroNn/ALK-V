#pragma once
#include <cstdint>
#include <vector>
#include "alkv/vm/value.hpp"

namespace alkv::bc {

// ДОЛЖНО совпадать по ordinal с Java enum Opcode
enum class Opcode : uint8_t {
    NOP,
    MOV,        // ABC: A=dst, B=src
    LOADK,      // ABx: A=dst, Bx=constId

    ADD_I, SUB_I, MUL_I, DIV_I, MOD_I,
    ADD_F, SUB_F, MUL_F, DIV_F, MOD_F,

    LT_I, LE_I, GT_I, GE_I,
    LT_F, LE_F, GT_F, GE_F,

    EQ, NE,
    NOT,

    JMP,        // AsBx
    JMP_T,      // AsBx: A=condReg
    JMP_F,      // AsBx: A=condReg

    I2F,        // ABC: A=dst(float), B=src(int)

    // --- arrays ---
    NEW_ARR,    // ABC: A=dstArr, B=sizeReg, C=0
    GET_ELEM,   // ABC: A=dst, B=arrReg, C=indexReg
    SET_ELEM,   // ABC: A=arrReg, B=indexReg, C=valueReg

    // --- objects/classes ---
    NEW_OBJ,    // ABx: A=dstObj, Bx=constId(KClass)
    // ВАЖНО: по факту компилятор сейчас использует ABC:
    // GET_FIELD: A=dst, B=objReg, C=fieldRefReg(KField)
    // SET_FIELD: A=objReg, B=fieldRefReg, C=valueReg
    GET_FIELD,
    SET_FIELD,

    // --- calls ---
    CALL,       // ABC: A=dst, B=funcReg, C=argc
    CALLK,      // ABx: A=dst, Bx=constId(KFunc/KMethod)
    CALL_NATIVE,// ABC: A=dst, B=nativeId, C=argc

    RET         // ABC: A=valueReg (или 255 = void)
};

struct DecodedABC { Opcode op; uint8_t a,b,c; };
struct DecodedABx { Opcode op; uint8_t a; uint16_t bx; };
struct DecodedAsBx { Opcode op; uint8_t a; int16_t sbx; };

inline Opcode decodeOp(uint32_t w) { return static_cast<Opcode>(w & 0xFFu); }

inline DecodedABC decodeABC(uint32_t w) {
    return { decodeOp(w),
             static_cast<uint8_t>((w >> 8) & 0xFFu),
             static_cast<uint8_t>((w >> 16) & 0xFFu),
             static_cast<uint8_t>((w >> 24) & 0xFFu) };
}

inline DecodedABx decodeABx(uint32_t w) {
    return { decodeOp(w),
             static_cast<uint8_t>((w >> 8) & 0xFFu),
             static_cast<uint16_t>((w >> 16) & 0xFFFFu) };
}

inline DecodedAsBx decodeAsBx(uint32_t w) {
    uint16_t u = static_cast<uint16_t>((w >> 16) & 0xFFFFu);
    int16_t s = static_cast<int16_t>(u);
    return { decodeOp(w),
             static_cast<uint8_t>((w >> 8) & 0xFFu),
             s };
}

struct Function {
    std::vector<alkv::vm::Value> constPool; // runtime values (включая func/class/field refs)
    std::vector<uint32_t> code;
    uint16_t regCount = 0;
};

} // namespace alkv::bc