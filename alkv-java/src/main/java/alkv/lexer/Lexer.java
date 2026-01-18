package alkv.lexer;

import java.util.*;

public class Lexer {

    private final String source;
    private final List<Token> tokens = new ArrayList<>();

    private int pos = 0;
    private int line = 1;
    private int col = 1;

    private static final Map<String, TokenType> keywords = Map.ofEntries(
            Map.entry("fnc", TokenType.FNC),
            Map.entry("class", TokenType.CLASS),
            Map.entry("if", TokenType.IF),
            Map.entry("else", TokenType.ELSE),
            Map.entry("while", TokenType.WHILE),
            Map.entry("for", TokenType.FOR),
            Map.entry("in", TokenType.IN),
            Map.entry("return", TokenType.RETURN),
            Map.entry("public", TokenType.PUBLIC),
            Map.entry("private", TokenType.PRIVATE),
            Map.entry("new", TokenType.NEW),              // <-- add
            Map.entry("int", TokenType.INT),
            Map.entry("float", TokenType.FLOAT),
            Map.entry("bool", TokenType.BOOL),
            Map.entry("string", TokenType.STRING),
            Map.entry("void", TokenType.VOID),
            Map.entry("T", TokenType.BOOL_LITERAL),
            Map.entry("F", TokenType.BOOL_LITERAL)
    );

    public Lexer(String source) {
        this.source = source;
    }

    public List<Token> tokenize() {
        while (!isAtEnd()) {
            skipWhitespace();
            int startCol = col;
            int startLine = line;

            if (isAtEnd()) break;

            char c = advance();

            switch (c) {
                case '+' -> add(TokenType.PLUS, "+", startLine, startCol);
                case '-' -> add(TokenType.MINUS, "-", startLine, startCol);
                case '*' -> add(TokenType.STAR, "*", startLine, startCol);
                case '%' -> add(TokenType.PERCENT, "%", startLine, startCol);

                case '=' -> {
                    boolean eq = match('=');
                    add(eq ? TokenType.EQ : TokenType.ASSIGN, eq ? "==" : "=", startLine, startCol);
                }
                case '!' -> {
                    boolean neq = match('=');
                    add(neq ? TokenType.NEQ : TokenType.NOT, neq ? "!=" : "!", startLine, startCol);
                }

                case '<' -> {
                    // ochev.<<< treated as identifier "<<<"
                    if (match('<') && match('<')) {
                        add(TokenType.IDENTIFIER, "<<<", startLine, startCol);
                    } else {
                        boolean le = match('=');
                        add(le ? TokenType.LE : TokenType.LT, le ? "<=" : "<", startLine, startCol);
                    }
                }

                case '>' -> {
                    // ochev.>>> treated as identifier ">>>"
                    if (match('>') && match('>')) {
                        add(TokenType.IDENTIFIER, ">>>", startLine, startCol);
                    } else {
                        boolean ge = match('=');
                        add(ge ? TokenType.GE : TokenType.GT, ge ? ">=" : ">", startLine, startCol);
                    }
                }

                case '&' -> {
                    if (match('&')) add(TokenType.AND, "&&", startLine, startCol);
                    else error("Unexpected '&'");
                }

                case '|' -> {
                    if (match('|')) add(TokenType.OR, "||", startLine, startCol);
                    else error("Unexpected '|'");
                }

                case '/' -> {
                    if (match('/')) {
                        skipComment();
                    } else {
                        add(TokenType.SLASH, "/", startLine, startCol);
                    }
                }

                case '(' -> add(TokenType.LPAREN, "(", startLine, startCol);
                case ')' -> add(TokenType.RPAREN, ")", startLine, startCol);
                case '{' -> add(TokenType.LBRACE, "{", startLine, startCol);
                case '}' -> add(TokenType.RBRACE, "}", startLine, startCol);
                case '[' -> add(TokenType.LBRACKET, "[", startLine, startCol);
                case ']' -> add(TokenType.RBRACKET, "]", startLine, startCol);
                case ':' -> add(TokenType.COLON, ":", startLine, startCol);
                case ';' -> add(TokenType.SEMICOLON, ";", startLine, startCol);
                case ',' -> add(TokenType.COMMA, ",", startLine, startCol);

                case '.' -> {
                    if (peek() == '.' && peekNext() == '.') {
                        advance();
                        advance();
                        add(TokenType.RANGE, "...", startLine, startCol);
                    } else {
                        add(TokenType.DOT, ".", startLine, startCol);
                    }
                }

                case '"' -> stringLiteral(startLine, startCol);

                default -> {
                    if (isDigit(c)) numberLiteral(c, startLine, startCol);
                    else if (isAlpha(c)) identifier(c, startLine, startCol);
                    else error("Unexpected character: " + c);
                }
            }
        }

        tokens.add(new Token(TokenType.EOF, "", line, col));
        return tokens;
    }

    // ================= helpers =================

    private void numberLiteral(char first, int line, int col) {
        StringBuilder sb = new StringBuilder();
        sb.append(first);

        boolean isFloat = false;

        while (!isAtEnd() && isDigit(peek())) {
            sb.append(advance());
        }

        if (!isAtEnd() && peek() == '.' && isDigit(peekNext())) {
            isFloat = true;
            sb.append(advance());
            while (!isAtEnd() && isDigit(peek())) {
                sb.append(advance());
            }
        }

        add(isFloat ? TokenType.FLOAT_LITERAL : TokenType.INT_LITERAL,
                sb.toString(), line, col);
    }

    private void identifier(char first, int line, int col) {
        StringBuilder sb = new StringBuilder();
        sb.append(first);

        while (!isAtEnd() && isAlphaNumeric(peek())) {
            sb.append(advance());
        }

        String text = sb.toString();
        TokenType type = keywords.getOrDefault(text, TokenType.IDENTIFIER);

        add(type, text, line, col);
    }

    private void stringLiteral(int line, int col) {
        StringBuilder sb = new StringBuilder();

        while (!isAtEnd() && peek() != '"') {
            char c = advance();
            if (c == '\n') error("Unterminated string");
            sb.append(c);
        }

        if (isAtEnd()) error("Unterminated string");

        advance(); // closing "
        add(TokenType.STRING_LITERAL, sb.toString(), line, col);
    }

    private void skipWhitespace() {
        while (!isAtEnd()) {
            char c = peek();
            switch (c) {
                case ' ', '\t', '\r' -> advance();
                case '\n' -> {
                    advance();
                    line++;
                    col = 1;
                }
                default -> { return; }
            }
        }
    }

    private void skipComment() {
        while (!isAtEnd() && peek() != '\n') advance();
    }

    private boolean match(char expected) {
        if (isAtEnd()) return false;
        if (source.charAt(pos) != expected) return false;
        advance();
        return true;
    }

    private char advance() {
        char c = source.charAt(pos++);
        col++;
        return c;
    }

    private char peek() {
        return isAtEnd() ? '\0' : source.charAt(pos);
    }

    private char peekNext() {
        return pos + 1 >= source.length() ? '\0' : source.charAt(pos + 1);
    }

    private boolean isAtEnd() {
        return pos >= source.length();
    }

    private boolean isDigit(char c) {
        return c >= '0' && c <= '9';
    }

    private boolean isAlpha(char c) {
        return (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                c == '_';
    }

    private boolean isAlphaNumeric(char c) {
        return isAlpha(c) || isDigit(c);
    }

    private void add(TokenType type, String lexeme, int line, int col) {
        tokens.add(new Token(type, lexeme, line, col));
    }

    private void error(String message) {
        throw new LexerException("[" + line + ":" + col + "] " + message);
    }
}
