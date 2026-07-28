# C-Forge LSP 1.0

El servidor se inicia con `cforge lsp` y habla JSON-RPC/LSP 3.17 por `stdio`.
Expone sincronización completa de documentos, diagnósticos estructurados, completion,
hover, símbolos de documento, definición, referencias, renombrado y formateo. Editores
compatibles deben configurar el comando `cforge` con el argumento `lsp`.

Los códigos actuales son `CF0001` (archivo), `CF1001` (lexer), `CF1002` (parser) y
`CF2001` (tipos). La siguiente fase del protocolo contempla navegación de símbolos,
El servidor evita renombrados con identificadores inválidos. La depuración visual utiliza
un protocolo separado y requiere mapas fuente antes de considerarse estable.
