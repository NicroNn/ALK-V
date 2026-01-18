package alkv.bytecode;

import alkv.ast.decl.*;
import alkv.ast.stmt.BlockStmt;
import alkv.ast.type.NamedTypeRef;
import alkv.ast.type.TypeRef;

import java.util.*;

public final class Lowering {
    private Lowering() {}

    public static FunctionDecl lowerMethodToFunction(String className, MethodDecl m) {
        List<FunctionDecl.Param> ps = new ArrayList<>();
        ps.add(new FunctionDecl.Param("this", new NamedTypeRef(className)));
        ps.addAll(m.params());
        return new FunctionDecl(className + "." + m.name(), m.returnType(), ps, m.body());
    }

    public static FunctionDecl lowerCtorToFunction(String className, ConstructorDecl c) {
        List<FunctionDecl.Param> ps = new ArrayList<>();
        ps.add(new FunctionDecl.Param("this", new NamedTypeRef(className)));
        ps.addAll(c.params());

        // модель: ctor возвращает this
        TypeRef ret = new NamedTypeRef(className);
        BlockStmt body = c.body(); // можно в конец добавить `return this;` на lowering-этапе
        return new FunctionDecl(className + ".<init>", ret, ps, body);
    }
}