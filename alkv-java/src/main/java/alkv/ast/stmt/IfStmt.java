package alkv.ast.stmt;

import alkv.ast.expr.Expr;

public record IfStmt(
        Expr condition,
        BlockStmt thenBlock,
        BlockStmt elseBlock
) implements Stmt {}
