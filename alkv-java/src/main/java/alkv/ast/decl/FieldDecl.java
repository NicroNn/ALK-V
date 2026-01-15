package alkv.ast.decl;

import alkv.ast.type.TypeRef;

public record FieldDecl(
        String name,
        TypeRef type,
        boolean isPublic
) {}
