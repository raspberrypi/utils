_vcgencmd() {
    local cur prev words cword split
    _init_completion -s || return
    if ! ((cword == 1)); then
        return
    fi
    local cmds=$(vcgencmd commands | sed 's/^.*=//;s/[",]//g')
    COMPREPLY+=($(compgen -W "$cmds" -- $cur))
}

complete -F _vcgencmd vcgencmd
