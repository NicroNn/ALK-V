package alkv.parser;

import alkv.ast.Program;
import alkv.ast.decl.ClassDecl;
import alkv.ast.decl.FunctionDecl;
import alkv.ast.expr.*;
import alkv.ast.stmt.BlockStmt;
import alkv.ast.stmt.ExprStmt;
import alkv.ast.stmt.ForRangeStmt;
import alkv.ast.stmt.ForStmt;
import alkv.ast.stmt.IfStmt;
import alkv.ast.stmt.ReturnStmt;
import alkv.ast.stmt.Stmt;
import alkv.ast.stmt.VarDeclStmt;
import alkv.ast.stmt.WhileStmt;
import alkv.ast.type.ArrayTypeRef;
import alkv.ast.type.NamedTypeRef;
import alkv.ast.type.PrimitiveTypeRef;
import alkv.ast.type.TypeRef;
import alkv.lexer.Token;
import alkv.lexer.TokenType;
import alkv.ast.decl.FieldDecl;
import alkv.ast.decl.MethodDecl;
import alkv.ast.decl.ConstructorDecl;

import java.util.ArrayList;
import java.util.List;

public final class Parser {
    private final List<Token> tokens;
    private int pos = 0;

    public Parser(List<Token> tokens) {
        this.tokens = tokens;
    }

    // ---------- entry ----------
    public Program parseProgram() {
        List<FunctionDecl> functions = new ArrayList<>();
        List<ClassDecl> classes = new ArrayList<>();

        while (!check(TokenType.EOF)) {
            if (match(TokenType.FNC)) functions.add(parseFunctionDecl());
            else if (match(TokenType.CLASS)) classes.add(parseClassDecl());
            else throw error(peek(), "Expected 'fnc' or 'class' at top-level");
        }
        consume(TokenType.EOF, "Expected EOF");
        return new Program(functions, classes);
    }

    // ---------- function ----------
    private FunctionDecl parseFunctionDecl() {
        Token name = consume(TokenType.IDENTIFIER, "Expected function name");
        consume(TokenType.COLON, "Expected ':' after function name");
        TypeRef retType = parseTypeRef();

        consume(TokenType.LPAREN, "Expected '(' after return type");
        List<FunctionDecl.Param> params = parseParamsOpt();
        consume(TokenType.RPAREN, "Expected ')' after parameters");

        BlockStmt body = parseBlock();
        return new FunctionDecl(name.lexeme(), retType, params, body);
    }

    private List<FunctionDecl.Param> parseParamsOpt() {
        if (check(TokenType.RPAREN)) return List.of();
        List<FunctionDecl.Param> ps = new ArrayList<>();
        do {
            Token n = consume(TokenType.IDENTIFIER, "Expected parameter name");
            consume(TokenType.COLON, "Expected ':' after parameter name");
            TypeRef t = parseTypeRef();
            ps.add(new FunctionDecl.Param(n.lexeme(), t));
        } while (match(TokenType.COMMA));
        return ps;
    }

    // ---------- class ----------
    private ClassDecl parseClassDecl() {
        Token classNameTok = consume(TokenType.IDENTIFIER, "Expected class name");
        String className = classNameTok.lexeme();

        consume(TokenType.LBRACE, "Expected '{' after class name");

        List<FieldDecl> fields = new ArrayList<>();
        List<MethodDecl> methods = new ArrayList<>();
        List<ConstructorDecl> constructors = new ArrayList<>();

        boolean currentPublic = false; // default private

        while (!check(TokenType.RBRACE) && !check(TokenType.EOF)) {

            // public: / private:
            if (check(TokenType.PUBLIC) || check(TokenType.PRIVATE)) {
                currentPublic = parseAccessLabel();
                continue;
            }

            // constructor: ClassName(params) { ... }
            if (check(TokenType.IDENTIFIER)
                    && peek().lexeme().equals(className)
                    && checkNext(TokenType.LPAREN)) {
                constructors.add(parseConstructorDecl(className, currentPublic));
                continue;
            }

            // field/method
            Token memberName = consume(TokenType.IDENTIFIER, "Expected field/method name");
            consume(TokenType.COLON, "Expected ':' after member name");
            TypeRef t = parseTypeRef();

            // method: name : type (params) { ... }
            if (match(TokenType.LPAREN)) {
                List<FunctionDecl.Param> params = parseParamsOpt();
                consume(TokenType.RPAREN, "Expected ')' after parameters");
                BlockStmt body = parseBlock();
                methods.add(new MethodDecl(memberName.lexeme(), t, params, body, currentPublic));
                continue;
            }

            // field: name : type ;
            consume(TokenType.SEMICOLON, "Expected ';' after field declaration");
            fields.add(new FieldDecl(memberName.lexeme(), t, currentPublic));
        }

        consume(TokenType.RBRACE, "Expected '}' after class body");
        return new ClassDecl(className, fields, methods, constructors);
    }

