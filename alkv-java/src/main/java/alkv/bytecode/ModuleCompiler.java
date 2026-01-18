package alkv.bytecode;

import alkv.ast.Program;
import alkv.ast.decl.*;
import alkv.sema.TypeChecker;

import java.util.*;

public final class ModuleCompiler {
    public record CompiledModule(List<CompiledFunction> functions) {}

    public record CompiledFunction(
            String name,
            int numParams,
            ConstPool constPool,
            List<Insn> code,
            int regCount
    ) {}

    private final TypeChecker.Result sema;

    public ModuleCompiler(TypeChecker.Result sema) {
        this.sema = sema;
    }

    public CompiledModule compileProgram(Program program) {
        List<CompiledFunction> compiled = new ArrayList<>();

        // 1) обычные функции
        for (FunctionDecl fn : program.functions()) {
            compiled.add(compileFunction(fn.name(), fn));
        }

        // 2) методы/конструкторы классов -> тоже функции (name-mangled)
        for (ClassDecl cd : program.classes()) {
            for (MethodDecl m : cd.methods()) {
                FunctionDecl lowered = Lowering.lowerMethodToFunction(cd.name(), m);
                compiled.add(compileFunction(lowered.name(), lowered));
            }
            for (ConstructorDecl c : cd.constructors()) {
                FunctionDecl lowered = Lowering.lowerCtorToFunction(cd.name(), c);
                compiled.add(compileFunction(lowered.name(), lowered));
            }
        }

        return new CompiledModule(compiled);
    }

    private CompiledFunction compileFunction(String name, FunctionDecl fn) {
        var compiler = new SingleFunctionCompiler(sema);
        var result = compiler.compile(fn);
        return new CompiledFunction(
                name,
                fn.params().size(),
                result.consts(),
                result.code(),
                result.regCount()
        );
    }
}