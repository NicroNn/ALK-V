package alkv.ast.stmt;

import alkv.ast.expr.Expr;

public record ExprStmt(Expr expr) implements Stmt {}
