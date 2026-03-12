proc macro {name body} {
    proc $name {} "subst [list $body]"
    return $name
}
