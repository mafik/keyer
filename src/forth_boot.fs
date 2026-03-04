needs ../lib/ueforth/common/phase1.fs
needs ../lib/ueforth/esp32/allocation.fs
needs ../lib/ueforth/common/phase2.fs

( Minimal platform I/O setup )
: nop-type ( a n -- ) 2drop ;
: nop-key ( -- n ) 0 ;
: nop-key? ( -- f ) 0 ;
' nop-type is type
' nop-key is key
' nop-key? is key?
' raw-terminate is terminate

( Setup vocabulary )
internals definitions
transfer internals-builtins
forth definitions internals
transfer forth
setup-saving-base
forth
