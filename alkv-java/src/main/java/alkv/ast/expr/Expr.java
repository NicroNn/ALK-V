package alkv.ast.expr;

public sealed interface Expr
        permits ArrayAccessExpr, ArrayLiteralExpr, AssignExpr, BinaryExpr, BoolLiteral, CallExpr, FieldAccessExpr, FloatLiteral, IntLiteral, NewExpr, StringLiteral, UnaryExpr, VarExpr {}