    private boolean parseAccessLabel() {
        boolean isPublic;
        if (match(TokenType.PUBLIC)) isPublic = true;
        else if (match(TokenType.PRIVATE)) isPublic = false;
        else throw error(peek(), "Expected 'public' or 'private'");

        consume(TokenType.COLON, "Expected ':' after access label");
        return isPublic;
    }

    private ConstructorDecl parseConstructorDecl(String className, boolean isPublic) {
        consume(TokenType.IDENTIFIER, "Expected constructor name"); // имя класса
        consume(TokenType.LPAREN, "Expected '(' after constructor name");
        List<FunctionDecl.Param> params = parseParamsOpt();
        consume(TokenType.RPAREN, "Expected ')' after parameters");
        BlockStmt body = parseBlock();
        return new ConstructorDecl(className, params, body, isPublic);
    }

    // ---------- block / statements ----------
    private BlockStmt parseBlock() {
        consume(TokenType.LBRACE, "Expected '{'");
        List<Stmt> stmts = new ArrayList<>();
        while (!check(TokenType.RBRACE) && !check(TokenType.EOF)) {
            stmts.add(parseStmt());
        }
        consume(TokenType.RBRACE, "Expected '}'");
        return new BlockStmt(stmts);
    }

    private Stmt parseStmt() {
        if (check(TokenType.LBRACE)) return parseBlock();

        if (match(TokenType.IF)) return parseIf();
        if (match(TokenType.WHILE)) return parseWhile();
        if (match(TokenType.FOR)) return parseFor();
        if (match(TokenType.RETURN)) return parseReturn();

        // varDecl: IDENTIFIER ':' ...
        if (check(TokenType.IDENTIFIER) && checkNext(TokenType.COLON)) {
            VarDeclStmt v = parseVarDeclStmtNoSemicolon();
            consume(TokenType.SEMICOLON, "Expected ';' after variable declaration");
            return v;
        }

        // expr stmt
        Expr e = parseExpr();
        consume(TokenType.SEMICOLON, "Expected ';' after expression");
        return new ExprStmt(e);
    }

    private VarDeclStmt parseVarDeclStmtNoSemicolon() {
        Token name = consume(TokenType.IDENTIFIER, "Expected variable name");
        consume(TokenType.COLON, "Expected ':' after variable name");
        TypeRef type = parseTypeRef();

        Expr init = null;
        if (match(TokenType.ASSIGN)) {
            init = parseExpr();
        }
        return new VarDeclStmt(name.lexeme(), type, init);
    }

    private IfStmt parseIf() {
        consume(TokenType.LPAREN, "Expected '(' after if");
        Expr cond = parseExpr();
        consume(TokenType.RPAREN, "Expected ')'");
        BlockStmt thenB = parseBlock();

        BlockStmt elseB = null;
        if (match(TokenType.ELSE)) {
            // В твоей AST IfStmt сейчас нет else-if цепочек (судя по файлу), поэтому
            // "else if" пока можно свернуть в else { if (...) { ... } } при желании.
            if (check(TokenType.IF)) {
                advance(); // съели IF
                Stmt nested = parseIf(); // вернёт IfStmt
                elseB = new BlockStmt(List.of(nested));
            } else {
                elseB = parseBlock();
            }
        }
        return new IfStmt(
                java.util.List.of(new IfStmt.Branch(cond, thenB)),
                elseB
        );
    }

    private WhileStmt parseWhile() {
        consume(TokenType.LPAREN, "Expected '(' after while");
        Expr cond = parseExpr();
        consume(TokenType.RPAREN, "Expected ')'");
        BlockStmt body = parseBlock();
        return new WhileStmt(cond, body);
    }

