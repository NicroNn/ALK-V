package alkv.sema;

import alkv.types.Type;

import java.util.List;

public record FuncSymbol(String name, List<Type> paramTypes, Type returnType) implements Symbol {}
