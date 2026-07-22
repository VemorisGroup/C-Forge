# Seguridad de C-Forge

## Versiones

`1.5.0-developer-preview` recibe correcciones durante su desarrollo, pero no posee
todavía certificación para entornos críticos. No ejecute scripts `.cfv` desconocidos:
los puentes `extern`, procesos y red pueden acceder a capacidades del sistema.

## Informar vulnerabilidades

No publiques detalles explotables en un issue público. Contacta a
`hola@vemorisgroup.com` indicando versión, plataforma, reproducción mínima e impacto.
Vemoris Group debe confirmar recepción, coordinar una corrección y publicar un aviso
cuando exista una versión reparada.

## Controles automatizados

- CodeQL para Python y C/C++ en cada cambio y semanalmente.
- Auditoría de dependencias Python con `pip-audit`.
- Límites de tamaño, HTTPS y SHA-256 para paquetes.
- Rechazo de enlaces y rutas ascendentes al extraer paquetes.
- Suite multiplataforma en macOS, Ubuntu y Windows.

Estos controles no sustituyen una auditoría independiente.
