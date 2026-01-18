package alkv.bytecode;

import alkv.ast.decl.FunctionDecl;
import alkv.ast.expr.*;
import alkv.ast.stmt.*;
import alkv.sema.TypeChecker;
import alkv.types.PrimitiveType;
import alkv.types.Type;
import alkv.types.TypeUtil;

import java.util.*;

import static alkv.bytecode.Opcode.*;

public final class SingleFunctionCompiler {
    public record CompiledFunction(ConstPool consts, List<Insn> code, int regCount) {}

    private final TypeChecker.Result sema;
    private final ConstPool consts = new ConstPool();
    private final Emitter out = new Emitter();

    private final Deque<Map<String, Integer>> scopes = new ArrayDeque<>();
    private final Deque<Integer> freeTemps = new ArrayDeque<>();

    // защита от двойного free одного и того же регистра
    private final boolean[] isFree = new boolean[256];

    private int nextReg = 0;

    public SingleFunctionCompiler(TypeChecker.Result sema) {
        this.sema = sema;
    }

    public CompiledFunction compile(FunctionDecl fn) {
        // params -> regs 0..n-1
        pushScope();
        for (Object po : fn.params()) {
            FunctionDecl.Param p = (FunctionDecl.Param) po;
            int r = allocReg();
            define(p.name(), r);
        }

        emitBlock(fn.body());

        // если функция void и нет явного return — вернём void
        out.emitABC(RET, 255, 0, 0);

        popScope();
        return new CompiledFunction(consts, out.code(), nextReg);
    }

    // ---------- scopes ----------
    private void pushScope() { scopes.push(new HashMap<>()); }

    private void popScope() {
        Map<String, Integer> m = scopes.pop();
        // локалы можно переиспользовать после выхода из scope
        for (int r : m.values()) freeReg(r);
    }

    private void define(String name, int reg) { scopes.peek().put(name, reg); }

    private Integer resolve(String name) {
        for (Map<String, Integer> s : scopes) {
            Integer r = s.get(name);
            if (r != null) return r;
        }
        return null;
    }

    private boolean isCapturedByScopes(int reg) {
        for (Map<String, Integer> s : scopes) {
            for (int v : s.values()) {
                if (v == reg) return true;
            }
        }
        return false;
    }

    // ---------- regs ----------
    private int allocReg() {
        Integer r = freeTemps.pollFirst();
        if (r != null) {
            isFree[r] = false;
            return r;
        }
        int v = nextReg++;
        if (v > 250) throw new RuntimeException("Register overflow (u8): " + v);
        isFree[v] = false;
        return v;
    }

    private void freeReg(int r) {
        if (r == 255) return;
        // нельзя освобождать регистры, которые принадлежат живым переменным/параметрам
        if (isCapturedByScopes(r)) return;

        if (r < 0 || r >= isFree.length) return;
        if (isFree[r]) return; // уже свободен

        isFree[r] = true;
        freeTemps.addFirst(r);
    }

    // ---------- statements ----------
    private void emitBlock(BlockStmt b) {
        pushScope();
        for (Object so : b.statements()) emitStmt((Stmt) so);
        popScope();
    }

    private void emitStmt(Stmt s) {
        switch (s) {
            case BlockStmt b -> emitBlock(b);

            case VarDeclStmt v -> {
                int dst = allocReg();
                define(v.name(), dst);
                if (v.initializer() != null) {
                    int rhs = emitExpr(v.initializer());
                    out.emitABC(MOV, dst, rhs, 0);
                    freeReg(rhs);
                }
            }

            case ExprStmt e -> {
                int r = emitExpr(e.expr());
                freeReg(r);
            }

            case ReturnStmt r -> {
                if (r.value() == null) {
                    out.emitABC(RET, 255, 0, 0);
                } else {
                    int v = emitExpr(r.value());
                    out.emitABC(RET, v, 0, 0);
                    freeReg(v);
                }
            }

            case IfStmt i -> emitIf(i);
            case WhileStmt w -> emitWhile(w);
            case ForRangeStmt fr -> emitForRange(fr);
            case ForStmt f -> emitForCStyle(f);
        }
    }

