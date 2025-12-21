package alkv.ast.stmt;

public sealed interface Stmt
        permits BlockStmt, IfStmt, WhileStmt, ForStmt,
        ReturnStmt, VarDeclStmt, ExprStmt {}
