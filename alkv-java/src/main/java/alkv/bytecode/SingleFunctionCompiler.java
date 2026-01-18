package alkv.bytecode;

import alkv.ast.decl.FunctionDecl;
import alkv.ast.expr.*;
import alkv.ast.stmt.*;
import alkv.sema.TypeChecker;
import alkv.types.PrimitiveType;
import alkv.types.Type;

import java.util.*;

import static alkv.bytecode.Opcode.*;

/**
 * Компилятор одной функции в байткод (register-based VM).
 *
 * ВАЖНО:
 * 1) Для полей/методов/классов здесь ожидается, что семантика умеет:
 *    - sema.exprTypes().get(expr) -> Type
 *    - sema.classOfExpr(expr)     -> String (имя класса для object-expr)
 *
 * 2) Для вызова функций используется простая calling convention:
 *    - аргументы перед CALLK/CALL_NATIVE кладутся в R0..R(argc-1)
 *    - перед кладкой мы сохраняем "живые" регистры из R0..R(argc-1), если они принадлежат scope.
 */
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
        for (FunctionDecl.Param p : fn.params()) {
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
        for (Stmt st : b.statements()) emitStmt(st);
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
            case SwitchStmt sw -> emitSwitch(sw);

            case WhileStmt w -> emitWhile(w);
            case ForRangeStmt fr -> emitForRange(fr);
            case ForStmt f -> emitForCStyle(f);
        }
    }

    private void emitIf(IfStmt i) {
        Label end = out.newLabel();

        List<IfStmt.Branch> branches = i.branches();
        List<Label> nextLabels = new ArrayList<>(branches.size());
        for (int idx = 0; idx < branches.size(); idx++) nextLabels.add(out.newLabel());

        for (int idx = 0; idx < branches.size(); idx++) {
            IfStmt.Branch br = branches.get(idx);
            Label next = nextLabels.get(idx);

            int cond = emitExpr(br.condition());
            out.jmpF(cond, next);
            freeReg(cond);

            emitBlock(br.body());
            out.jmp(end);

            out.bind(next);
            out.patch(next);
        }

        if (i.elseBlock() != null) emitBlock(i.elseBlock());

        out.bind(end);
        out.patch(end);
    }

    private void emitSwitch(SwitchStmt sw) {
        Label end = out.newLabel();

        int subj = emitExpr(sw.subject());

        List<Label> caseLabels = new ArrayList<>();
        for (int i = 0; i < sw.cases().size(); i++) caseLabels.add(out.newLabel());
        Label defaultLabel = out.newLabel();

        // dispatch: if (subj == case.match) goto caseLabel
        for (int i = 0; i < sw.cases().size(); i++) {
            SwitchStmt.Case cs = sw.cases().get(i);

            int m = emitExpr(cs.match());
            int eq = allocReg();
            out.emitABC(EQ, eq, subj, m);
            freeReg(m);

            out.jmpT(eq, caseLabels.get(i));
            freeReg(eq);
        }

        out.jmp(defaultLabel);

        // bodies
        for (int i = 0; i < sw.cases().size(); i++) {
            out.bind(caseLabels.get(i));
            out.patch(caseLabels.get(i));
            emitBlock(sw.cases().get(i).body());
            out.jmp(end);
        }

        out.bind(defaultLabel);
        out.patch(defaultLabel);
        if (sw.defaultBlock() != null) emitBlock(sw.defaultBlock());

        out.bind(end);
        out.patch(end);

        freeReg(subj);
    }

    private void emitWhile(WhileStmt w) {
        Label head = out.newLabel();
        Label exit = out.newLabel();

        out.bind(head);

        int cond = emitExpr(w.condition());
        out.jmpF(cond, exit);
        freeReg(cond);

        emitBlock(w.body());

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
        // --- literals ---
        if (e instanceof IntLiteral lit) {
            int r = allocReg();
            out.emitABx(LOADK, r, consts.add(new ConstPool.KInt(lit.value())));
            return r;
        }

        if (e instanceof StringLiteral lit) {
            int r = allocReg();
            out.emitABx(LOADK, r, consts.add(new ConstPool.KString(lit.value())));
            return r;
        }

        if (e instanceof BoolLiteral lit) {
            int r = allocReg();
            out.emitABx(LOADK, r, consts.add(new ConstPool.KBool(lit.value())));
            return r;
        }

        // --- variable ---
        if (e instanceof VarExpr v) {
            Integer r = resolve(v.name());
            if (r == null) throw new RuntimeException("Unknown var: " + v.name());
            // возвращаем регистр переменной (не MOV)
            return r;
        }

        // --- array literal ---
        if (e instanceof ArrayLiteralExpr al) {
            int sizeR = allocReg();
            out.emitABx(LOADK, sizeR, consts.add(new ConstPool.KInt(al.elements().size())));

            int arr = allocReg();
            out.emitABC(NEW_ARR, arr, sizeR, 0);
            freeReg(sizeR);

            for (int i = 0; i < al.elements().size(); i++) {
                int idx = allocReg();
                out.emitABx(LOADK, idx, consts.add(new ConstPool.KInt(i)));

                int val = emitExpr(al.elements().get(i));
                out.emitABC(SET_ELEM, arr, idx, val);

                freeReg(idx);
                freeReg(val);
            }

            return arr;
        }

        // --- array access ---
        if (e instanceof ArrayAccessExpr aa) {
            int arr = emitExpr(aa.array());
            int idx = emitExpr(aa.index());
            int dst = allocReg();
            out.emitABC(GET_ELEM, dst, arr, idx);
            freeReg(arr);
            freeReg(idx);
            return dst;
        }

        // --- field access ---
        if (e instanceof FieldAccessExpr fa) {
            int obj = emitExpr(fa.target());

            int fieldIdReg = allocReg();
            String cls = sema.classOfExpr(fa.target()); // требуется от sema
            int fid = consts.add(new ConstPool.KField(cls, fa.field()));
            out.emitABx(LOADK, fieldIdReg, fid);

            int dst = allocReg();
            out.emitABC(GET_FIELD, dst, obj, fieldIdReg);

            freeReg(fieldIdReg);
            freeReg(obj);
            return dst;
        }

        // --- new object ---
        if (e instanceof NewExpr ne) {
            int obj = allocReg();
            int cid = consts.add(new ConstPool.KClass(ne.className()));
            out.emitABx(NEW_OBJ, obj, cid);

            // ctor: Class.<init>(this, args...) ; модель: возвращает this (или игнорируем return)
            List<Integer> args = new ArrayList<>();
            args.add(obj);
            for (Expr a : ne.args()) args.add(emitExpr(a));

            int tmpRet = emitCallByName(ne.className() + ".<init>", args, true);
            freeReg(tmpRet);

            // освобождаем temps аргументов (obj оставляем как результат выражения)
            for (int i = 1; i < args.size(); i++) freeReg(args.get(i));

            return obj;
        }

        // --- unary ---
        if (e instanceof UnaryExpr u) {
            int a = emitExpr(u.expr());
            int dst = allocReg();

            switch (u.op()) {
                case NOT -> out.emitABC(NOT, dst, a, 0);

                case NEG -> {
                    Type operandType = sema.exprTypes().get(u.expr());
                    if (operandType == PrimitiveType.FLOAT) {
                        // -x => 0.0 - x
                        int zero = allocReg();
                        out.emitABx(LOADK, zero, consts.add(new ConstPool.KFloat(0.0f)));
                        out.emitABC(SUB_F, dst, zero, a);
                        freeReg(zero);
                    } else {
                        // -x => 0 - x
                        int zero = allocReg();
                        out.emitABx(LOADK, zero, consts.add(new ConstPool.KInt(0)));
                        out.emitABC(SUB_I, dst, zero, a);
                        freeReg(zero);
                    }
                }
            }

            freeReg(a);
            return dst;
        }

        // --- call ---
        if (e instanceof CallExpr c) {
            return emitCall(c);
        }

        // --- binary / assign ---
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

        if (a.target() instanceof ArrayAccessExpr aa) {
            int arr = emitExpr(aa.array());
            int idx = emitExpr(aa.index());
            out.emitABC(SET_ELEM, arr, idx, rhs);
            freeReg(idx);
            freeReg(rhs);
            return arr;
        }

        if (a.target() instanceof FieldAccessExpr fa) {
            int obj = emitExpr(fa.target());

            int fieldIdReg = allocReg();
            String cls = sema.classOfExpr(fa.target()); // требуется от sema
            int fid = consts.add(new ConstPool.KField(cls, fa.field()));
            out.emitABx(LOADK, fieldIdReg, fid);

            out.emitABC(SET_FIELD, obj, fieldIdReg, rhs);

            freeReg(fieldIdReg);
            freeReg(rhs);
            return obj;
        }

        throw new RuntimeException("Unsupported assignment target");
    }

    private int emitBinary(BinaryExpr b) {
        // short-circuit for && and ||
        switch (b.op()) {
            case AND -> { return emitLazyAnd(b.left(), b.right()); }
            case OR  -> { return emitLazyOr(b.left(), b.right()); }
            default  -> {}
        }

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

            // AND/OR обработаны выше
            case AND, OR -> throw new IllegalStateException("Unreachable");
        }

        freeReg(l);
        freeReg(r);
        return dst;
    }

    private int emitLazyAnd(Expr left, Expr right) {
        Label falseL = out.newLabel();
        Label endL = out.newLabel();

        int dst = allocReg();

        int l = emitExpr(left);
        out.jmpF(l, falseL);
        freeReg(l);

        int r = emitExpr(right);
        out.emitABC(MOV, dst, r, 0);
        freeReg(r);

        out.jmp(endL);

        out.bind(falseL);
        out.patch(falseL);

        int f = allocReg();
        out.emitABx(LOADK, f, consts.add(new ConstPool.KBool(false)));
        out.emitABC(MOV, dst, f, 0);
        freeReg(f);

        out.bind(endL);
        out.patch(endL);

        return dst;
    }

    private int emitLazyOr(Expr left, Expr right) {
        Label trueL = out.newLabel();
        Label endL = out.newLabel();

        int dst = allocReg();

        int l = emitExpr(left);
        out.jmpT(l, trueL);
        freeReg(l);

        int r = emitExpr(right);
        out.emitABC(MOV, dst, r, 0);
        freeReg(r);

        out.jmp(endL);

        out.bind(trueL);
        out.patch(trueL);

        int t = allocReg();
        out.emitABx(LOADK, t, consts.add(new ConstPool.KBool(true)));
        out.emitABC(MOV, dst, t, 0);
        freeReg(t);

        out.bind(endL);
        out.patch(endL);

        return dst;
    }

    // ---------- calls ----------
    private int emitCall(CallExpr c) {
        // builtin ochev.*
        if (c.callee() instanceof FieldAccessExpr fa
                && fa.target() instanceof VarExpr ve
                && ve.name().equals("ochev")) {

            int nativeId = switch (fa.field()) {
                case "Out" -> 1;
                case "In" -> 2;
                case "TudaSyuda" -> 3;
                case ">>>" -> 4;
                case "<<<" -> 5;
                default -> throw new RuntimeException("Unknown ochev function: " + fa.field());
            };

            List<Integer> args = new ArrayList<>();
            for (Expr a : c.args()) args.add(emitExpr(a));

            int dst = allocReg();
            // кладём args в R0..R(argc-1), CALL_NATIVE читает их оттуда
            var saved = placeArgsIntoR0(args);
            out.emitABC(CALL_NATIVE, dst, nativeId, args.size());
            restorePlacedArgs(saved);

            for (int r : args) freeReg(r);
            return dst;
        }

        // method call: obj.method(args...)
        if (c.callee() instanceof FieldAccessExpr fa) {
            int obj = emitExpr(fa.target());

            List<Integer> args = new ArrayList<>();
            args.add(obj); // this
            for (Expr a : c.args()) args.add(emitExpr(a));

            String cls = sema.classOfExpr(fa.target()); // требуется от sema
            String mangled = cls + "." + fa.field();

            int dst = emitCallByName(mangled, args, true);

            for (int ar : args) freeReg(ar);
            return dst;
        }

        // plain function: f(args...)
        if (c.callee() instanceof VarExpr v) {
            List<Integer> args = new ArrayList<>();
            for (Expr a : c.args()) args.add(emitExpr(a));
            int dst = emitCallByName(v.name(), args, true);
            for (int ar : args) freeReg(ar);
            return dst;
        }

        throw new RuntimeException("Unsupported call callee: " + c.callee().getClass().getSimpleName());
    }

    /**
     * CALLK: вызываем функцию по имени (KFunc), args передаются через R0..R(n-1)
     */
    private int emitCallByName(String fnName, List<Integer> argRegs, boolean returnsValue) {
        int dst = allocReg();

        int k = consts.add(new ConstPool.KFunc(fnName, argRegs.size()));

        var saved = placeArgsIntoR0(argRegs);
        out.emitABx(CALLK, dst, k);
        restorePlacedArgs(saved);

        if (!returnsValue) {
            freeReg(dst);
            return 255;
        }
        return dst;
    }

    /**
     * Сохраняет "живые" регистры из диапазона [0..argc-1], которые будут перезатёрты,
     * затем MOV-ит аргументы в R0..R(argc-1).
     *
     * Возвращает список сохранений (dstReg, savedToTemp).
     */
    private List<int[]> placeArgsIntoR0(List<Integer> argRegs) {
        int argc = argRegs.size();
        List<int[]> saved = new ArrayList<>();

        // 1) сохраняем живые R0..R(argc-1), если они принадлежат scope
        for (int i = 0; i < argc; i++) {
            if (!isCapturedByScopes(i)) continue; // не "живой" локал/параметр -> можно затирать
            int tmp = allocReg();
            out.emitABC(MOV, tmp, i, 0);
            saved.add(new int[]{i, tmp});
        }

        // 2) кладём аргументы
        for (int i = 0; i < argc; i++) {
            int src = argRegs.get(i);
            out.emitABC(MOV, i, src, 0);
        }

        return saved;
    }

    private void restorePlacedArgs(List<int[]> saved) {
        // восстанавливаем в обратном порядке
        for (int i = saved.size() - 1; i >= 0; i--) {
            int[] pair = saved.get(i);
            int dstReg = pair[0];
            int tmp = pair[1];
            out.emitABC(MOV, dstReg, tmp, 0);
            freeReg(tmp);
        }
    }
}