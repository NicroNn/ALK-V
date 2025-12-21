package alkv.ast.stmt;

import alkv.ast.expr.Expr;

public record ForStmt(
        Stmt init,
        Expr condition,
        Expr update,
        BlockStmt body
) implements Stmt {}
