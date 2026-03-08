idf_component_register(SRCS
    "ble_keyboard.cpp"
    "eye_term.cpp"
    "common_esp32.cpp"
    "main_loop.cpp"
    "secrets.cpp"
    "ssh.cpp"
    "keyer.cpp"
    "keyboard.cpp"
    "tcl.cpp"
    "shadow_editor.cpp"
    "typist.cpp"
    "../lib/jimtcl/src/jim.c"
    "../lib/jimtcl/src/jimregexp.c"
    "../lib/jimtcl/src/jimiocompat.c"
    "../lib/jimtcl/src/jim-subcmd.c"
    "../lib/jimtcl/src/jim-format.c"
    "../lib/jimtcl/src/jim-array.c"
    "../lib/jimtcl/src/jim-clock.c"
    "../lib/jimtcl/src/jim-pack.c"
    "../lib/jimtcl/src/jim-nosignal.c"
    "../lib/jimtcl/src/utf8.c"
    INCLUDE_DIRS "../lib/jimtcl/src"
)

# Suppress warnings in jimtcl sources (upstream code)
set(JIMTCL_SRCS
    ../lib/jimtcl/src/jim.c
    ../lib/jimtcl/src/jimregexp.c
    ../lib/jimtcl/src/jimiocompat.c
    ../lib/jimtcl/src/jim-subcmd.c
    ../lib/jimtcl/src/jim-format.c
    ../lib/jimtcl/src/jim-array.c
    ../lib/jimtcl/src/jim-clock.c
    ../lib/jimtcl/src/jim-pack.c
    ../lib/jimtcl/src/jim-nosignal.c
    ../lib/jimtcl/src/utf8.c
)
set_source_files_properties(${JIMTCL_SRCS} PROPERTIES
    COMPILE_FLAGS "-Wno-missing-field-initializers -Wno-implicit-fallthrough -Wno-unused-variable"
)
