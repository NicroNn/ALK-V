package alkv.ast.expr;

import java.util.List;

public record NewExpr(
        String className,
        List<Expr> args
) implements Expr {}