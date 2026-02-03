package alkv.types;

public sealed interface Type permits PrimitiveType, ArrayType, ClassType {
    default boolean isNumeric() {
        return this == PrimitiveType.INT || this == PrimitiveType.FLOAT;
    }
}