    private Stmt parseFor() {
        consume(TokenType.LPAREN, "Expected '(' after for");

        // range-for: for (IDENTIFIER IN expr RANGE expr) block
        if (check(TokenType.IDENTIFIER) && checkNext(TokenType.IN)) {
            Token var = consume(TokenType.IDENTIFIER, "Expected loop variable");
            consume(TokenType.IN, "Expected 'in'");
            Expr from = parseExpr();
            consume(TokenType.RANGE, "Expected '...' in range for");
            Expr to = parseExpr();
            consume(TokenType.RPAREN, "Expected ')'");
            BlockStmt body = parseBlock();

            return new ForRangeStmt(var.lexeme(), from, to, body);
        }

        // C-style for: for (init; cond; update) block
        Stmt init;
        if (match(TokenType.SEMICOLON)) {
            init = new ExprStmt(new IntLiteral(0)); // или отдельный EmptyStmt (лучше), у тебя его нет [file:9]
        } else if (check(TokenType.IDENTIFIER) && checkNext(TokenType.COLON)) {
            init = parseVarDeclStmtNoSemicolon();
            consume(TokenType.SEMICOLON, "Expected ';' after for-init var decl");
        } else {
            Expr initExpr = parseExpr();
            consume(TokenType.SEMICOLON, "Expected ';' after for-init");
            init = new ExprStmt(initExpr);
        }

        Expr cond = check(TokenType.SEMICOLON) ? new BoolLiteral(true) : parseExpr();
        consume(TokenType.SEMICOLON, "Expected ';' after for-condition");

        Expr update = check(TokenType.RPAREN) ? new IntLiteral(0) : parseExpr();
        consume(TokenType.RPAREN, "Expected ')' after for");
        BlockStmt body = parseBlock();

        return new ForStmt(init, cond, update, body);
    }

    private ReturnStmt parseReturn() {
        if (check(TokenType.SEMICOLON)) {
            advance();
            return new ReturnStmt(null);
        }
        Expr value = parseExpr();
        consume(TokenType.SEMICOLON, "Expected ';' after return");
        return new ReturnStmt(value);
    }

    // ---------- types ----------
    private TypeRef parseTypeRef() {
        // base type: int/float/bool/string/void or named identifier
        TypeRef base;
        if (match(TokenType.INT)) base = new PrimitiveTypeRef("int");
        else if (match(TokenType.FLOAT)) base = new PrimitiveTypeRef("float");
        else if (match(TokenType.BOOL)) base = new PrimitiveTypeRef("bool");
        else if (match(TokenType.STRING)) base = new PrimitiveTypeRef("string");
        else if (match(TokenType.VOID)) base = new PrimitiveTypeRef("void");
        else {
            Token n = consume(TokenType.IDENTIFIER, "Expected type name");
            base = new NamedTypeRef(n.lexeme());
        }

        // array suffix: [] or [N]
        while (match(TokenType.LBRACKET)) {
            Integer size = null;
            if (check(TokenType.INT_LITERAL)) {
                size = Integer.parseInt(advance().lexeme());
            }
            consume(TokenType.RBRACKET, "Expected ']'");
            base = new ArrayTypeRef(base, size);
        }
        return base;
    }

    // ---------- expressions (precedence climbing) ----------
    private Expr parseExpr() { return parseAssign(); }

    private Expr parseAssign() {
        Expr left = parseOr();
        if (match(TokenType.ASSIGN)) {
            Expr right = parseAssign(); // right-assoc

            if (!(left instanceof VarExpr
                    || left instanceof FieldAccessExpr
                    || left instanceof ArrayAccessExpr)) {
                throw error(previous(), "Invalid assignment target");
            }
            return new AssignExpr(left, right);
        }
        return left;
    }


    private Expr parseOr() {
        Expr e = parseAnd();
        while (match(TokenType.OR)) {
            Token op = previous();
            Expr r = parseAnd();
            e = new BinaryExpr(e, toBinOp(op.type()), r);
        }
        return e;
    }

    private Expr parseAnd() {
        Expr e = parseCompare();
        while (match(TokenType.AND)) {
            Token op = previous();
            Expr r = parseCompare();
            e = new BinaryExpr(e, toBinOp(op.type()), r);
        }
        return e;
    }

    private Expr parseCompare() {
        Expr e = parseAdd();
        while (match(TokenType.LT, TokenType.LE, TokenType.GT, TokenType.GE, TokenType.EQ, TokenType.NEQ)) {
            Token op = previous();
            Expr r = parseAdd();
            e = new BinaryExpr(e, toBinOp(op.type()), r);
        }
        return e;
    }

    private Expr parseAdd() {
        Expr e = parseMul();
        while (match(TokenType.PLUS, TokenType.MINUS)) {
            Token op = previous();
            Expr r = parseMul();
            e = new BinaryExpr(e, toBinOp(op.type()), r);
        }
        return e;
    }

