# C-Forge fish completions
# Instalar: cp completions/cforge.fish ~/.config/fish/completions/

# Deshabilitar completions de archivo por defecto
complete -c cforge -f

# Subcomandos principales
complete -c cforge -n '__fish_use_subcommand' -a 'run'     -d 'Ejecutar archivo .cfv'
complete -c cforge -n '__fish_use_subcommand' -a 'build'   -d 'Build del proyecto'
complete -c cforge -n '__fish_use_subcommand' -a 'test'    -d 'Ejecutar tests'
complete -c cforge -n '__fish_use_subcommand' -a 'fmt'     -d 'Formatear código'
complete -c cforge -n '__fish_use_subcommand' -a 'lint'    -d 'Analizar código'
complete -c cforge -n '__fish_use_subcommand' -a 'docs'    -d 'Generar documentación'
complete -c cforge -n '__fish_use_subcommand' -a 'watch'   -d 'Hot reload'
complete -c cforge -n '__fish_use_subcommand' -a 'repl'    -d 'REPL interactivo'
complete -c cforge -n '__fish_use_subcommand' -a 'pkg'     -d 'Gestión de paquetes'
complete -c cforge -n '__fish_use_subcommand' -a 'new'     -d 'Crear nuevo proyecto'
complete -c cforge -n '__fish_use_subcommand' -a 'check'   -d 'Verificar instalación'
complete -c cforge -n '__fish_use_subcommand' -a 'version' -d 'Mostrar versión'

# run — archivos .cfv
complete -c cforge -n '__fish_seen_subcommand_from run' -a '(ls *.cfv 2>/dev/null)'

# build
complete -c cforge -n '__fish_seen_subcommand_from build' -l release  -d 'Modo producción'
complete -c cforge -n '__fish_seen_subcommand_from build' -l output -s o -d 'Directorio de salida' -r

# test
complete -c cforge -n '__fish_seen_subcommand_from test' -a '(ls tests/*.cfv 2>/dev/null)'
complete -c cforge -n '__fish_seen_subcommand_from test' -l watch   -d 'Modo watch'
complete -c cforge -n '__fish_seen_subcommand_from test' -l json    -d 'Salida JSON'
complete -c cforge -n '__fish_seen_subcommand_from test' -l tap     -d 'Salida TAP'
complete -c cforge -n '__fish_seen_subcommand_from test' -s v       -d 'Verbose'

# fmt
complete -c cforge -n '__fish_seen_subcommand_from fmt' -a '(ls **/*.cfv 2>/dev/null)'
complete -c cforge -n '__fish_seen_subcommand_from fmt' -l check    -d 'Solo verificar'
complete -c cforge -n '__fish_seen_subcommand_from fmt' -l stdout   -d 'Imprimir a stdout'
complete -c cforge -n '__fish_seen_subcommand_from fmt' -l quiet    -d 'Sin salida'

# lint
complete -c cforge -n '__fish_seen_subcommand_from lint' -a '(ls **/*.cfv 2>/dev/null)'
complete -c cforge -n '__fish_seen_subcommand_from lint' -l strict  -d 'Modo estricto'
complete -c cforge -n '__fish_seen_subcommand_from lint' -l json    -d 'Salida JSON'
complete -c cforge -n '__fish_seen_subcommand_from lint' -l ignore  -d 'Ignorar reglas' -r

# docs
complete -c cforge -n '__fish_seen_subcommand_from docs' -l serve   -d 'Servir localmente'
complete -c cforge -n '__fish_seen_subcommand_from docs' -l port    -d 'Puerto HTTP' -r
complete -c cforge -n '__fish_seen_subcommand_from docs' -l format  -d 'Formato' -a 'html markdown json'
complete -c cforge -n '__fish_seen_subcommand_from docs' -s o -l output -d 'Directorio salida' -r

# watch
complete -c cforge -n '__fish_seen_subcommand_from watch' -l cmd    -d 'Comando a ejecutar' -r
complete -c cforge -n '__fish_seen_subcommand_from watch' -l dir    -d 'Directorio' -r
complete -c cforge -n '__fish_seen_subcommand_from watch' -l delay  -d 'Delay ms' -r
complete -c cforge -n '__fish_seen_subcommand_from watch' -l clear  -d 'Limpiar pantalla'

# new
complete -c cforge -n '__fish_seen_subcommand_from new' -l template -s t -d 'Plantilla' -a 'cli web api lib'

# pkg subcomandos
complete -c cforge -n '__fish_seen_subcommand_from pkg' -a 'install'  -d 'Instalar paquete'
complete -c cforge -n '__fish_seen_subcommand_from pkg' -a 'remove'   -d 'Desinstalar'
complete -c cforge -n '__fish_seen_subcommand_from pkg' -a 'search'   -d 'Buscar'
complete -c cforge -n '__fish_seen_subcommand_from pkg' -a 'info'     -d 'Info'
complete -c cforge -n '__fish_seen_subcommand_from pkg' -a 'list'     -d 'Listar'
complete -c cforge -n '__fish_seen_subcommand_from pkg' -a 'lock'     -d 'Ver lock'
complete -c cforge -n '__fish_seen_subcommand_from pkg' -a 'ci'       -d 'Instalar desde lock'
complete -c cforge -n '__fish_seen_subcommand_from pkg' -a 'verify'   -d 'Verificar'
complete -c cforge -n '__fish_seen_subcommand_from pkg' -a 'publish'  -d 'Publicar'
complete -c cforge -n '__fish_seen_subcommand_from pkg' -a 'serve'    -d 'Servidor registry'
