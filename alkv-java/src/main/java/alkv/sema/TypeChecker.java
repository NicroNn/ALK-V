package alkv.sema;

import alkv.ast.Program;
import alkv.ast.decl.ClassDecl;
import alkv.ast.decl.FunctionDecl;
import alkv.ast.expr.*;
import alkv.ast.stmt.*;
import alkv.types.*;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class TypeChecker {

    public record Result(
            SymbolTable globals,
            Map<Expr, Type> exprTypes
    ) {
        /**
         * Нужно компилятору байткода для FieldAccess/MethodCall, чтобы получить имя класса receiver-а.
         * Работает, если typeOf(receiver) возвращает ClassType.
         */
        public String classOfExpr(Expr e) {
            Type t = exprTypes.get(e);
            if (t == null) {
                throw new IllegalStateException("classOfExpr: expr type not computed: " + e);
            }
            if (t instanceof ClassType ct) return ct.name();
            throw new IllegalStateException("classOfExpr: expected ClassType, got: " + t);
        }
    }

    private final SymbolTable globals = new SymbolTable();
    private final SymbolTable locals = new SymbolTable(); // стек для функций/блоков
    private final Map<Expr, Type> exprTypes = new HashMap<>();

    private TypeResolver typeResolver;
    private Type currentReturnType = PrimitiveType.VOID;

    public Result check(Program program) {
        // 1) predeclare classes
        for (ClassDecl c : program.classes()) {
            globals.define(new ClassSymbol(c.name()));
        }

        // 2) predeclare functions
        typeResolver = new TypeResolver(globals);
        for (FunctionDecl f : program.functions()) {
            List<Type> ps = new ArrayList<>();
            for (FunctionDecl.Param p : f.params()) {
                ps.add(typeResolver.resolve(p.type()));
            }
            Type rt = typeResolver.resolve(f.returnType());
            globals.define(new FuncSymbol(f.name(), ps, rt));
        }

        // 3) typecheck function bodies
        for (FunctionDecl f : program.functions()) {
            checkFunction(f);
        }

        // (классы/методы/конструкторы — позже аналогично)
        return new Result(globals, exprTypes);
    }

    private void checkFunction(FunctionDecl f) {
        FuncSymbol fs = (FuncSymbol) globals.lookup(f.name());
        currentReturnType = fs.returnType();

        locals.push(); // function scope

        // params
        int i = 0;
        for (FunctionDecl.Param p : f.params()) {
            locals.define(new VarSymbol(p.name(), fs.paramTypes().get(i++)));
        }

        checkBlock(f.body());

        locals.pop();
        currentReturnType = PrimitiveType.VOID;
    }

    private void checkBlock(BlockStmt b) {
        locals.push();
        for (Stmt st : b.statements()) checkStmt(st);
        locals.pop();
    }

    private void checkStmt(Stmt s) {
        switch (s) {
            case BlockStmt b -> checkBlock(b);

            case VarDeclStmt v -> {
                Type declared = typeResolver.resolve(v.type());
                if (v.initializer() != null) {
                    Type initT = typeOf(v.initializer());
                    if (!TypeUtil.isAssignable(declared, initT)) {
                        throw new SemanticException("Cannot assign " + initT + " to variable '" + v.name() + "' of type " + declared);
                    }
                }
                locals.define(new VarSymbol(v.name(), declared));
            }

            case ExprStmt e -> typeOf(e.expr());

            case IfStmt i -> {
                // NEW IfStmt: branches + elseBlock
                for (IfStmt.Branch br : i.branches()) {
                    requireBool(typeOf(br.condition()), "if condition");
                    checkBlock(br.body());
                }
                if (i.elseBlock() != null) checkBlock(i.elseBlock());
            }

            case SwitchStmt sw -> {
                Type subjT = typeOf(sw.subject());

                for (SwitchStmt.Case cs : sw.cases()) {
                    Type matchT = typeOf(cs.match());
                    // базовая проверка: тип case должен быть совместим с subject
                    // (можно ужесточить позже)
                    if (!TypeUtil.isAssignable(subjT, matchT) && !TypeUtil.isAssignable(matchT, subjT)) {
                        throw new SemanticException("switch case type mismatch: subject=" + subjT + ", case=" + matchT);
                    }
                    checkBlock(cs.body());
                }

                if (sw.defaultBlock() != null) checkBlock(sw.defaultBlock());
            }

            case WhileStmt w -> {
                requireBool(typeOf(w.condition()), "while condition");
                checkBlock(w.body());
            }

            case ForRangeStmt fr -> {
                // i in from...to : i считается int, from/to должны быть int
                Type fromT = typeOf(fr.from());
                Type toT = typeOf(fr.to());
                if (fromT != PrimitiveType.INT || toT != PrimitiveType.INT) {
                    throw new SemanticException("for-range bounds must be int");
                }

                locals.push();
                locals.define(new VarSymbol(fr.varName(), PrimitiveType.INT));
                checkBlock(fr.body());
                locals.pop();
            }

            case ForStmt f -> {
                locals.push();

                if (f.init() != null) checkStmt(f.init());

                if (f.condition() != null) {
                    requireBool(typeOf(f.condition()), "for condition");
                }

                checkBlock(f.body());

                if (f.update() != null) typeOf(f.update());

                locals.pop();
            }

            case ReturnStmt r -> {
                if (r.value() == null) {
                    if (currentReturnType != PrimitiveType.VOID) {
                        throw new SemanticException("Return without value in non-void function");
                    }
                } else {
                    Type t = typeOf(r.value());
                    if (!TypeUtil.isAssignable(currentReturnType, t)) {
                        throw new SemanticException("Return type mismatch: expected " + currentReturnType + ", got " + t);
                    }
                }
            }

            default -> throw new SemanticException("Unsupported statement: " + s.getClass().getSimpleName());
        }
    }

    private void requireBool(Type t, String ctx) {
        if (t != PrimitiveType.BOOL) throw new SemanticException(ctx + " must be bool");
    }

    private Type typeOf(Expr e) {
        Type cached = exprTypes.get(e);
        if (cached != null) return cached;

        Type t;
        switch (e) {
            case IntLiteral ignored -> t = PrimitiveType.INT;
            case StringLiteral ignored -> t = PrimitiveType.STRING;
            case BoolLiteral ignored -> t = PrimitiveType.BOOL;

            case VarExpr v -> {
                Symbol sym = locals.lookup(v.name());
                if (sym == null) sym = globals.lookup(v.name());
                if (sym instanceof VarSymbol vs) {
                    t = vs.type();
                } else {
                    throw new SemanticException("Unknown variable: " + v.name());
                }
            }

            case UnaryExpr u -> {
                Type a = typeOf(u.expr());
                t = switch (u.op()) {
                    case NEG -> {
                        if (!a.isNumeric()) throw new SemanticException("Unary '-' expects number");
                        yield a;
                    }
                    case NOT -> {
                        if (a != PrimitiveType.BOOL) throw new SemanticException("Unary '!' expects bool");
                        yield PrimitiveType.BOOL;
                    }
                };
            }

            case BinaryExpr b -> t = typeBinary(b);

            case AssignExpr a -> {
                Type rt = typeOf(a.value());

                if (a.target() instanceof VarExpr v) {
                    Symbol sym = locals.lookup(v.name());
                    if (sym == null) sym = globals.lookup(v.name());
                    if (!(sym instanceof VarSymbol vs)) throw new SemanticException("Unknown variable: " + v.name());

                    if (!TypeUtil.isAssignable(vs.type(), rt)) {
                        throw new SemanticException("Cannot assign " + rt + " to " + vs.type());
                    }
                    t = vs.type();
                } else if (a.target() instanceof ArrayAccessExpr arr) {
                    Type arrT = typeOf(arr.array());
                    if (!(arrT instanceof ArrayType at)) throw new SemanticException("Indexing non-array");
                    if (typeOf(arr.index()) != PrimitiveType.INT) throw new SemanticException("Array index must be int");
                    if (!TypeUtil.isAssignable(at.element(), rt)) throw new SemanticException("Cannot assign " + rt + " to " + at.element());
                    t = at.element();
                } else if (a.target() instanceof FieldAccessExpr f) {
                    // Заглушка (пока нет таблицы полей классов)
                    Type recv = typeOf(f.target());
                    if (!(recv instanceof ClassType)) throw new SemanticException("Field assignment on non-class value");
                    // тип результата присваивания = тип RHS (как в большинстве языков)
                    t = rt;
                } else {
                    throw new SemanticException("Invalid assignment target");
                }
            }

            case ArrayAccessExpr a -> {
                Type arrT = typeOf(a.array());
                if (!(arrT instanceof ArrayType at)) throw new SemanticException("Indexing non-array");
                if (typeOf(a.index()) != PrimitiveType.INT) throw new SemanticException("Array index must be int");
                t = at.element();
            }

            case ArrayLiteralExpr al -> {
                if (al.elements().isEmpty()) {
                    throw new SemanticException("Cannot infer type of empty array literal []");
                }
                Type elemT = typeOf(al.elements().get(0));
                for (int i = 1; i < al.elements().size(); i++) {
                    Type cur = typeOf(al.elements().get(i));
                    // разрешим числовой промоушн int/float
                    Type numRes = TypeUtil.numericResult(elemT, cur);
                    if (numRes != null) {
                        elemT = numRes;
                    } else if (!elemT.equals(cur)) {
                        throw new SemanticException("Array literal elements must have same type. Got " + elemT + " and " + cur);
                    }
                }
                t = new ArrayType(elemT, al.elements().size());
            }

            case NewExpr ne -> {
                // достаточно проверить, что класс объявлен
                Symbol sym = globals.lookup(ne.className());
                if (!(sym instanceof ClassSymbol)) {
                    throw new SemanticException("Unknown class: " + ne.className());
                }
                // args/ctor пока не проверяем (нужна информация о конструкторах)
                for (Expr arg : ne.args()) typeOf(arg);
                t = new ClassType(ne.className());
            }

            case FieldAccessExpr f -> {
                Type recv = typeOf(f.target());
                if (!(recv instanceof ClassType)) throw new SemanticException("Field access on non-class value");
                // TODO: когда появится ClassSymbol с полями — вернуть реальный тип поля
                t = PrimitiveType.INT; // заглушка
            }

            case CallExpr c -> t = typeCall(c);

            default -> throw new SemanticException("Unsupported expr: " + e.getClass().getSimpleName());
        }

        exprTypes.put(e, t);
        return t;
    }

    private Type typeCall(CallExpr c) {
        // 1) ochev.*
        if (c.callee() instanceof FieldAccessExpr fa
                && fa.target() instanceof VarExpr ve
                && ve.name().equals("ochev")) {

            // просто типизируем аргументы
            for (Expr a : c.args()) typeOf(a);

            return switch (fa.field()) {
                case "Out" -> PrimitiveType.VOID;
                case "In" -> PrimitiveType.VOID;
                case "TudaSyuda" -> PrimitiveType.VOID;
                case ">>>" -> {
                    // max(a,b) => numeric
                    if (c.args().size() != 2) throw new SemanticException("ochev.>>>(a,b) expects 2 args");
                    Type a = typeOf(c.args().get(0));
                    Type b = typeOf(c.args().get(1));
                    Type res = TypeUtil.numericResult(a, b);
                    if (res == null) throw new SemanticException("ochev.>>> expects numbers");
                    yield res;
                }
                case "<<<" -> {
                    if (c.args().size() != 2) throw new SemanticException("ochev.<<<(a,b) expects 2 args");
                    Type a = typeOf(c.args().get(0));
                    Type b = typeOf(c.args().get(1));
                    Type res = TypeUtil.numericResult(a, b);
                    if (res == null) throw new SemanticException("ochev.<<< expects numbers");
                    yield res;
                }
                default -> throw new SemanticException("Unknown ochev function: " + fa.field());
            };
        }

        // 2) вызов обычной функции по имени: foo(...)
        if (c.callee() instanceof VarExpr v) {
            Symbol sym = globals.lookup(v.name());
            if (!(sym instanceof FuncSymbol fs)) throw new SemanticException("Unknown function: " + v.name());

            if (fs.paramTypes().size() != c.args().size()) {
                throw new SemanticException("Arity mismatch for " + v.name());
            }

            for (int i = 0; i < c.args().size(); i++) {
                Type argT = typeOf(c.args().get(i));
                Type paramT = fs.paramTypes().get(i);
                if (!TypeUtil.isAssignable(paramT, argT)) {
                    throw new SemanticException("Argument " + i + " type mismatch: expected " + paramT + ", got " + argT);
                }
            }

            return fs.returnType();
        }

        // 3) метод: obj.method(...) — пока заглушка (нужны ClassSymbol.methods)
        if (c.callee() instanceof FieldAccessExpr fa) {
            Type recvT = typeOf(fa.target());
            if (!(recvT instanceof ClassType)) {
                throw new SemanticException("Call target is not a function/method: " + c.callee());
            }
            // typecheck args хотя бы
            for (Expr a : c.args()) typeOf(a);
            // TODO: lookup method in class symbol table
            return PrimitiveType.INT; // заглушка
        }

        throw new SemanticException("Unsupported call target (yet)");
    }

    private Type typeBinary(BinaryExpr b) {
        Type l = typeOf(b.left());
        Type r = typeOf(b.right());
        return switch (b.op()) {
            case ADD, SUB, MUL, DIV, MOD -> {
                Type res = TypeUtil.numericResult(l, r);
                if (res == null) throw new SemanticException("Arithmetic expects numbers");
                yield res;
            }
            case LT, LE, GT, GE -> {
                Type res = TypeUtil.numericResult(l, r);
                if (res == null) throw new SemanticException("Comparison expects numbers");
                yield PrimitiveType.BOOL;
            }
            case EQ, NE -> {
                // допускаем сравнение одинаковых типов + int vs float (с промоушном)
                if (l.equals(r)) yield PrimitiveType.BOOL;
                if ((l == PrimitiveType.INT && r == PrimitiveType.FLOAT) || (l == PrimitiveType.FLOAT && r == PrimitiveType.INT))
                    yield PrimitiveType.BOOL;
                throw new SemanticException("Cannot compare " + l + " and " + r);
            }
            case AND, OR -> {
                if (l != PrimitiveType.BOOL || r != PrimitiveType.BOOL) throw new SemanticException("Logical op expects bool");
                yield PrimitiveType.BOOL;
            }
        };
    }
}