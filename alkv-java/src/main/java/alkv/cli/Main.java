package alkv.cli;

import alkv.ast.Program;
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
                : Path.of(args[0] + ".alkb");

        String source = Files.readString(input); // Java NIO readString [web:125]

        var tokens = new Lexer(source).tokenize();

        var program = new Parser(tokens).parseProgram();
        var sema = new TypeChecker().check(program);

        var fn = (alkv.ast.decl.FunctionDecl) program.functions().get(0);
        var compiled = new alkv.bytecode.SingleFunctionCompiler(sema).compile(fn);

        alkv.io.AlkbWriter.write(output, compiled.code());


        // 4) bytecode (пока заглушка, дальше вставишь генератор)
        // byte[] alkb = new BytecodeCompiler().compile(program, sema);
        // Files.write(output, alkb);

        System.out.println("OK: parsed + typechecked: " + input);
        System.out.println("Next: write bytecode to: " + output);
    }
}
