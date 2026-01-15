package alkv.sema;

import alkv.types.Type;

public record VarSymbol(String name, Type type) implements Symbol {}
