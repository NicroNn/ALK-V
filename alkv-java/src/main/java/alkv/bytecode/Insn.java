package alkv.bytecode;

public record Insn(int word) {
    public static Insn ABC(Opcode op, int a, int b, int c) {
        return new Insn((op.ordinal() & 0xFF)
                | ((a & 0xFF) << 8)
                | ((b & 0xFF) << 16)
                | ((c & 0xFF) << 24));
    }

    public static Insn ABx(Opcode op, int a, int bx) {
        return new Insn((op.ordinal() & 0xFF)
                | ((a & 0xFF) << 8)
                | ((bx & 0xFFFF) << 16));
    }

    public static Insn AsBx(Opcode op, int a, int sbx) {
        int s = sbx & 0xFFFF; // two's complement
        return new Insn((op.ordinal() & 0xFF)
                | ((a & 0xFF) << 8)
                | (s << 16));
    }
}
