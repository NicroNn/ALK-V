package alkv.ast.stmt;

import alkv.ast.expr.Expr;
import java.util.List;

public record SwitchStmt(
        Expr subject,
        List<Case> cases,
        BlockStmt defaultBlock // может быть null
) implements Stmt {
    public record Case(Expr match, BlockStmt body) {}
}