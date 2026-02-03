package alkv.bytecode;

public enum Opcode {
    NOP,
    MOV,        // ABC: A=dst, B=src
    LOADK,      // ABx: A=dst, Bx=constId

    ADD_I, SUB_I, MUL_I, DIV_I, MOD_I,  // ABC: A=dst, B=lhs, C=rhs
    ADD_F, SUB_F, MUL_F, DIV_F, MOD_F,

    LT_I, LE_I, GT_I, GE_I,
    LT_F, LE_F, GT_F, GE_F,
    EQ, NE,      // ABC: A=dstBool, B=lhs, C=rhs

    NOT, AND, OR,

    JMP,         // AsBx: sBx
    JMP_T,       // AsBx: A=condReg, sBx
    JMP_F,       // AsBx: A=condReg, sBx

    RET,        // ABC: A=valueReg (или 255 = void)
    I2F,        // ABC: A=dst(float), B=src(int)
}
