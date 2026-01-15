package alkv.sema;

import java.util.HashMap;
import java.util.Map;

public final class ClassSymbol implements Symbol {
    public final String name;
    public final Map<String, VarSymbol> fields = new HashMap<>();
    public final Map<String, FuncSymbol> methods = new HashMap<>();

    public ClassSymbol(String name) { this.name = name; }
}
