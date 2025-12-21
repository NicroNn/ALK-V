package alkv.ast.stmt;

import alkv.ast.expr.Expr;

public record WhileStmt(
        Expr condition,
        BlockStmt body
) implements Stmt {}
