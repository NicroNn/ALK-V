package alkv.sema;

import java.util.ArrayDeque;
import java.util.Deque;

public final class SymbolTable {
    private final Deque<Scope> scopes = new ArrayDeque<>();

    public SymbolTable() { push(); }

    public void push() { scopes.push(new Scope()); }
    public void pop() { scopes.pop(); }

    public void define(Symbol sym) { scopes.peek().define(sym); }

    public Symbol lookup(String name) {
        for (Scope s : scopes) {
            Symbol sym = s.getLocal(name);
            if (sym != null) return sym;
        }
        return null;
    }
}
