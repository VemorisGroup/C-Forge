#!/usr/bin/env bash
# C-Forge bash completions
# Instalar: source completions/cforge.bash
# O: echo 'source /usr/local/share/cforge/completions/cforge.bash' >> ~/.bashrc

_cforge_completions() {
    local cur prev words cword
    _init_completion || return

    local cmds="run build test fmt lint docs watch repl pkg new check version"
    local pkg_cmds="install remove search info list lock ci verify serve publish"
    local templates="cli web api lib"

    case "$prev" in
        cforge)
            COMPREPLY=($(compgen -W "$cmds" -- "$cur"))
            return ;;
        run)
            COMPREPLY=($(compgen -f -X '!*.cfv' -- "$cur"))
            return ;;
        pkg)
            COMPREPLY=($(compgen -W "$pkg_cmds" -- "$cur"))
            return ;;
        install|remove|info)
            # Could complete from cforge.lock
            if [[ -f cforge.lock ]]; then
                local pkgs=$(python3 -c "import json; d=json.load(open('cforge.lock')); print('\n'.join(d.get('paquetes',{}).keys()))" 2>/dev/null)
                COMPREPLY=($(compgen -W "$pkgs" -- "$cur"))
            fi
            return ;;
        --template|-t)
            COMPREPLY=($(compgen -W "$templates" -- "$cur"))
            return ;;
        fmt|lint)
            COMPREPLY=($(compgen -f -X '!*.cfv' -- "$cur") $(compgen -d -- "$cur"))
            return ;;
        --format)
            COMPREPLY=($(compgen -W "html markdown json" -- "$cur"))
            return ;;
        --target)
            COMPREPLY=($(compgen -W "c js js-html wasm wasm-js" -- "$cur"))
            return ;;
    esac

    case "${words[1]}" in
        run)
            COMPREPLY=($(compgen -f -X '!*.cfv' -- "$cur"))
            ;;
        build)
            COMPREPLY=($(compgen -W "--release --output" -- "$cur"))
            ;;
        test)
            COMPREPLY=($(compgen -f -X '!*.cfv' -- "$cur") $(compgen -W "--watch --json --tap -v" -- "$cur"))
            ;;
        fmt)
            COMPREPLY=($(compgen -f -X '!*.cfv' -- "$cur") $(compgen -W "--check --stdout" -- "$cur"))
            ;;
        lint)
            COMPREPLY=($(compgen -f -X '!*.cfv' -- "$cur") $(compgen -W "--strict --json --ignore" -- "$cur"))
            ;;
        docs)
            COMPREPLY=($(compgen -W "--serve --port --format --output" -- "$cur"))
            ;;
        watch)
            COMPREPLY=($(compgen -f -X '!*.cfv' -- "$cur") $(compgen -W "--cmd --dir --delay --clear" -- "$cur"))
            ;;
        new)
            if [[ $cword -eq 2 ]]; then
                COMPREPLY=()
            else
                COMPREPLY=($(compgen -W "--template -t" -- "$cur"))
            fi
            ;;
        pkg)
            if [[ $cword -eq 2 ]]; then
                COMPREPLY=($(compgen -W "$pkg_cmds" -- "$cur"))
            fi
            ;;
        *)
            COMPREPLY=($(compgen -W "$cmds" -- "$cur"))
            ;;
    esac
}

complete -F _cforge_completions cforge

# Completions for standalone tools
_cfmt_completions() {
    local cur prev
    _init_completion || return
    case "$prev" in
        cfmt) COMPREPLY=($(compgen -f -X '!*.cfv' -- "$cur") $(compgen -d -- "$cur") $(compgen -W "--check --stdout --quiet" -- "$cur")) ;;
        *) COMPREPLY=($(compgen -W "--check --stdout --quiet" -- "$cur")) ;;
    esac
}
complete -F _cfmt_completions cfmt

_cflint_completions() {
    local cur
    _init_completion || return
    COMPREPLY=($(compgen -f -X '!*.cfv' -- "$cur") $(compgen -W "--strict --json --ignore --only -v" -- "$cur"))
}
complete -F _cflint_completions cflint

_cftest_completions() {
    local cur
    _init_completion || return
    COMPREPLY=($(compgen -f -X '!*.cfv' -- "$cur") $(compgen -W "--watch --json --tap -v" -- "$cur"))
}
complete -F _cftest_completions cftest
