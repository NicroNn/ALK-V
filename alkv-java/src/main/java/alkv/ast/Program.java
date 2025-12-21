package alkv.ast;

import alkv.ast.decl.*;

import java.util.List;

public record Program(
        List<FunctionDecl> functions,
        List<ClassDecl> classes
) {}
