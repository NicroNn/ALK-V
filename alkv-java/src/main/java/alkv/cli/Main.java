package alkv.cli;

import alkv.bytecode.ModuleCompiler;
import alkv.io.AlkbWriter;
import alkv.lexer.Lexer;
import alkv.parser.Parser;
import alkv.sema.TypeChecker;

import java.nio.file.Files;
import java.nio.file.Path;

public final class Main {
    public static void main(String[] args) throws Exception {
        if (args.length < 1) {
            System.err.println("Usage: alkv <input.alkv> [output.alkb]");
            System.exit(2);
        }

        Path input = Path.of(args[0]);
        Path output = (args.length >= 2)
                ? Path.of(args[1])
                : Path.of(input.toString().replaceFirst("\\.alkv$", "") + ".alkb");

        // 1. Чтение
        String source = Files.readString(input);
        System.out.println("[1/5] Reading: " + input);

        // 2. Лексер
        var tokens = new Lexer(source).tokenize();
        System.out.println("[2/5] Lexer: " + tokens.size() + " tokens");

        // 3. Парсер
        var program = new Parser(tokens).parseProgram();
        System.out.println("[3/5] Parser: " + program.functions().size() + " functions");

        // 4. Семантика
        var sema = new TypeChecker().check(program);
        System.out.println("[4/5] Type checker: OK");

        // 5. Генерация байткода
        var compiler = new ModuleCompiler(sema);
        var module = compiler.compileProgram(program);
        System.out.println("[5/5] Bytecode: " + module.functions().size() + " functions compiled");

        // 6. Запись
        AlkbWriter.write(output, module);

        // Статистика
        long fileSize = Files.size(output);
        int totalInsn = module.functions().stream()
                .mapToInt(f -> f.code().size())
                .sum();

        System.out.println("\n✓ Success: " + output);
        System.out.println("  Functions:    " + module.functions().size());
        System.out.println("  Instructions: " + totalInsn);
        System.out.println("  File size:    " + fileSize + " bytes");
    }
}