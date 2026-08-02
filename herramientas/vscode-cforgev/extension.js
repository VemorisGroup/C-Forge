"use strict";

// Integración estable de VS Code. Solo usa comandos públicos de C-Forge 2.6:
// check y ejecución de archivos. LSP y DAP no se anuncian hasta que formen
// parte del contrato probado del CLI.
const vscode = require("vscode");
const { execFile } = require("child_process");

const words = [
  "sea", "si", "sino", "mientras", "para", "en", "funcion", "retornar",
  "clase", "esto", "intentar", "capturar", "lanzar", "importar", "mostrar",
  "verdadero", "falso", "nulo", "afirmar", "agregar", "longitud"
];

function runCForge(args, callback) {
  execFile("cforge", args, { timeout: 30000, maxBuffer: 4 * 1024 * 1024 }, callback);
}

function activate(context) {
  const diagnostics = vscode.languages.createDiagnosticCollection("cforge");
  context.subscriptions.push(diagnostics);

  function check(document, notify = false) {
    if (document.languageId !== "cforgev" || document.isUntitled) return;
    runCForge(["check", document.uri.fsPath], (error, stdout, stderr) => {
      diagnostics.delete(document.uri);
      if (error) {
        const text = (stderr || stdout || error.message).trim();
        const match = /línea\s+(\d+)/i.exec(text);
        const line = Math.max(0, Number(match?.[1] || 1) - 1);
        diagnostics.set(document.uri, [new vscode.Diagnostic(
          new vscode.Range(line, 0, line, Number.MAX_SAFE_INTEGER),
          text,
          vscode.DiagnosticSeverity.Error
        )]);
        if (notify) vscode.window.showErrorMessage("C-Forge: el archivo contiene errores");
      } else if (notify) {
        vscode.window.showInformationMessage("C-Forge: sintaxis válida");
      }
    });
  }

  context.subscriptions.push(vscode.workspace.onDidSaveTextDocument(document => check(document)));
  context.subscriptions.push(vscode.languages.registerCompletionItemProvider("cforgev", {
    provideCompletionItems() {
      return words.map(word => new vscode.CompletionItem(word, vscode.CompletionItemKind.Keyword));
    }
  }));

  context.subscriptions.push(vscode.commands.registerCommand("cforge.checkFile", () => {
    const editor = vscode.window.activeTextEditor;
    if (editor) check(editor.document, true);
  }));

  context.subscriptions.push(vscode.commands.registerCommand("cforge.runFile", () => {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.isUntitled) return;
    const terminal = vscode.window.createTerminal("C-Forge");
    terminal.show();
    terminal.sendText(`cforge ${JSON.stringify(editor.document.uri.fsPath)}`);
  }));
}

function deactivate() {}

module.exports = { activate, deactivate };
