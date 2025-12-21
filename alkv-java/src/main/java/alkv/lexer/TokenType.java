package alkv.lexer;

public enum TokenType {

    // literals
    IDENTIFIER,
    INT_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,
    BOOL_LITERAL,

    // keywords
    FNC,
    CLASS,
    IF,
    ELSE,
    WHILE,
    FOR,
    IN,
    RETURN,
    PUBLIC,
    PRIVATE,

    // types
    INT,
    FLOAT,
    BOOL,
    STRING,
    VOID,

    // operators
    PLUS, MINUS, STAR, SLASH, PERCENT,
    ASSIGN,
    EQ, NEQ,
    LT, LE,
    GT, GE,
    AND, OR, NOT,

    // symbols
    LPAREN, RPAREN,
    LBRACE, RBRACE,
    LBRACKET, RBRACKET,
    COLON, SEMICOLON, COMMA,
    DOT, RANGE,

    EOF
}
