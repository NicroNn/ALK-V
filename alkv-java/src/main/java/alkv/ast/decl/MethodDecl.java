package alkv.ast.decl;

import alkv.ast.stmt.BlockStmt;
import alkv.ast.type.TypeRef;

import java.util.List;

public record MethodDecl(
        String name,
        TypeRef returnType,
        List<FunctionDecl.Param> params,
        BlockStmt body,
        boolean isPublic
) {}
