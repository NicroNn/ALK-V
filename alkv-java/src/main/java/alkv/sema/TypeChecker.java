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
    ) {}

    private final SymbolTable globals = new SymbolTable();
    private final SymbolTable locals = new SymbolTable(); // отдельный стек для функций/блоков
    private final Map<Expr, Type> exprTypes = new HashMap<>();
    private TypeResolver typeResolver;

    private Type currentReturnType = PrimitiveType.VOID;

    public Result check(Program program) {
        // 1) predeclare classes
        for (Object o : program.classes()) {
            ClassDecl c = (ClassDecl) o;
            globals.define(new ClassSymbol(c.name()));
        }

        // 2) predeclare functions
        typeResolver = new TypeResolver(globals);
        for (Object o : program.functions()) {
            FunctionDecl f = (FunctionDecl) o;
            List<Type> ps = new ArrayList<>();
            for (Object po : f.params()) {
                FunctionDecl.Param p = (FunctionDecl.Param) po;
                ps.add(typeResolver.resolve(p.type()));
            }
            Type rt = typeResolver.resolve(f.returnType());
            globals.define(new FuncSymbol(f.name(), ps, rt));
        }

        // 3) typecheck function bodies
        for (Object o : program.functions()) {
            checkFunction((FunctionDecl) o);
        }

        // (классы/методы позже можно так же прогонять через checkMethod)

        return new Result(globals, exprTypes);
    }

    private void checkFunction(FunctionDecl f) {
        FuncSymbol fs = (FuncSymbol) globals.lookup(f.name());
        currentReturnType = fs.returnType();

        locals.push(); // function scope

        // params
        int i = 0;
        for (Object po : f.params()) {
            FunctionDecl.Param p = (FunctionDecl.Param) po;
            locals.define(new VarSymbol(p.name(), fs.paramTypes().get(i++)));
        }

        checkBlock(f.body());

        locals.pop();
        currentReturnType = PrimitiveType.VOID;
    }

    private void checkBlock(BlockStmt b) {
        locals.push();
        for (Object so : b.statements()) checkStmt((Stmt) so);
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
                requireBool(typeOf(i.condition()), "if condition");
                checkBlock(i.thenBlock());
                if (i.elseBlock() != null) checkBlock(i.elseBlock());
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
                requireBool(typeOf(f.condition()), "for condition");
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
        }
    }

    private void requireBool(Type t, String ctx) {
        if (t != PrimitiveType.BOOL) throw new SemanticException(ctx + " must be bool");
    }

    private Type typeOf(Expr e) {
        Type cached = exprTypes.get(e);
        if (cached != null) return cached;

        Type t = switch (e) {
            case IntLiteral ignored -> PrimitiveType.INT;
            case StringLiteral ignored -> PrimitiveType.STRING;
            case BoolLiteral ignored -> PrimitiveType.BOOL;

            case VarExpr v -> {
                Symbol sym = locals.lookup(v.name());
                if (sym == null) sym = globals.lookup(v.name());
                if (sym instanceof VarSymbol vs) yield vs.type();
                throw new SemanticException("Unknown variable: " + v.name());
            }

            case UnaryExpr u -> {
                Type a = typeOf(u.expr());
                yield switch (u.op()) {
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

            case BinaryExpr b -> typeBinary(b);

            case AssignExpr a -> {
                Type rt = typeOf(a.value());
                // разрешаем var / field / array как target (как в парсере) [file:30]
                if (a.target() instanceof VarExpr v) {
                    Symbol sym = locals.lookup(v.name());
                    if (!(sym instanceof VarSymbol vs)) throw new SemanticException("Unknown variable: " + v.name());
                    if (!TypeUtil.isAssignable(vs.type(), rt)) {
                        throw new SemanticException("Cannot assign " + rt + " to " + vs.type());
                    }
                    yield vs.type();
                }
                if (a.target() instanceof ArrayAccessExpr arr) {
                    Type arrT = typeOf(arr.array());
                    if (!(arrT instanceof ArrayType at)) throw new SemanticException("Indexing non-array");
                    if (typeOf(arr.index()) != PrimitiveType.INT) throw new SemanticException("Array index must be int");
                    if (!TypeUtil.isAssignable(at.element(), rt)) throw new SemanticException("Cannot assign " + rt + " to " + at.element());
                    yield at.element();
                }
                if (a.target() instanceof FieldAccessExpr) {
                    // полноценная проверка полей добавится, когда подключишь классы в sema полностью
                    yield rt;
                }
                throw new SemanticException("Invalid assignment target");
            }

            case ArrayAccessExpr a -> {
                Type arrT = typeOf(a.array());
                if (!(arrT instanceof ArrayType at)) throw new SemanticException("Indexing non-array");
                if (typeOf(a.index()) != PrimitiveType.INT) throw new SemanticException("Array index must be int");
                yield at.element();
            }

            case FieldAccessExpr f -> {
                // полноценная проверка по ClassSymbol полям добавится на этапе OOP семантики
                // пока хотя бы проверим, что receiver — class
                Type recv = typeOf(f.target());
                if (!(recv instanceof ClassType)) throw new SemanticException("Field access on non-class value");
                yield PrimitiveType.INT; // заглушка до привязки ClassSymbol.fields
            }

            case CallExpr c -> {
                // v1: поддерживаем только вызов по имени функции: foo(...)
                if (c.callee() instanceof VarExpr v) {
                    Symbol sym = globals.lookup(v.name());
                    if (!(sym instanceof FuncSymbol fs)) throw new SemanticException("Unknown function: " + v.name());
                    if (fs.paramTypes().size() != c.args().size()) throw new SemanticException("Arity mismatch for " + v.name());
                    for (int i = 0; i < c.args().size(); i++) {
                        Type argT = typeOf((Expr) c.args().get(i));
                        Type paramT = fs.paramTypes().get(i);
                        if (!TypeUtil.isAssignable(paramT, argT)) {
                            throw new SemanticException("Argument " + i + " type mismatch: expected " + paramT + ", got " + argT);
                        }
                    }
                    yield fs.returnType();
                }
                throw new SemanticException("Unsupported call target (yet)");
            }
        };

        exprTypes.put(e, t);
        return t;
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
