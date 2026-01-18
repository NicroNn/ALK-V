package alkv.bytecode;

import java.util.ArrayList;
import java.util.List;

public final class Emitter {
    private final List<Insn> code = new ArrayList<>();

    public int pc() { return code.size(); }

    public List<Insn> code() { return code; }

    public Label newLabel() { return new Label(); }

    public void bind(Label l) {
        if (l.position != -1) throw new IllegalStateException("Label already bound");
        l.position = pc();
    }

    public void emitABC(Opcode op, int a, int b, int c) {
        code.add(Insn.ABC(op, a, b, c));
    }

    public void emitABx(Opcode op, int a, int bx) {
        code.add(Insn.ABx(op, a, bx));
    }

    public void emitAsBx(Opcode op, int a, int sbx) {
        code.add(Insn.AsBx(op, a, sbx));
    }

    // --- jumps ---

    private static void checkSbxRange(int sbx) {
        if (sbx < Short.MIN_VALUE || sbx > Short.MAX_VALUE) {
            throw new IllegalStateException("Jump offset out of s16 range: " + sbx);
        }
    }

    private void emitJump(Opcode op, int a, Label target) {
        int at = pc();
        int nextPc = at + 1;

        if (target.position != -1) {
            int sbx = target.position - nextPc; // relative from _next_ instruction
            checkSbxRange(sbx);
            code.add(Insn.AsBx(op, a, sbx));
        } else {
            code.add(Insn.AsBx(op, a, 0)); // placeholder
            target.patchSites.add(at);
        }
    }

    public void jmp(Label target) {
        emitJump(Opcode.JMP, 0, target);
    }

    public void jmpT(int condReg, Label target) {
        emitJump(Opcode.JMP_T, condReg, target);
    }

    public void jmpF(int condReg, Label target) {
        emitJump(Opcode.JMP_F, condReg, target);
    }

    public void patch(Label target) {
        if (target.position == -1) throw new IllegalStateException("Label not bound");

        for (int at : target.patchSites) {
            int nextPc = at + 1;
            int sbx = target.position - nextPc; // relative from _next_ instruction
            checkSbxRange(sbx);

            Insn old = code.get(at);
            int oldWord = old.word();

            int opByte = oldWord & 0xFF;
            if (opByte < 0 || opByte >= Opcode.values().length) {
                throw new IllegalStateException("Bad opcode byte in instruction word: " + opByte);
            }

            int a = (oldWord >>> 8) & 0xFF;
            Opcode op = Opcode.values()[opByte];

            code.set(at, Insn.AsBx(op, a, sbx));
        }
        target.patchSites.clear();
    }
}