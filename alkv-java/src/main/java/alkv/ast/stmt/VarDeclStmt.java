package alkv.ast.stmt;

import alkv.ast.expr.Expr;
import alkv.ast.type.TypeRef;

public record VarDeclStmt(
        String name,
        TypeRef type,
        Expr initializer
) implements Stmt {}
