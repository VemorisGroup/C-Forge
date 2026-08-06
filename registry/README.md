# Registro público de paquetes C-Forge

URL oficial del registro: **[pkg.c-forge.org](https://pkg.c-forge.org)**

Este directorio define el índice público, auditable y versionado del gestor `cforge pkg`.
Cada versión del formato 2 debe publicar una URL HTTPS, el SHA-256 exacto del
archivo `.tar.gz`, la clave pública Ed25519, su `key_id` y la firma. El cliente
verifica todos esos campos antes de extraer el paquete y rechaza versiones o claves
incluidas en `revocations`.

La publicación se realiza mediante pull request para conservar revisión, historial y
protecciones de rama. `cforge pkg build` crea el archivo y su digest; ningún paquete se
ejecuta durante instalación. El cliente rechaza HTTP, rutas ascendentes, enlaces,
archivos mayores a 32 MiB y hashes incorrectos.

El publicador crea su identidad mediante `cforge pkg keygen` y firma el archivo con
`cforge pkg sign archivo.tar.gz clave.pem nombre versión`. La clave privada nunca
se publica. La cuenta del publicador sigue representada por su identidad GitHub y
la revisión del pull request; un servicio de cuentas independiente aún no está
operativo.

El registro está vacío hasta que el primer paquete sea revisado y aceptado. Esto evita
presentar paquetes de ejemplo como dependencias oficiales.

`publishers` contiene identidades aprobadas, claves y estado. El instalador exige
que cada paquete señale un publicador `active` y que su `key_id` esté autorizada
por esa cuenta. Esta fase usa
identidades de GitHub y revisión por pull request; todavía no existe un servicio
central de inicio de sesión, recuperación de cuenta ni publicación automática.
