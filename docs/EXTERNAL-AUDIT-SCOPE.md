# Alcance solicitado para auditoría externa

La auditoría debe ser realizada por una persona u organización independiente. Este
documento prepara el trabajo y evita declarar una certificación inexistente.
Todavía no ha sido contratada ni realizada; el equipo de C-Forge no puede
autoemitir un resultado independiente.

## Componentes prioritarios

1. Lexer, parser, análisis de tipos, bytecode y límites de la VM.
2. `extern`, ejecución de procesos, archivos, sockets y descargas.
3. Primitivas nativas de procesos, archivos, sockets y criptografía.
4. Forge Shared Arena: validación de offsets, concurrencia y ciclo de vida.
5. Futuro gestor de paquetes: índice, firmas, extracción y rutas.
6. Instaladores, actualizaciones, CI y artefactos de lanzamiento.

## Entregables exigidos

- Modelo de amenazas y superficies de confianza.
- Hallazgos con severidad, reproducción y corrección recomendada.
- Revisión de las correcciones y reporte final publicable.
- Versiones, commits, plataformas y herramientas utilizadas.

Hasta completar esos entregables, C-Forge 2.6 no se considerará certificado
para bancos ni otras cargas reguladas. El núcleo general conserva su versión
estable.
