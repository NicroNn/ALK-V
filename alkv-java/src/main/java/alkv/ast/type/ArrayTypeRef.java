package alkv.ast.type;

public record ArrayTypeRef(TypeRef element, Integer size) implements TypeRef {}
