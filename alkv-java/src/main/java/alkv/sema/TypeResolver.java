package alkv.sema;

import alkv.ast.type.ArrayTypeRef;
import alkv.ast.type.NamedTypeRef;
import alkv.ast.type.PrimitiveTypeRef;
import alkv.ast.type.TypeRef;
import alkv.types.*;

public final class TypeResolver {
    private final SymbolTable globals;

    public TypeResolver(SymbolTable globals) { this.globals = globals; }

    public Type resolve(TypeRef ref) {
        return switch (ref) {
            case PrimitiveTypeRef p -> switch (p.name()) {
                case "int" -> PrimitiveType.INT;
                case "float" -> PrimitiveType.FLOAT;
                case "bool" -> PrimitiveType.BOOL;
                case "string" -> PrimitiveType.STRING;
                case "void" -> PrimitiveType.VOID;
                default -> throw new SemanticException("Unknown primitive type: " + p.name());
            };
            case ArrayTypeRef a -> new ArrayType(resolve(a.element()), a.size());
            case NamedTypeRef n -> {
                // класс должен быть в глобальном scope
                Symbol sym = globals.lookup(n.name());
                if (sym instanceof ClassSymbol) yield new ClassType(n.name());
                throw new SemanticException("Unknown type: " + n.name());
            }
        };
    }
}
