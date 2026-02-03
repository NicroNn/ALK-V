package alkv.ast.type;

public sealed interface TypeRef
        permits PrimitiveTypeRef, ArrayTypeRef, NamedTypeRef {}
