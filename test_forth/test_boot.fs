needs ../lib/ueforth/common/phase1.fs
needs ../lib/ueforth/esp32/allocation.fs
needs ../lib/ueforth/common/phase2.fs

( Minimal platform I/O setup )
: nop-key ( -- n ) 0 ;
: nop-key? ( -- f ) 0 ;
' capture-type is type
' nop-key is key
' nop-key? is key?
' raw-terminate is terminate

( Setup vocabulary — no filetools in desktop test )
internals definitions
transfer internals-builtins
forth definitions internals
transfer forth
forth

( Override notfound to print error instead of throwing/crashing )
also internals
: safe-notfound ( a n n -- )
  dup if cr ." ERROR: " >r type r> ."  NOT FOUND!" cr drop
  else drop then ;
' safe-notfound 'notfound !
forth

forth-yield
