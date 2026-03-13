_rpi_gpu_usage() {
    local cur prev words cword
    _init_completion || return
    COMPREPLY=($(compgen -W "--csv --help" -- "$cur"))
}

complete -F _rpi_gpu_usage rpi-gpu-usage
