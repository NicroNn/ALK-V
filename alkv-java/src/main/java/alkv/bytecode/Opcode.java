package alkv.bytecode;

public enum Opcode {
    NOP,

    MOV,        // ABC: A=dst, B=src
    LOADK,      // ABx: A=dst, Bx=constId

    // арифметика/сравнения (как было)
    ADD_I, SUB_I, MUL_I, DIV_I, MOD_I,
    ADD_F, SUB_F, MUL_F, DIV_F, MOD_F,
    LT_I, LE_I, GT_I, GE_I,
    LT_F, LE_F, GT_F, GE_F,
    EQ, NE,
    NOT,

    // прыжки
    JMP,        // AsBx
    JMP_T,      // AsBx: A=condReg
    JMP_F,      // AsBx: A=condReg

    // конверсии
    I2F,        // ABC: A=dst(float), B=src(int)

    // --- new: arrays ---
    NEW_ARR,    // ABC: A=dstArr, B=sizeReg, C=0
    GET_ELEM,   // ABC: A=dst, B=arrReg, C=indexReg
    SET_ELEM,   // ABC: A=arrReg, B=indexReg, C=valueReg

    // --- new: objects/classes ---
    NEW_OBJ,    // ABx: A=dstObj, Bx=constId(KClass)
    GET_FIELD,  // ABx: A=dst, B=objReg, x=constId(KField)  (см. ниже про формат)
    SET_FIELD,  // ABx: A=objReg, B=valueReg, x=constId(KField)

    // --- new: calls ---
    CALL,       // ABC: A=dst, B=funcRegOrConstMode, C=argc
    CALLK,      // ABx: A=dst, Bx=constId(KFunc/KMethod), (argc берём из K* или отдельным MOV/LOADK)
    CALL_NATIVE,// ABC: A=dst, B=nativeId, C=argc

    RET         // ABC: A=valueReg (или 255 = void)
}