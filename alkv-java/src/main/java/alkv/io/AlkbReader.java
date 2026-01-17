package alkv.io;

import alkv.bytecode.ConstPool;
import alkv.bytecode.Insn;
import alkv.bytecode.ModuleCompiler;

import java.io.DataInputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * Чтение байткода из формата .alkb
 */
public final class AlkbReader {

    private static final int MAGIC = 0x414C4B42;

    public static ModuleCompiler.CompiledModule read(Path path) throws Exception {
        try (InputStream is = Files.newInputStream(path);
             DataInputStream d = new DataInputStream(is)) {

            return readModule(d);
        }
    }

    /**
     * Чтение только кода (старый формат, для совместимости)
     */
    public static List<Insn> readCodeOnly(Path path) throws Exception {
        try (InputStream is = Files.newInputStream(path);
             DataInputStream d = new DataInputStream(is)) {

            // Magic
            int magic = d.readInt();
            if (magic != MAGIC) {
                throw new IllegalArgumentException("Invalid magic: 0x" + Integer.toHexString(magic));
            }

            // Version
            short version = d.readShort();
            if (version != 1) {
                throw new IllegalArgumentException("Unsupported version: " + version);
            }

            // Expect CD section
            byte tag1 = d.readByte();
            byte tag2 = d.readByte();
            if (tag1 != 'C' || tag2 != 'D') {
                throw new IllegalArgumentException("Expected CD section");
            }

            int sizeBytes = d.readInt();
            int numInsn = sizeBytes / 4;

            List<Insn> code = new ArrayList<>(numInsn);
            for (int i = 0; i < numInsn; i++) {
                code.add(new Insn(d.readInt()));
            }

            return code;
        }
    }

    // ========================================================================
    // Внутренние методы
    // ========================================================================

    private static ModuleCompiler.CompiledModule readModule(DataInputStream d) throws Exception {
        // Magic
        int magic = d.readInt();
        if (magic != MAGIC) {
            throw new IllegalArgumentException("Invalid magic: 0x" + Integer.toHexString(magic));
        }

        // Version
        short version = d.readShort();
        if (version != 1) {
            throw new IllegalArgumentException("Unsupported version: " + version);
        }

        // Section FN
        expectTag(d, 'F', 'N');
        int numFunctions = d.readInt();

        List<ModuleCompiler.CompiledFunction> functions = new ArrayList<>(numFunctions);

        for (int i = 0; i < numFunctions; i++) {
            functions.add(readFunction(d));
        }

        return new ModuleCompiler.CompiledModule(functions);
    }

    private static ModuleCompiler.CompiledFunction readFunction(DataInputStream d) throws Exception {
        // Section FH: Function Header
        expectTag(d, 'F', 'H');
        int fhSize = d.readInt();

        short nameLen = d.readShort();
        byte[] nameBytes = new byte[nameLen];
        d.readFully(nameBytes);
        String name = new String(nameBytes, StandardCharsets.UTF_8);

        int numParams = d.readInt();
        int regCount = d.readInt();

        // Section CP: Const Pool
        expectTag(d, 'C', 'P');
        int cpSize = d.readInt();
        ConstPool constPool = readConstPool(d);

        // Section CD: Code
        expectTag(d, 'C', 'D');
        int cdSize = d.readInt();
        List<Insn> code = readCode(d, cdSize);

        return new ModuleCompiler.CompiledFunction(name, numParams, constPool, code, regCount);
    }

    private static ConstPool readConstPool(DataInputStream d) throws Exception {
        ConstPool pool = new ConstPool();
        int numConstants = d.readInt();

        for (int i = 0; i < numConstants; i++) {
            byte type = d.readByte();

            switch (type) {
                case 0: // int
                    pool.add(new ConstPool.KInt(d.readInt()));
                    break;

                case 1: // float
                    pool.add(new ConstPool.KFloat(d.readFloat()));
                    break;

                case 2: // bool
                    pool.add(new ConstPool.KBool(d.readBoolean()));
                    break;

                case 3: // string
                    int strLen = d.readInt();
                    byte[] strBytes = new byte[strLen];
                    d.readFully(strBytes);
                    pool.add(new ConstPool.KString(new String(strBytes, StandardCharsets.UTF_8)));
                    break;

                default:
                    throw new IllegalArgumentException("Unknown constant type: " + type);
            }
        }

        return pool;
    }

    private static List<Insn> readCode(DataInputStream d, int sizeBytes) throws Exception {
        int numInsn = sizeBytes / 4;
        List<Insn> code = new ArrayList<>(numInsn);

        for (int i = 0; i < numInsn; i++) {
            code.add(new Insn(d.readInt()));
        }

        return code;
    }

    private static void expectTag(DataInputStream d, char c1, char c2) throws Exception {
        byte b1 = d.readByte();
        byte b2 = d.readByte();

        if (b1 != c1 || b2 != c2) {
            throw new IllegalArgumentException(
                    String.format("Expected tag '%c%c', got '%c%c'", c1, c2, (char)b1, (char)b2)
            );
        }
    }
}