package alkv.parser;

import alkv.ast.Program;
import alkv.ast.decl.FunctionDecl;
import alkv.ast.expr.*;
import alkv.ast.stmt.*;
import alkv.ast.type.*;
import alkv.lexer.Lexer;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

public class ParserTest {

    private static Program parse(String src) {
        var tokens = new Lexer(src).tokenize();
        return new Parser(tokens).parseProgram();
    }

    private static FunctionDecl firstFn(Program p) {
        return (FunctionDecl) p.functions().getFirst();
    }

    @Test
    void parse_program_with_function_and_class() {
        var p = parse("""
            class A { public: x: int; }
            fnc main : void() { return; }
            """);
        assertEquals(1, p.classes().size());   // класс пока заглушка, но учитывается [file:23]
        assertEquals(1, p.functions().size());
    }

    @Test
    void parse_function_params_and_return_type() {
        var p = parse("""
            fnc f : int(a:int, b:float) { return 1; }
            """);
        var f = firstFn(p);
        assertEquals("f", f.name());
        assertTrue(f.returnType() instanceof PrimitiveTypeRef);
        assertEquals(2, f.params().size()); // raw List в FunctionDecl [file:18]
    }

    @Test
    void parse_type_arrays() {
        var p = parse("""
            fnc f : void(a:int[3], b:int[]) { return; }
            """);
        var f = firstFn(p);
        var p0 = (FunctionDecl.Param) f.params().get(0);
        var p1 = (FunctionDecl.Param) f.params().get(1);

        assertTrue(p0.type() instanceof ArrayTypeRef);
        assertEquals(3, ((ArrayTypeRef)p0.type()).size()); // [3] [file:23]
        assertNull(((ArrayTypeRef)p1.type()).size());      // [] => null [file:23]
    }

    @Test
    void parse_var_decl_stmt_with_init_and_without() {
        var p = parse("""
            fnc f : void() {
              x:int;
              y:int = 10;
              return;
            }
            """);
        var f = firstFn(p);
        var stmts = f.body().statements();
        assertTrue(stmts.get(0) instanceof VarDeclStmt);
        assertTrue(stmts.get(1) instanceof VarDeclStmt);
        assertNotNull(((VarDeclStmt) stmts.get(1)).initializer());
    }

    @Test
    void parse_if_else_and_else_if_desugared() {
        var p = parse("""
            fnc f : void() {
              if (T) { } else if (F) { } else { }
              return;
            }
            """);
        var ifs = (IfStmt) firstFn(p).body().statements().get(0);
        assertNotNull(ifs.elseBlock());
        // else-if превращается в else { if (...) { ... } } [file:23]
        assertTrue(ifs.elseBlock().statements().get(0) instanceof IfStmt);
    }

    @Test
    void parse_while_stmt() {
        var p = parse("""
            fnc f : void() {
              while (T) { }
              return;
            }
            """);
        assertTrue(firstFn(p).body().statements().get(0) instanceof WhileStmt);
    }

    @Test
    void parse_for_range_stmt() {
        var p = parse("""
            fnc f : void() {
              for (i in 0...10) { }
              return;
            }
            """);
        var fr = (ForRangeStmt) firstFn(p).body().statements().get(0);
        assertEquals("i", fr.varName());
        assertTrue(fr.from() instanceof IntLiteral);
        assertTrue(fr.to() instanceof IntLiteral);
    }

    @Test
    void parse_for_c_style_full_parts() {
        var p = parse("""
            fnc f : void() {
              for (i:int = 0; i < 10; i = i + 1) { }
              return;
            }
            """);
        var fs = (ForStmt) firstFn(p).body().statements().get(0);
        assertTrue(fs.init() instanceof VarDeclStmt);
        assertTrue(fs.condition() instanceof BinaryExpr);
        assertTrue(fs.update() instanceof AssignExpr); // update парсится как Expr [file:23]
    }

    @Test
    void parse_for_c_style_empty_parts() {
        var p = parse("""
            fnc f : void() {
              for (; ; ) { }
              return;
            }
            """);
        var fs = (ForStmt) firstFn(p).body().statements().get(0);
        // В твоём парсере пустой init/update кодируется через ExprStmt(IntLiteral(0)),
        // а пустое условие — BoolLiteral(true). [file:23]
        assertTrue(fs.init() instanceof ExprStmt);
        assertTrue(((ExprStmt) fs.init()).expr() instanceof IntLiteral);
        assertTrue(fs.condition() instanceof BoolLiteral);
        assertTrue(fs.update() instanceof IntLiteral);
    }

    @Test
    void parse_return_with_and_without_value() {
        var p = parse("""
            fnc f : void() { return; }
            fnc g : int() { return 1; }
            """);
        var f = (FunctionDecl) p.functions().get(0);
        var g = (FunctionDecl) p.functions().get(1);
        assertNull(((ReturnStmt) f.body().statements().getFirst()).value());
        assertNotNull(((ReturnStmt) g.body().statements().getFirst()).value());
    }

    @Test
    void parse_expr_precedence_and_assign_unary() {
        var p = parse("""
            fnc f : void() {
              x:int;
              x = -1 + 2 * 3;
              return;
            }
            """);
        var s = (ExprStmt) firstFn(p).body().statements().get(1);
        assertTrue(s.expr() instanceof AssignExpr);

        var asg = (AssignExpr) s.expr();
        assertTrue(asg.target() instanceof VarExpr);
        assertTrue(asg.value() instanceof BinaryExpr);

        var add = (BinaryExpr) asg.value();
        assertEquals(BinaryExpr.Operator.ADD, add.op());
        assertTrue(add.left() instanceof UnaryExpr);
        assertTrue(add.right() instanceof BinaryExpr);
        assertEquals(BinaryExpr.Operator.MUL, ((BinaryExpr)add.right()).op());
    }

    @Test
    void parse_postfix_chain_call_index_field() {
        var p = parse("""
            fnc f : void() {
              a.b(1,2)[3].c;
              return;
            }
            """);
        var stmt = (ExprStmt) firstFn(p).body().statements().get(0);
        // (((a.b(1,2))[3]).c) => FieldAccessExpr(ArrayAccessExpr(CallExpr(FieldAccessExpr(VarExpr("a"),"b"),...))) [file:23]
        assertTrue(stmt.expr() instanceof FieldAccessExpr);
        var fa = (FieldAccessExpr) stmt.expr();
        assertEquals("c", fa.field());
        assertTrue(fa.target() instanceof ArrayAccessExpr);
    }

    @Test
    void parse_invalid_assignment_target_throws() {
        // (1) = 2; запрещено проверкой lvalue [file:23]
        assertThrows(RuntimeException.class, () -> parse("""
            fnc f : void() { (1) = 2; }
            """));
    }

    @Test
    void parse_missing_semicolon_throws() {
        assertThrows(RuntimeException.class, () -> parse("""
            fnc f : void() { x:int = 1 }
            """));
    }
}
