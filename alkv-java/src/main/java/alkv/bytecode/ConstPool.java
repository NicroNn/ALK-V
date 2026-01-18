package alkv.bytecode;

import java.util.*;

public final class ConstPool {
    public sealed interface K permits KInt, KFloat, KBool, KString,
            KFunc, KClass, KField, KMethod {}

    public record KInt(int v) implements K {}
    public record KFloat(float v) implements K {}
    public record KBool(boolean v) implements K {}
    public record KString(String v) implements K {}

    // глобальная функция (включая name-mangled методы)
    public record KFunc(String name, int arity) implements K {}

    public record KClass(String name) implements K {}

    // поле класса (для layout/offset-таблицы на VM-стороне)
    public record KField(String className, String fieldName) implements K {}

    // метод класса
    public record KMethod(String className, String methodName, int arity) implements K {}

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