package alkv.ast.expr;

public record BinaryExpr(
        Expr left,
        Operator op,
        Expr right
) implements Expr {

    public enum Operator {
        ADD, SUB, MUL, DIV, MOD,
        EQ, NE, LT, GT, LE, GE,
        AND, OR
    }
}
