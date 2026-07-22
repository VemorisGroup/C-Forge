# Registro público de paquetes C-Forge

Este directorio define el índice público, auditable y versionado del gestor `cforge pkg`.
Cada versión debe publicar una URL HTTPS y el SHA-256 exacto del archivo `.tar.gz`.

La publicación se realiza mediante pull request para conservar revisión, historial y
protecciones de rama. `cforge pkg build` crea el archivo y su digest; ningún paquete se
ejecuta durante instalación. El cliente rechaza HTTP, rutas ascendentes, enlaces,
archivos mayores a 32 MiB y hashes incorrectos.

El registro está vacío hasta que el primer paquete sea revisado y aceptado. Esto evita
presentar paquetes de ejemplo como dependencias oficiales.
