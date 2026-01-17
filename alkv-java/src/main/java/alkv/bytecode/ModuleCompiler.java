package alkv.bytecode;

import alkv.ast.Program;
import alkv.ast.decl.FunctionDecl;
import alkv.sema.TypeChecker;

import java.util.ArrayList;
import java.util.List;

/**
 * Компилятор модуля (всей программы)
 * Компилирует все функции в один модуль байткода
 */
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

    /**
     * Компилирует всю программу
     */
    public CompiledModule compileProgram(Program program) {
        List<CompiledFunction> compiledFunctions = new ArrayList<>();

        for (Object declObj : program.functions()) {
            if (declObj instanceof FunctionDecl fn) {
                CompiledFunction compiled = compileFunction(fn);
                compiledFunctions.add(compiled);
            }
        }

        return new CompiledModule(compiledFunctions);
    }

    /**
     * Компилирует одну функцию
     */
    private CompiledFunction compileFunction(FunctionDecl fn) {
        var compiler = new SingleFunctionCompiler(sema);
        var result = compiler.compile(fn);

        // Подсчёт параметров
        int numParams = fn.params().size();

        return new CompiledFunction(
                fn.name(),
                numParams,
                result.consts(),
                result.code(),
                result.regCount()
        );
    }
}