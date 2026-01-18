package alkv.ast.expr;

import java.util.List;

public record ArrayLiteralExpr(
        List<Expr> elements
) implements Expr {}