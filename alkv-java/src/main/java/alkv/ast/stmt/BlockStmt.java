package alkv.ast.stmt;

import java.util.List;

public record BlockStmt(List<Stmt> statements) implements Stmt {}
