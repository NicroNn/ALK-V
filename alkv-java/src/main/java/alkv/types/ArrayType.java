package alkv.types;

public record ArrayType(Type element, Integer size) implements Type {}
