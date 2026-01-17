package alkv.io;

import alkv.bytecode.ConstPool;
import alkv.bytecode.Insn;
import alkv.bytecode.ModuleCompiler;

import java.io.DataOutputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

/**
 * Запись байткода в формат .alkb
 *
 * Формат файла:
 * [Magic: 4 bytes "ALKB" (0x414C4B42)]
 * [Version: 2 bytes]
 *
 * Sections (tagged):
 *   'FN' + [Num Functions: 4 bytes]
 *   For each function:
 *     'F''H' + [Function Header]
 *     'C''P' + [Const Pool]
 *     'C''D' + [Code]
 */
public final class AlkbWriter {

    // "ALKB"
    private static final int MAGIC = 0x414C4B42;
    private static final short VERSION = 1;

    /**
     * Запись модуля (всех функций)
     */
    public static void write(Path out, ModuleCompiler.CompiledModule module) throws Exception {
        try (OutputStream os = Files.newOutputStream(out);
             DataOutputStream d = new DataOutputStream(os)) {

            // Header
            d.writeInt(MAGIC);
            d.writeShort(VERSION);

            // Section FN: количество функций
            d.writeByte('F');
            d.writeByte('N');
            d.writeInt(module.functions().size());

            // Запись каждой функции
            for (var fn : module.functions()) {
                writeFunction(d, fn);
            }
        }
    }

    /**
     * Запись одной функции (старый формат для совместимости)
     * Используется для тестов
     */
    public static void write(Path out, List<Insn> code) throws Exception {
        try (OutputStream os = Files.newOutputStream(out);
             DataOutputStream d = new DataOutputStream(os)) {

            d.writeInt(MAGIC);
            d.writeShort(VERSION);

            // Секция CODE: tag 'CD' + length(bytes) + payload
            d.writeByte('C');
            d.writeByte('D');
            d.writeInt(code.size() * 4);
            for (Insn ins : code) {
                d.writeInt(ins.word());
            }
        }
    }

    // ========================================================================
    // Внутренние методы
    // ========================================================================

    /**
     * Запись одной функции
     */
    private static void writeFunction(DataOutputStream d, ModuleCompiler.CompiledFunction fn)
            throws Exception {

        // Section FH: Function Header
        writeFunctionHeader(d, fn);

        // Section CP: Const Pool
        writeConstPool(d, fn.constPool());

        // Section CD: Code
        writeCode(d, fn.code());
    }

    /**
     * Section FH: Function Header
     * Format:
     *   'F''H' + size(bytes) + payload
     *   payload:
     *     [Name Length: 2 bytes]
     *     [Name: N bytes UTF-8]
     *     [Num Params: 4 bytes]
     *     [Num Registers: 4 bytes]
     */
    private static void writeFunctionHeader(DataOutputStream d, ModuleCompiler.CompiledFunction fn)
            throws Exception {

        byte[] nameBytes = fn.name().getBytes(StandardCharsets.UTF_8);

        // Вычисляем размер payload
        int payloadSize = 2 + nameBytes.length + 4 + 4;

        // Tag
        d.writeByte('F');
        d.writeByte('H');

        // Size
        d.writeInt(payloadSize);

        // Payload
        d.writeShort(nameBytes.length);
        d.write(nameBytes);
        d.writeInt(fn.numParams());
        d.writeInt(fn.regCount());
    }

    /**
     * Section CP: Const Pool
     * Format:
     *   'C''P' + size(bytes) + payload
     *   payload:
     *     [Num Constants: 4 bytes]
     *     For each constant:
     *       [Type: 1 byte]  // 0=int, 1=float, 2=bool, 3=string
     *       [Value: variable]
     */
    private static void writeConstPool(DataOutputStream d, ConstPool pool) throws Exception {
        List<ConstPool.K> items = pool.items();

        // Вычисляем размер payload (нужно предварительно посчитать)
        int payloadSize = 4; // num constants
        for (ConstPool.K k : items) {
            payloadSize += 1; // type byte
            if (k instanceof ConstPool.KInt) {
                payloadSize += 4;
            } else if (k instanceof ConstPool.KFloat) {
                payloadSize += 4;
            } else if (k instanceof ConstPool.KBool) {
                payloadSize += 1;
            } else if (k instanceof ConstPool.KString ks) {
                byte[] strBytes = ks.v().getBytes(StandardCharsets.UTF_8);
                payloadSize += 4 + strBytes.length;
            }
        }

        // Tag
        d.writeByte('C');
        d.writeByte('P');

        // Size
        d.writeInt(payloadSize);

        // Payload
        d.writeInt(items.size());

        for (ConstPool.K k : items) {
            if (k instanceof ConstPool.KInt ki) {
                d.writeByte(0);  // type: int
                d.writeInt(ki.v());

            } else if (k instanceof ConstPool.KFloat kf) {
                d.writeByte(1);  // type: float
                d.writeFloat(kf.v());

            } else if (k instanceof ConstPool.KBool kb) {
                d.writeByte(2);  // type: bool
                d.writeBoolean(kb.v());

            } else if (k instanceof ConstPool.KString ks) {
                d.writeByte(3);  // type: string
                byte[] strBytes = ks.v().getBytes(StandardCharsets.UTF_8);
                d.writeInt(strBytes.length);
                d.write(strBytes);

            } else {
                throw new IllegalStateException("Unknown constant type: " + k.getClass());
            }
        }
    }

    /**
     * Section CD: Code
     * Format:
     *   'C''D' + size(bytes) + payload
     *   payload:
     *     [instructions: N * 4 bytes]
     */
    private static void writeCode(DataOutputStream d, List<Insn> code) throws Exception {
        // Tag
        d.writeByte('C');
        d.writeByte('D');

        // Size
        d.writeInt(code.size() * 4);

        // Payload
        for (Insn ins : code) {
            d.writeInt(ins.word());
        }
    }
}