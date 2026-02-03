package alkv.sema;

import java.util.HashMap;
import java.util.Map;

public final class Scope {
    private final Map<String, Symbol> symbols = new HashMap<>();

    public void define(Symbol sym) {
        String name = switch (sym) {
            case VarSymbol v -> v.name();
            case FuncSymbol f -> f.name();
            case ClassSymbol c -> c.name;
        };
        if (symbols.containsKey(name)) throw new SemanticException("Duplicate symbol: " + name);
        symbols.put(name, sym);
    }

    public Symbol getLocal(String name) {
        return symbols.get(name);
    }
}
