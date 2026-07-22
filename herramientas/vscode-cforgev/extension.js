"use strict";

const vscode = require("vscode");
const { execFile } = require("child_process");

const words = [
  "sea", "si", "sino", "mientras", "funcion", "retornar", "estructura",
  "clase", "campo", "metodo", "intentar", "capturar", "gpu", "cluster",
  "test", "mostrar", "print", "verdadero", "falso", "nulo", "file_read",
  "file_write", "json_parse", "sys_fetch", "forge_hash", "forge_bench"
];

function runCForge(args, callback) {
  execFile("cforge", args, { timeout: 30000, maxBuffer: 4 * 1024 * 1024 }, callback);
}

function activate(context) {
  const diagnostics = vscode.languages.createDiagnosticCollection("cforge");
  context.subscriptions.push(diagnostics);

  function check(document) {
    if (document.languageId !== "cforgev" || document.isUntitled) return;
    runCForge(["check", document.uri.fsPath, "--json"], (error, stdout) => {
      let values = [];
      try { values = JSON.parse(stdout || "[]"); } catch (_) { return; }
      diagnostics.set(document.uri, values.map(item => {
        const line = Math.max(0, Number(item.line || 1) - 1);
        const column = Math.max(0, Number(item.column || 1) - 1);
        const diagnostic = new vscode.Diagnostic(
          new vscode.Range(line, column, line, column + 1),
          `${item.code}: ${item.message}`,
          item.severity === "error" ? vscode.DiagnosticSeverity.Error : vscode.DiagnosticSeverity.Warning
        );
        diagnostic.source = "C-Forge";
        diagnostic.code = item.code;
        return diagnostic;
      }));
    });
  }

  context.subscriptions.push(vscode.workspace.onDidSaveTextDocument(check));
  if (vscode.window.activeTextEditor) check(vscode.window.activeTextEditor.document);
  context.subscriptions.push(vscode.languages.registerCompletionItemProvider("cforgev", {
    provideCompletionItems() {
      return words.map(word => new vscode.CompletionItem(word, vscode.CompletionItemKind.Keyword));
    }
  }));
  context.subscriptions.push(vscode.commands.registerCommand("cforge.checkFile", () => {
    const editor = vscode.window.activeTextEditor;
    if (editor) { check(editor.document); vscode.window.showInformationMessage("C-Forge: comprobación finalizada"); }
  }));
  context.subscriptions.push(vscode.commands.registerCommand("cforge.runFile", () => {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.isUntitled) return;
    const terminal = vscode.window.createTerminal("C-Forge");
    terminal.show();
    terminal.sendText(`cforge ${JSON.stringify(editor.document.uri.fsPath)}`);
  }));
  context.subscriptions.push(vscode.commands.registerCommand("cforge.debugBreakpoint", async () => {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.isUntitled) return;
    const offset = await vscode.window.showInputBox({ prompt: "Offset de bytecode", value: "0", validateInput: value => /^\d+$/.test(value) ? undefined : "Escribe un número" });
    if (offset === undefined) return;
    const terminal = vscode.window.createTerminal("C-Forge Debug");
    terminal.show();
    terminal.sendText(`cforge debug ${JSON.stringify(editor.document.uri.fsPath)} --break ${offset}`);
  }));
}

function deactivate() {}

module.exports = { activate, deactivate };
