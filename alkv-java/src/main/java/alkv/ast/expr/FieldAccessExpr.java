package alkv.ast.expr;

public record FieldAccessExpr(
        Expr target,
        String field
) implements Expr {}
