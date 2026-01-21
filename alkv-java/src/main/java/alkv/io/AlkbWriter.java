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

public final class AlkbWriter {
    private static final int MAGIC = 0x414C4B42;

    // подняли версию, потому что расширили CP
    private static final short VERSION = 2;

    // ---- public API ----

    public static void write(Path out, ModuleCompiler.CompiledModule module) throws Exception {
        try (OutputStream os = Files.newOutputStream(out);
             DataOutputStream d = new DataOutputStream(os)) {

            // Header
            d.writeInt(MAGIC);
            d.writeShort(VERSION);

            // Section FN
            d.writeByte('F');
            d.writeByte('N');
            d.writeInt(module.functions().size());

            for (var fn : module.functions()) {
                writeFunction(d, fn);
            }
        }
    }

    public static void write(Path out, List<Insn> code) throws Exception {
        try (OutputStream os = Files.newOutputStream(out);
             DataOutputStream d = new DataOutputStream(os)) {

            d.writeInt(MAGIC);
            d.writeShort(VERSION);

            // CODE tag CD length(bytes) payload
            d.writeByte('C');
            d.writeByte('D');
            d.writeInt(code.size() * 4);

            for (Insn ins : code) d.writeInt(ins.word());
        }
    }

    // ---- internals ----

    private static void writeFunction(DataOutputStream d, ModuleCompiler.CompiledFunction fn) throws Exception {
        // Section FH
        writeFunctionHeader(d, fn);

        // Section CP
        writeConstPool(d, fn.constPool());

        // Section CD
        writeCode(d, fn.code());
    }

    private static void writeFunctionHeader(DataOutputStream d, ModuleCompiler.CompiledFunction fn) throws Exception {
        byte[] nameBytes = fn.name().getBytes(StandardCharsets.UTF_8);

        // payloadSize:
        //   nameLen(u16) + nameBytes + numParams(i32) + regCount(i32)
        int payloadSize = 2 + nameBytes.length + 4 + 4;

        d.writeByte('F');
        d.writeByte('H');
        d.writeInt(payloadSize);

        d.writeShort(nameBytes.length);
        d.write(nameBytes);
        d.writeInt(fn.numParams());
        d.writeInt(fn.regCount());
    }

    // CP:
    //   tag 'C''P'
    //   sizeBytes(i32)
    //   payload:
    //     numConstants(i32)
    //     repeated:
    //       type(u8) + data...
    private static void writeConstPool(DataOutputStream d, ConstPool pool) throws Exception {
        List<ConstPool.K> items = pool.items();

        int payloadSize = 4; // num constants

        for (ConstPool.K k : items) {
            payloadSize += 1; // type byte

            if (k instanceof ConstPool.KInt) payloadSize += 4;
            else if (k instanceof ConstPool.KFloat) payloadSize += 4;
            else if (k instanceof ConstPool.KBool) payloadSize += 1;
            else if (k instanceof ConstPool.KString ks) payloadSize += sizeOfString(ks.v());

            else if (k instanceof ConstPool.KFunc kf) {
                payloadSize += sizeOfString(kf.name());
                payloadSize += 4; // arity
            } else if (k instanceof ConstPool.KClass kc) {
                payloadSize += sizeOfString(kc.name());
            } else if (k instanceof ConstPool.KField kfld) {
                payloadSize += sizeOfString(kfld.className());
                payloadSize += sizeOfString(kfld.fieldName());
            } else if (k instanceof ConstPool.KMethod km) {
                payloadSize += sizeOfString(km.className());
                payloadSize += sizeOfString(km.methodName());
                payloadSize += 4; // arity
            } else {
                throw new IllegalStateException("Unknown constant type: " + k.getClass());
            }
        }

        d.writeByte('C');
        d.writeByte('P');
        d.writeInt(payloadSize);

        d.writeInt(items.size());

        for (ConstPool.K k : items) {
            if (k instanceof ConstPool.KInt ki) {
                d.writeByte(0);
                d.writeInt(ki.v());

            } else if (k instanceof ConstPool.KFloat kf) {
                d.writeByte(1);
                d.writeFloat(kf.v());

            } else if (k instanceof ConstPool.KBool kb) {
                d.writeByte(2);
                d.writeBoolean(kb.v());

            } else if (k instanceof ConstPool.KString ks) {
                d.writeByte(3);
                writeString(d, ks.v());

            } else if (k instanceof ConstPool.KFunc fn) {
                d.writeByte(4);
                writeString(d, fn.name());
                d.writeInt(fn.arity());

            } else if (k instanceof ConstPool.KClass cls) {
                d.writeByte(5);
                writeString(d, cls.name());

            } else if (k instanceof ConstPool.KField fld) {
                d.writeByte(6);
                writeString(d, fld.className());
                writeString(d, fld.fieldName());

            } else if (k instanceof ConstPool.KMethod m) {
                d.writeByte(7);
                writeString(d, m.className());
                writeString(d, m.methodName());
                d.writeInt(m.arity());

            } else {
                throw new IllegalStateException("Unknown constant type: " + k.getClass());
            }
        }
    }

    private static void writeCode(DataOutputStream d, List<Insn> code) throws Exception {
        d.writeByte('C');
        d.writeByte('D');
        d.writeInt(code.size() * 4);

        for (Insn ins : code) d.writeInt(ins.word());
    }

    private static int sizeOfString(String s) {
        byte[] b = s.getBytes(StandardCharsets.UTF_8);
        return 4 + b.length; // i32 len + bytes
    }

    private static void writeString(DataOutputStream d, String s) throws Exception {
        byte[] b = s.getBytes(StandardCharsets.UTF_8);
        d.writeInt(b.length);
        d.write(b);
    }
}
