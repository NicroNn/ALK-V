package alkv.ast.stmt;

import alkv.ast.expr.Expr;

public record ReturnStmt(Expr value) implements Stmt {}
