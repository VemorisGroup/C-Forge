#compdef cforge cfmt cflint cftest cfbuild
# C-Forge zsh completions
# Instalar: fpath=(completions $fpath) && compinit
# O copiar a un directorio en $fpath como /usr/local/share/zsh/site-functions/_cforge

_cforge() {
    local state

    _arguments -C \
        '1:comando:->cmds' \
        '*::opciones:->opts' \
        && return 0

    case $state in
        cmds)
            local -a commands
            commands=(
                'run:Ejecutar archivo .cfv'
                'build:Build del proyecto'
                'test:Ejecutar tests'
                'fmt:Formatear código'
                'lint:Analizar código estático'
                'docs:Generar documentación'
                'watch:Hot reload'
                'repl:REPL interactivo'
                'pkg:Gestión de paquetes'
                'new:Crear nuevo proyecto'
                'check:Verificar instalación'
                'version:Mostrar versión'
            )
            _describe 'comandos' commands
            ;;
        opts)
            case $words[1] in
                run)
                    _arguments '*:archivo:_files -g "*.cfv"'
                    ;;
                build)
                    _arguments \
                        '--release[Modo producción]' \
                        '-o[Directorio de salida]:dir:_dirs' \
                        '--output[Directorio de salida]:dir:_dirs'
                    ;;
                test)
                    _arguments \
                        '*:archivo:_files -g "*.cfv"' \
                        '--watch[Modo watch]' \
                        '--json[Salida JSON]' \
                        '--tap[Salida TAP]' \
                        '-v[Verbose]' \
                        '--verbose[Verbose]'
                    ;;
                fmt)
                    _arguments \
                        '*:archivo:_files -g "*.cfv"' \
                        '--check[Solo verificar]' \
                        '--stdout[Imprimir a stdout]' \
                        '--quiet[Silencioso]'
                    ;;
                lint)
                    _arguments \
                        '*:archivo:_files -g "*.cfv"' \
                        '--strict[Modo estricto]' \
                        '--json[Salida JSON]' \
                        '--ignore[Ignorar reglas]:reglas:' \
                        '-v[Verbose]'
                    ;;
                docs)
                    _arguments \
                        '1:directorio:_dirs' \
                        '--serve[Servir localmente]' \
                        '--port[Puerto HTTP]:puerto:' \
                        '--format[Formato de salida]:(html markdown json)' \
                        '-o[Directorio de salida]:dir:_dirs'
                    ;;
                watch)
                    _arguments \
                        '1:archivo:_files -g "*.cfv"' \
                        '--cmd[Comando a ejecutar]:cmd:' \
                        '--dir[Directorio a observar]:dir:_dirs' \
                        '--delay[Delay en ms]:ms:' \
                        '--clear[Limpiar pantalla]'
                    ;;
                new)
                    _arguments \
                        '1:nombre:' \
                        '--template[Plantilla]:(cli web api lib)' \
                        '-t[Plantilla]:(cli web api lib)'
                    ;;
                pkg)
                    local -a pkg_cmds
                    pkg_cmds=(
                        'install:Instalar paquete'
                        'remove:Desinstalar paquete'
                        'search:Buscar paquetes'
                        'info:Info de paquete'
                        'list:Listar paquetes'
                        'lock:Ver lock file'
                        'ci:Instalar desde lock'
                        'verify:Verificar integridad'
                        'publish:Publicar paquete'
                        'serve:Iniciar servidor registry'
                    )
                    _arguments '1:subcomando:->pkg_sub' '*::pkg_opts:'
                    case $state in
                        pkg_sub) _describe 'pkg' pkg_cmds ;;
                    esac
                    ;;
            esac
            ;;
    esac
}

_cfmt() {
    _arguments \
        '*:archivo:_files -g "*.cfv"' \
        '--check[Solo verificar, no modificar]' \
        '--stdout[Imprimir a stdout]' \
        '--quiet[Sin salida]'
}

_cflint() {
    _arguments \
        '*:archivo:_files -g "*.cfv"' \
        '--strict[Modo estricto]' \
        '--json[Salida JSON]' \
        '--ignore[Ignorar reglas]:reglas:' \
        '--only[Solo estas reglas]:reglas:'
}

_cftest() {
    _arguments \
        '*:archivo:_files -g "*.cfv"' \
        '--watch[Modo watch]' \
        '--json[Salida JSON]' \
        '--tap[Salida TAP]' \
        '-v[Verbose]'
}

_cfbuild() {
    _arguments \
        '1:subcomando:(build clean init)' \
        '--mode[Modo]:(dev prod)' \
        '-o[Salida]:dir:_dirs'
}

compdef _cforge  cforge
compdef _cfmt    cfmt
compdef _cflint  cflint
compdef _cftest  cftest
compdef _cfbuild cfbuild
