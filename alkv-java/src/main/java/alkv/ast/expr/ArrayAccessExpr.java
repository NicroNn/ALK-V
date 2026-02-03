package alkv.ast.expr;

public record ArrayAccessExpr(
        Expr array,
        Expr index
) implements Expr {}
