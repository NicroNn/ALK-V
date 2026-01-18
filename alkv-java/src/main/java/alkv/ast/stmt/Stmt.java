package alkv.ast.stmt;

public sealed interface Stmt
        permits BlockStmt, ExprStmt, ForRangeStmt, ForStmt, IfStmt, ReturnStmt, VarDeclStmt, WhileStmt, SwitchStmt {}
