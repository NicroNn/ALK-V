package alkv.ast.expr;

public sealed interface Expr
        permits IntLiteral, BoolLiteral, StringLiteral,
        VarExpr, BinaryExpr, CallExpr,
        ArrayAccessExpr, FieldAccessExpr {}
