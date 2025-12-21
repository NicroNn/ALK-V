package alkv.ast.expr;

public record UnaryExpr(
        Operator op,
        Expr expr
) implements Expr {
    public enum Operator {
        NEG, NOT
    }
}
