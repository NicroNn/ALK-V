package alkv.ast.expr;

public sealed interface Expr
        permits ArrayAccessExpr, AssignExpr, BinaryExpr, BoolLiteral, CallExpr, FieldAccessExpr, IntLiteral, StringLiteral, UnaryExpr, VarExpr, ArrayLiteralExpr, NewExpr {}
