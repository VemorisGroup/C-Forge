#compdef cforge cforgev

_cforge() {
  local -a commands
  commands=(
    'run:Ejecutar un archivo .cfv'
    'check:Verificar sintaxis'
    'test:Ejecutar pruebas C-Forge'
    'fmt:Validar formato y sintaxis'
    'repl:Abrir consola interactiva'
  )

  _arguments \
    '(-h --help)'{-h,--help}'[Mostrar ayuda]' \
    '(-V --version)'{-V,--version}'[Mostrar versión]' \
    '1:comando:->command' \
    '*:archivo:_files -g "*.cfv"'

  if [[ $state == command ]]; then
    _describe 'comandos' commands
  fi
}

compdef _cforge cforge cforgev
