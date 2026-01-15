package alkv.ast.decl;

import alkv.ast.stmt.BlockStmt;

import java.util.List;

public record ConstructorDecl(
        String className,
        List<FunctionDecl.Param> params,
        BlockStmt body,
        boolean isPublic
) {}
