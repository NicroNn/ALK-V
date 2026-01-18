package alkv.ast.stmt;

import alkv.ast.expr.Expr;
import java.util.List;

public record IfStmt(
        List<Branch> branches,   // if + else if ...
        BlockStmt elseBlock      // может быть null
) implements Stmt {
    public record Branch(Expr condition, BlockStmt body) {}
}