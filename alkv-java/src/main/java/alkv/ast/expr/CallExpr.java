package alkv.ast.expr;

import java.util.List;

public record CallExpr(
        Expr callee,
        List<Expr> args
) implements Expr {}
