package alkv.ast.decl;

import java.util.List;

public record ClassDecl(
        String name,
        List<FieldDecl> fields,
        List<MethodDecl> methods,
        List<ConstructorDecl> constructors
) {}