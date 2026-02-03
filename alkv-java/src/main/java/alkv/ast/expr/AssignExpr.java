package alkv.ast.expr;

public record AssignExpr(
        Expr target,
        Expr value
) implements Expr {}
