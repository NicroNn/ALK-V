package alkv.ast.stmt;

import alkv.ast.expr.Expr;

public record ForRangeStmt(
        String varName,
        Expr from,
        Expr to,
        BlockStmt body
) implements Stmt {}
