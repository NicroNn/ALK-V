package alkv.types;

public final class TypeUtil {
    private TypeUtil() {}

    public static boolean isAssignable(Type dst, Type src) {
        if (dst.equals(src)) return true;
        // неявное int -> float
        if (dst == PrimitiveType.FLOAT && src == PrimitiveType.INT) return true;
        return false;
    }

    public static Type numericResult(Type a, Type b) {
        if (!a.isNumeric() || !b.isNumeric()) return null;
        return (a == PrimitiveType.FLOAT || b == PrimitiveType.FLOAT) ? PrimitiveType.FLOAT : PrimitiveType.INT;
    }
}
