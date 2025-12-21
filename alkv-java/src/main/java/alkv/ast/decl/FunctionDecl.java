package alkv.ast.decl;

import alkv.ast.stmt.BlockStmt;
import alkv.ast.type.TypeRef;

import java.util.List;

public record FunctionDecl(
        String name,
        TypeRef returnType,
        List<Param> params,
        BlockStmt body
) {
    public record Param(String name, TypeRef type) {}
}
