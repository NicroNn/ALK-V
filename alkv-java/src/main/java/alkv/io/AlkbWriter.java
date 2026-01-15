package alkv.io;

import alkv.bytecode.Insn;

import java.io.DataOutputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

public final class AlkbWriter {
    // "ALKB"
    private static final int MAGIC = 0x414C4B42;
    private static final short VERSION = 1;

    public static void write(Path out, List<Insn> code) throws Exception {
        try (OutputStream os = Files.newOutputStream(out);
             DataOutputStream d = new DataOutputStream(os)) {

            d.writeInt(MAGIC);
            d.writeShort(VERSION);

            // секция CODE: tag 'C''D' + length(bytes) + payload
            d.writeByte('C');
            d.writeByte('D');
            d.writeInt(code.size() * 4);

            for (Insn ins : code) d.writeInt(ins.word());
        }
    }
}
