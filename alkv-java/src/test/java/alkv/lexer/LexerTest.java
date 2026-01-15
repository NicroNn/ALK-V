package alkv.lexer;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.MethodSource;

import java.util.List;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.*;

public class LexerTest {

    private static List<Token> lex(String src) {
        return new Lexer(src).tokenize();
    }

    private static List<TokenType> typesNoEof(String src) {
        return lex(src).stream().filter(t -> t.type() != TokenType.EOF).map(Token::type).toList();
    }

    @Test
    void lex_all_single_char_tokens() {
        // () {} [] : ; , + - * / % .
        var ts = typesNoEof("(){}[]:;,+-*/%.");
        assertEquals(List.of(
                TokenType.LPAREN, TokenType.RPAREN,
                TokenType.LBRACE, TokenType.RBRACE,
                TokenType.LBRACKET, TokenType.RBRACKET,
                TokenType.COLON, TokenType.SEMICOLON, TokenType.COMMA,
                TokenType.PLUS, TokenType.MINUS, TokenType.STAR, TokenType.SLASH, TokenType.PERCENT,
                TokenType.DOT
        ), ts);
    }

    @Test
    void lex_range_vs_dot() {
        assertEquals(List.of(TokenType.RANGE), typesNoEof("..."));
        assertEquals(List.of(TokenType.DOT, TokenType.DOT), typesNoEof(".."));
        assertEquals(List.of(TokenType.INT_LITERAL, TokenType.DOT), typesNoEof("12.")); // число + DOT [file:24]
        assertEquals(List.of(TokenType.INT_LITERAL, TokenType.RANGE, TokenType.IDENTIFIER), typesNoEof("12...n"));
    }

    @Test
    void lex_keywords_types_and_bool_literal() {
        var ts = typesNoEof("fnc class if else while for in return public private int float bool string void T F");
        assertEquals(List.of(
                TokenType.FNC, TokenType.CLASS, TokenType.IF, TokenType.ELSE, TokenType.WHILE,
                TokenType.FOR, TokenType.IN, TokenType.RETURN, TokenType.PUBLIC, TokenType.PRIVATE,
                TokenType.INT, TokenType.FLOAT, TokenType.BOOL, TokenType.STRING, TokenType.VOID,
                TokenType.BOOL_LITERAL, TokenType.BOOL_LITERAL
        ), ts);
        var toks = lex("T F");
        assertEquals("T", toks.get(0).lexeme());
        assertEquals("F", toks.get(1).lexeme());
    }

    @Test
    void lex_identifier_vs_keyword() {
        // "in" это keyword, "in1" — обычный идентификатор [file:24]
        assertEquals(List.of(TokenType.IN, TokenType.IDENTIFIER), typesNoEof("in in1"));
    }

    @Test
    void lex_numbers_int_and_float() {
        assertEquals(List.of(TokenType.INT_LITERAL, TokenType.FLOAT_LITERAL), typesNoEof("0 3.14"));
        var toks = lex("3.14");
        assertEquals("3.14", toks.getFirst().lexeme());
    }

    @Test
    void lex_string_literal() {
        var toks = lex("\"abc\"");
        assertEquals(TokenType.STRING_LITERAL, toks.getFirst().type());
        assertEquals("abc", toks.getFirst().lexeme());
    }

    @Test
    void lex_comment_skipped() {
        assertEquals(List.of(TokenType.IDENTIFIER, TokenType.IDENTIFIER), typesNoEof("x//cmt\ny"));
    }

    @Test
    void lex_whitespace_and_positions() {
        var toks = lex("a\n  b");
        // a at 1:1, b at 2:3 (после '\n' col=1, затем два пробела -> col=3) [file:24]
        assertEquals("IDENTIFIER('a')@1:1", toks.get(0).toString());
        assertEquals("IDENTIFIER('b')@2:3", toks.get(1).toString());
    }

    static Stream<String> opInputs() {
        return Stream.of(
                "a=b", "a==b", "a!=b", "a<b", "a<=b", "a>b", "a>=b", "a&&b", "a||b", "!a"
        );
    }

    @ParameterizedTest
    @MethodSource("opInputs")
    void lex_operators(String input) {
        // Просто smoke: не падает и выдаёт >= 2 токена + EOF [file:24]
        var toks = lex(input);
        assertTrue(toks.size() >= 3);
        assertEquals(TokenType.EOF, toks.getLast().type());
    }

    @Test
    void lex_error_single_ampersand() {
        assertThrows(LexerException.class, () -> lex("&"));
    }

    @Test
    void lex_error_single_pipe() {
        assertThrows(LexerException.class, () -> lex("|"));
    }

    @Test
    void lex_error_unexpected_char() {
        assertThrows(LexerException.class, () -> lex("@"));
    }

    @Test
    void lex_error_unterminated_string() {
        assertThrows(LexerException.class, () -> lex("\"abc"));
        assertThrows(LexerException.class, () -> lex("\"ab\nc\"")); // по коду это тоже ошибка [file:24]
    }
}
