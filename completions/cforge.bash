#!/usr/bin/env bash

_cforge_completions() {
    local current previous
    current="${COMP_WORDS[COMP_CWORD]}"
    previous="${COMP_WORDS[COMP_CWORD-1]}"
    case "$previous" in
        run|check|test|fmt)
            COMPREPLY=($(compgen -f -X '!*.cfv' -- "$current"))
            ;;
        *)
            COMPREPLY=($(compgen -W "run check test fmt repl --help --version" -- "$current"))
            ;;
    esac
}

complete -F _cforge_completions cforge cforgev