    private void emitIf(IfStmt i) {
        Label elseL = out.newLabel();
        Label endL = out.newLabel();

        int cond = emitExpr(i.condition());
        out.jmpF(cond, elseL);
        freeReg(cond);

        emitBlock(i.thenBlock());
        out.jmp(endL);

        out.bind(elseL);
        out.patch(elseL);

        if (i.elseBlock() != null) emitBlock(i.elseBlock());

        out.bind(endL);
        out.patch(endL);
    }

    private void emitWhile(WhileStmt w) {
        Label head = out.newLabel();
        Label exit = out.newLabel();

        out.bind(head);

        int cond = emitExpr(w.condition());
        out.jmpF(cond, exit);
        freeReg(cond);

        emitBlock(w.body());

        // backward jump теперь корректный: Emitter сам пропатчит, т.к. head уже bound
        out.jmp(head);

        out.bind(exit);
        out.patch(exit);
    }

    private void emitForRange(ForRangeStmt fr) {
        // for (i in from...to)  => i=from; while (i < to) { body; i=i+1; }
        pushScope();

        int iReg = allocReg();
        define(fr.varName(), iReg);

        int fromR = emitExpr(fr.from());
        out.emitABC(MOV, iReg, fromR, 0);
        freeReg(fromR);

        Label head = out.newLabel();
        Label exit = out.newLabel();

        out.bind(head);

        int toR = emitExpr(fr.to());
        int condR = allocReg();
        out.emitABC(LT_I, condR, iReg, toR);
        freeReg(toR);

        out.jmpF(condR, exit);
        freeReg(condR);

        emitBlock(fr.body());

        int one = allocReg();
        out.emitABx(LOADK, one, consts.add(new ConstPool.KInt(1)));

        int tmp = allocReg();
        out.emitABC(ADD_I, tmp, iReg, one);
        out.emitABC(MOV, iReg, tmp, 0);

        freeReg(one);
        freeReg(tmp);

        out.jmp(head);

        out.bind(exit);
        out.patch(exit);

        popScope();
    }

    private void emitForCStyle(ForStmt f) {
        pushScope();

        if (f.init() != null) emitStmt(f.init());

        Label head = out.newLabel();
        Label exit = out.newLabel();

        out.bind(head);

        if (f.condition() != null) {
            int cond = emitExpr(f.condition());
            out.jmpF(cond, exit);
            freeReg(cond);
        }

        emitBlock(f.body());

        if (f.update() != null) {
            int u = emitExpr(f.update());
            freeReg(u);
        }

        out.jmp(head);

        out.bind(exit);
        out.patch(exit);

        popScope();
    }

    // ---------- expressions ----------
    private static boolean isNumeric(Type t) {
        return t == PrimitiveType.INT || t == PrimitiveType.FLOAT;
    }

