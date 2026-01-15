package alkv.sema;

public sealed interface Symbol permits VarSymbol, FuncSymbol, ClassSymbol {}
