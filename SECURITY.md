# Seguridad de C-Forge

## Versiones

`2.6.x` es la rama estable mantenida y recibe correcciones de seguridad. No posee
certificación regulatoria para entornos críticos. No ejecute scripts `.cfv` desconocidos:
los procesos, archivos y la red pueden acceder a capacidades del sistema.

## Informar vulnerabilidades

No publiques detalles explotables en un issue público. Contacta a
`hola@vemorisgroup.com` indicando versión, plataforma, reproducción mínima e impacto.
Vemoris Group debe confirmar recepción, coordinar una corrección y publicar un aviso
cuando exista una versión reparada.

## Controles automatizados

- CodeQL para C++ en cada cambio y semanalmente.
- Compilación sin avisos, pruebas nativas `.cfv`, ASan y UBSan.
- Límites de tamaño, HTTPS y SHA-256 para paquetes.
- Rechazo de enlaces y rutas ascendentes al extraer paquetes.
- Suite multiplataforma en macOS, Ubuntu y Windows.

Estos controles no sustituyen una auditoría independiente.
