complete -c cforge -f
complete -c cforge -n '__fish_use_subcommand' -a run -d 'Ejecutar archivo .cfv'
complete -c cforge -n '__fish_use_subcommand' -a check -d 'Verificar sintaxis'
complete -c cforge -n '__fish_use_subcommand' -a test -d 'Ejecutar pruebas'
complete -c cforge -n '__fish_use_subcommand' -a fmt -d 'Validar formato'
complete -c cforge -n '__fish_use_subcommand' -a repl -d 'Abrir REPL'
complete -c cforge -s h -l help -d 'Mostrar ayuda'
complete -c cforge -s V -l version -d 'Mostrar versión'

for command in run check test fmt
    complete -c cforge -n "__fish_seen_subcommand_from $command" -a '(__fish_complete_suffix .cfv)'
end