    private Expr parseMul() {
        Expr e = parseUnary();
        while (match(TokenType.STAR, TokenType.SLASH, TokenType.PERCENT)) {
            Token op = previous();
            Expr r = parseUnary();
            e = new BinaryExpr(e, toBinOp(op.type()), r);
        }
        return e;
    }

    private Expr parseUnary() {
        if (match(TokenType.NOT)) {
            return new UnaryExpr(UnaryExpr.Operator.NOT, parseUnary());
        }
        if (match(TokenType.MINUS)) {
            return new UnaryExpr(UnaryExpr.Operator.NEG, parseUnary());
        }
        return parsePostfix();
    }

    private Expr parsePostfix() {
        Expr e = parsePrimary();
        while (true) {
            if (match(TokenType.LPAREN)) {
                List<Expr> args = new ArrayList<>();
                if (!check(TokenType.RPAREN)) {
                    do { args.add(parseExpr()); } while (match(TokenType.COMMA));
                }
                consume(TokenType.RPAREN, "Expected ')'");
                e = new CallExpr(e, args);
                continue;
            }
            if (match(TokenType.LBRACKET)) {
                Expr idx = parseExpr();
                consume(TokenType.RBRACKET, "Expected ']'");
                e = new ArrayAccessExpr(e, idx);
                continue;
            }
            if (match(TokenType.DOT)) {
                Token name = consume(TokenType.IDENTIFIER, "Expected field name after '.'");
                e = new FieldAccessExpr(e, name.lexeme());
                continue;
            }
            break;
        }
        return e;
    }

    private Expr parseArrayLiteral() {
        consume(TokenType.LBRACKET, "Expected '['");
        List<Expr> elems = new ArrayList<>();
        if (!check(TokenType.RBRACKET)) {
            do {
                elems.add(parseExpr());
            } while (match(TokenType.COMMA));
        }
        consume(TokenType.RBRACKET, "Expected ']'");
        return new ArrayLiteralExpr(elems);
    }

    private Expr parsePrimary() {
        if (check(TokenType.LBRACKET)) {
            return parseArrayLiteral();
        }
        if (match(TokenType.INT_LITERAL)) return new IntLiteral(Integer.parseInt(previous().lexeme()));
        if (match(TokenType.STRING_LITERAL)) return new StringLiteral(previous().lexeme());
        if (match(TokenType.BOOL_LITERAL)) return new BoolLiteral("T".equals(previous().lexeme()));
        if (match(TokenType.IDENTIFIER)) return new VarExpr(previous().lexeme());
        if (match(TokenType.LPAREN)) {
            Expr e = parseExpr();
            consume(TokenType.RPAREN, "Expected ')'");
            return e;
        }
        throw error(peek(), "Expected expression");
    }

    // ---------- helpers ----------
    private boolean match(TokenType... types) {
        for (TokenType t : types) {
            if (check(t)) { advance(); return true; }
        }
        return false;
    }

    private Token consume(TokenType t, String msg) {
        if (check(t)) return advance();
        throw error(peek(), msg);
    }

    private boolean check(TokenType t) {
        return peek().type() == t;
    }

    private boolean checkNext(TokenType t) {
        if (pos + 1 >= tokens.size()) return false;
        return tokens.get(pos + 1).type() == t;
    }

    private Token advance() {
        if (!check(TokenType.EOF)) pos++;
        return previous();
    }

    private Token peek() { return tokens.get(pos); }
    private Token previous() { return tokens.get(pos - 1); }

    private RuntimeException error(Token at, String msg) {
        return new RuntimeException("[" + at.line() + ":" + at.column() + "] " + msg + " (got " + at.type() + " '" + at.lexeme() + "')");
    }

    private static BinaryExpr.Operator toBinOp(TokenType t) {
        return switch (t) {
            case PLUS    -> BinaryExpr.Operator.ADD;
            case MINUS   -> BinaryExpr.Operator.SUB;
            case STAR    -> BinaryExpr.Operator.MUL;
            case SLASH   -> BinaryExpr.Operator.DIV;
            case PERCENT -> BinaryExpr.Operator.MOD;

            case EQ  -> BinaryExpr.Operator.EQ;
            case NEQ -> BinaryExpr.Operator.NE;
            case LT  -> BinaryExpr.Operator.LT;
            case GT  -> BinaryExpr.Operator.GT;
            case LE  -> BinaryExpr.Operator.LE;
            case GE  -> BinaryExpr.Operator.GE;

            case AND -> BinaryExpr.Operator.AND;
            case OR  -> BinaryExpr.Operator.OR;

            default -> throw new IllegalArgumentException("Not a binary operator token: " + t);
        };
    }

}