    private int emitExpr(Expr e) {
        if (e instanceof IntLiteral lit) {
            int r = allocReg();
            int id = consts.add(new ConstPool.KInt(lit.value()));
            out.emitABx(LOADK, r, id);
            return r;
        }

        if (e instanceof StringLiteral lit) {
            int r = allocReg();
            int id = consts.add(new ConstPool.KString(lit.value()));
            out.emitABx(LOADK, r, id);
            return r;
        }

        if (e instanceof BoolLiteral lit) {
            int r = allocReg();
            int id = consts.add(new ConstPool.KBool(lit.value()));
            out.emitABx(LOADK, r, id);
            return r;
        }

        if (e instanceof VarExpr v) {
            Integer r = resolve(v.name());
            if (r == null) throw new RuntimeException("Unknown var: " + v.name());
            // читаем var как значение: возвращаем регистр (без MOV)
            return r;
        }

        if (e instanceof UnaryExpr u) {
            int a = emitExpr(u.expr());
            int dst = allocReg();

            switch (u.op()) {
                case NEG -> {
                    Type t = sema.exprTypes().get(u.expr());
                    if (t == PrimitiveType.FLOAT) {
                        // гарантируем float-операнд
                        Type at = sema.exprTypes().get(u.expr());
                        if (at == PrimitiveType.INT) {
                            int af = allocReg();
                            out.emitABC(I2F, af, a, 0);
                            freeReg(a);
                            a = af;
                        }
                        int zero = allocReg();
                        out.emitABx(LOADK, zero, consts.add(new ConstPool.KFloat(0.0f)));
                        out.emitABC(SUB_F, dst, zero, a);
                        freeReg(zero);
                    } else {
                        int zero = allocReg();
                        out.emitABx(LOADK, zero, consts.add(new ConstPool.KInt(0)));
                        out.emitABC(SUB_I, dst, zero, a);
                        freeReg(zero);
                    }
                }

                case NOT -> out.emitABC(NOT, dst, a, 0);
            }

            freeReg(a);
            return dst;
        }

        if (e instanceof BinaryExpr b) return emitBinary(b);
        if (e instanceof AssignExpr a) return emitAssign(a);

        throw new RuntimeException("Unsupported expr: " + e.getClass().getSimpleName());
    }

    private int emitAssign(AssignExpr a) {
        int rhs = emitExpr(a.value());
        if (a.target() instanceof VarExpr v) {
            Integer dst = resolve(v.name());
            if (dst == null) throw new RuntimeException("Unknown var: " + v.name());
            out.emitABC(MOV, dst, rhs, 0);
            freeReg(rhs);
            return dst;
        }
        throw new RuntimeException("Unsupported assignment target (v1)");
    }

    private int emitBinary(BinaryExpr b) {
        Type lt = sema.exprTypes().get(b.left());
        Type rt = sema.exprTypes().get(b.right());

        int l = emitExpr(b.left());
        int r = emitExpr(b.right());

        boolean numericOrCompareOrEq = switch (b.op()) {
            case ADD, SUB, MUL, DIV, MOD,
                 LT, LE, GT, GE,
                 EQ, NE -> true;
            default -> false;
        };

        boolean wantFloat = numericOrCompareOrEq
                && isNumeric(lt) && isNumeric(rt)
                && (lt == PrimitiveType.FLOAT || rt == PrimitiveType.FLOAT);

        // implicit int->float
        if (wantFloat) {
            if (lt == PrimitiveType.INT) {
                int lf = allocReg();
                out.emitABC(I2F, lf, l, 0);
                freeReg(l);
                l = lf;
            }
            if (rt == PrimitiveType.INT) {
                int rf = allocReg();
                out.emitABC(I2F, rf, r, 0);
                freeReg(r);
                r = rf;
            }
        }

        int dst = allocReg();
        switch (b.op()) {
            case ADD -> out.emitABC(wantFloat ? ADD_F : ADD_I, dst, l, r);
            case SUB -> out.emitABC(wantFloat ? SUB_F : SUB_I, dst, l, r);
            case MUL -> out.emitABC(wantFloat ? MUL_F : MUL_I, dst, l, r);
            case DIV -> out.emitABC(wantFloat ? DIV_F : DIV_I, dst, l, r);
            case MOD -> out.emitABC(wantFloat ? MOD_F : MOD_I, dst, l, r);

            case LT -> out.emitABC(wantFloat ? LT_F : LT_I, dst, l, r);
            case LE -> out.emitABC(wantFloat ? LE_F : LE_I, dst, l, r);
            case GT -> out.emitABC(wantFloat ? GT_F : GT_I, dst, l, r);
            case GE -> out.emitABC(wantFloat ? GE_F : GE_I, dst, l, r);

            case EQ -> out.emitABC(EQ, dst, l, r);
            case NE -> out.emitABC(NE, dst, l, r);

            case AND -> out.emitABC(AND, dst, l, r);
            case OR  -> out.emitABC(OR, dst, l, r);
        }

        freeReg(l);
        freeReg(r);
        return dst;
    }
}