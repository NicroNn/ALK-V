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

    public void jmp(Label target) {
        int at = pc();
        code.add(Insn.AsBx(Opcode.JMP, 0, 0)); // placeholder
        target.patchSites.add(at);
    }

    public void jmpT(int condReg, Label target) {
        int at = pc();
        code.add(Insn.AsBx(Opcode.JMP_T, condReg, 0)); // placeholder
        target.patchSites.add(at);
    }

    public void jmpF(int condReg, Label target) {
        int at = pc();
        code.add(Insn.AsBx(Opcode.JMP_F, condReg, 0)); // placeholder
        target.patchSites.add(at);
    }

    public void patch(Label target) {
        if (target.position == -1) throw new IllegalStateException("Label not bound");
        for (int at : target.patchSites) {
            int nextPc = at + 1;
            int sbx = target.position - nextPc; // relative from *next* instruction [web:147]
            Insn old = code.get(at);
            int oldWord = old.word();
            int op = oldWord & 0xFF;
            int a = (oldWord >>> 8) & 0xFF;
            code.set(at, Insn.AsBx(Opcode.values()[op], a, sbx));
        }
        target.patchSites.clear();
    }
}
