package alkv.bytecode;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class ConstPool {
    public sealed interface K permits KInt, KFloat, KBool, KString {}
    public record KInt(int v) implements K {}
    public record KFloat(float v) implements K {}
    public record KBool(boolean v) implements K {}
    public record KString(String v) implements K {}

    private final List<K> items = new ArrayList<>();
    private final Map<K, Integer> index = new HashMap<>();

    public int add(K k) {
        Integer i = index.get(k);
        if (i != null) return i;
        int id = items.size();
        items.add(k);
        index.put(k, id);
        return id;
    }

    public List<K> items() { return items; }
}
