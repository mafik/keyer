const char forth_boot[] = R"""(
: (   bl nl + 1- parse drop drop ; immediate ( Now can do comments! )
( bl=32 nl=10 so nl+32-1=41, right paren )
: \   nl parse drop drop ; immediate
: #!   nl parse drop drop ; immediate  ( shebang for scripts )
( Stack Baseline )
sp@ constant sp0
rp@ constant rp0
fp@ constant fp0

( Quoting Words )
: ' bl parse 2dup find dup >r -rot r> 0= 'notfound @ execute 2drop ;
: ['] ' aliteral ; immediate
: char bl parse drop c@ ;
: [char] char aliteral ; immediate

( Core Control Flow )
create BEGIN ' nop @ ' begin !        : begin   ['] begin , here ; immediate
create AGAIN ' branch @ ' again !     : again   ['] again , , ; immediate
create UNTIL ' 0branch @ ' until !    : until   ['] until , , ; immediate
create AHEAD ' branch @ ' ahead !     : ahead   ['] ahead , here 0 , ; immediate
create THEN ' nop @ ' then !          : then   ['] then , here swap ! ; immediate
create IF ' 0branch @ ' if !          : if   ['] if , here 0 , ; immediate
create ELSE ' branch @ ' else !       : else   ['] else , here 0 , swap here swap ! ; immediate
create WHILE ' 0branch @ ' while !    : while   ['] while , here 0 , swap ; immediate
create REPEAT ' branch @ ' repeat !   : repeat   ['] repeat , , here swap ! ; immediate
create AFT ' branch @ ' aft !         : aft   drop ['] aft , here 0 , here swap ; immediate

( Recursion )
: recurse   current @ @ aliteral ['] execute , ; immediate

( Tools to build postpone later out of recognizers )
: immediate? ( xt -- f ) >flags 1 and 0= 0= ;
: postpone, ( xt -- ) aliteral ['] , , ;

( Rstack nest depth )
variable nest-depth

( FOR..NEXT )
create FOR ' >r @ ' for !         : for   1 nest-depth +! ['] for , here ; immediate
create NEXT ' donext @ ' next !   : next   -1 nest-depth +! ['] next , , ; immediate

( Define a data type for Recognizers. )
: RECTYPE: ( xt-interpret xt-compile xt-postpone "name" -- ) CREATE , , , ;
: do-notfound ( a n -- ) -1 'notfound @ execute ;
' do-notfound ' do-notfound ' do-notfound  RECTYPE: RECTYPE-NONE
' execute     ' ,           ' postpone,    RECTYPE: RECTYPE-WORD
' execute     ' execute     ' ,            RECTYPE: RECTYPE-IMM
' drop        ' execute     ' execute      RECTYPE: RECTYPE-NUM

: RECOGNIZE ( c-addr len addr1 -- i*x addr2 )
  dup @ for aft
    cell+ 3dup >r >r >r @ execute
    dup RECTYPE-NONE <> if rdrop rdrop rdrop rdrop exit then
    drop r> r> r>
  then next
  drop RECTYPE-NONE
;

( Define a recognizer stack. )
create RECSTACK 0 , bl 2/ ( 16 no numbers yet ) cells allot
: +RECOGNIZER ( xt -- ) 1 RECSTACK +! RECSTACK dup @ cells + ! ;
: -RECOGNIZER ( -- ) -1 RECSTACK +! ;
: GET-RECOGNIZERS ( -- xtn..xt1 n )
   RECSTACK @ for RECSTACK r@ cells + @ next ;
: SET-RECOGNIZERS ( xtn..xt1 n -- )
   0 RECSTACK ! for aft +RECOGNIZER then next ;

( Create recognizer based words. )
: postpone ( "name" -- ) bl parse RECSTACK RECOGNIZE @ execute ; immediate
: +evaluate1
  bl parse dup 0= if 2drop exit then
  RECSTACK RECOGNIZE state @ 1+ 1+ cells + @ execute
;

( Setup recognizing words. )
: REC-FIND ( c-addr len -- xt addr1 | addr2 )
  find dup if
    dup immediate? if RECTYPE-IMM else RECTYPE-WORD then
  else
    drop RECTYPE-NONE
  then
;
' REC-FIND +RECOGNIZER

( Setup recognizing integers. )
: REC-NUM ( c-addr len -- n addr1 | addr2 )
  s>number? if
    ['] aliteral RECTYPE-NUM
  else
    RECTYPE-NONE
  then
;
' REC-NUM +RECOGNIZER

: interpret0 begin +evaluate1 again ; interpret0

( Useful stack/heap words )
: depth ( -- n ) sp@ sp0 - cell/ ;
: fdepth ( -- n ) fp@ fp0 - 4 / ;
: remaining ( -- n ) 'heap-start @ 'heap-size @ + 'heap @ - ;
: used ( -- n ) 'heap @ sp@ 'stack-cells @ cells + - 28 + ;

( DO..LOOP )
variable leaving
: leaving,   here leaving @ , leaving ! ;
: leaving(   leaving @ 0 leaving !   2 nest-depth +! ;
: )leaving   leaving @ swap leaving !  -2 nest-depth +!
             begin dup while dup @ swap here swap ! repeat drop ;
: DO ( n n -- .. ) swap r> -rot >r >r >r ;
: do ( lim s -- ) leaving( postpone DO here ; immediate
: ?DO ( n n -- n n f .. )
   2dup = if 2drop r> @ >r else swap r> cell+ -rot >r >r >r then ;
: ?do ( lim s -- ) leaving( postpone ?DO leaving, here ; immediate
: UNLOOP   r> rdrop rdrop >r ;
: LEAVE   r> rdrop rdrop @ >r ;
: leave   postpone LEAVE leaving, ; immediate
: +LOOP ( n -- ) r> r> dup r@ - >r rot + r> -rot
                       dup r@ - -rot >r >r xor 0<
                 if r> cell+ rdrop rdrop >r else r> @ >r then ;
: +loop ( n -- ) postpone +LOOP , )leaving ; immediate
: LOOP   r> r> dup r@ - >r 1+ r> -rot
               dup r@ - -rot >r >r xor 0<
         if r> cell+ rdrop rdrop >r else r> @ >r then ;
: loop   postpone LOOP , )leaving ; immediate
create I ' r@ @ ' i !  ( i is same as r@ )
: J ( -- n ) rp@ 3 cells - @ ;
: K ( -- n ) rp@ 5 cells - @ ;

( Exceptions )
variable handler
handler 'throw-handler !
: catch ( xt -- n )
  fp@ >r sp@ >r handler @ >r rp@ handler ! execute
  r> handler ! rdrop rdrop 0 ;
: throw ( n -- )
  dup if handler @ rp! r> handler !
         r> swap >r sp! drop r> r> fp! else drop then ;
' throw 'notfound !

( Values )
: value ( n -- ) constant ;
: value-bind ( xt-val xt )
   >r >body state @ if
     r@ ['] ! = if rdrop ['] doset , , else aliteral r> , then
   else r> execute then ;
: to ( n -- ) ' ['] ! value-bind ; immediate
: +to ( n -- ) ' ['] +! value-bind ; immediate

( Deferred Words )
: defer ( "name" -- ) create 0 , does> @ dup 0= throw execute ;
: is ( xt "name -- ) postpone to ; immediate
( Defer I/O to platform specific )
defer type
defer key
defer key?
defer terminate

( Hooks for prompt )
defer prompt

( Core I/O )
: bye   0 terminate ;
: emit ( n -- ) >r rp@ 1 type rdrop ;
: space bl emit ;   : cr 13 emit nl emit ;

( Numeric Output )
variable hld
: pad ( -- a ) here 80 + ;
: digit ( u -- c ) 9 over < 7 and + 48 + ;
: extract ( n base -- n c ) u/mod swap digit ;
: <# ( -- ) pad hld ! ;
: hold ( c -- ) hld @ 1 - dup hld ! c! ;
: # ( u -- u ) base @ extract hold ;
: #s ( u -- 0 ) begin # dup while repeat ;
: sign ( n -- ) 0< if 45 hold then ;
: #> ( w -- b u ) drop hld @ pad over - ;
: str ( n -- b u ) dup >r abs <# #s r> sign #> ;
: hex ( -- ) 16 base ! ;   : octal ( -- ) 8 base ! ;
: decimal ( -- ) 10 base ! ;   : binary ( -- ) 2 base ! ;
: u. ( u -- ) <# #s #> type space ;
: . ( w -- ) base @ 10 xor if u. exit then str type space ;
: ? ( a -- ) @ . ;
: n. ( n -- ) base @ swap decimal <# #s #> type base ! ;

( Strings )
: parse-quote ( -- a n ) [char] " parse ;
: $place ( a n -- ) for aft dup c@ c, 1+ then next drop ;
: zplace ( a n -- ) $place 0 c, align ;
: $@   r@ dup cell+ swap @ r> dup @ 1+ aligned + cell+ >r ;
: s"   parse-quote state @ if postpone $@ dup , zplace
       else dup here swap >r >r zplace r> r> then ; immediate
: ."   postpone s" state @ if postpone type else type then ; immediate
: z"   postpone s" state @ if postpone drop else drop then ; immediate
: r"   parse-quote state @ if swap aliteral aliteral then ; immediate
: r|   [char] | parse state @ if swap aliteral aliteral then ; immediate
: r~   [char] ~ parse state @ if swap aliteral aliteral then ; immediate
: s>z ( a n -- z ) here >r zplace r> ;
: z>s ( z -- a n ) 0 over begin dup c@ while 1+ swap 1+ swap repeat drop ;

( Better Errors )
: notfound ( a n n -- )
   dup if cr ." ERROR: " >r type r> ."  NOT FOUND!" cr throw else drop then ;
' notfound 'notfound !

( Abort )
: abort   -1 throw ;
: abort"   postpone ." postpone cr -2 aliteral postpone throw ; immediate

( Input )
: raw.s   depth 0 max for aft sp@ r@ cells - @ . then next ;
variable echo -1 echo !   variable arrow -1 arrow !  0 value wascr
: *emit ( n -- ) dup 13 = if drop cr else emit then ;
: ?echo ( n -- ) echo @ if *emit else drop then ;
: ?arrow.   arrow @ if ." --> " then ;
: *key ( -- n )
  begin
    key
    dup nl = if
      drop wascr if 0 else 13 exit then
    then
    dup 13 = to wascr
    dup if exit else drop then
  again ;
: eat-till-cr   begin *key dup 13 = if ?echo exit else drop then again ;
: accept ( a n -- n ) ?arrow. 0 swap begin 2dup < while
     *key
     dup 13 = if ?echo drop nip exit then
     dup 8 = over 127 = or if
       drop over if rot 1- rot 1- rot 8 ?echo bl ?echo 8 ?echo then
     else
       dup ?echo
       >r rot r> over c! 1+ -rot swap 1+ swap
     then
   repeat drop nip
   eat-till-cr
;
200 constant input-limit
: tib ( -- a ) 'tib @ ;
create input-buffer   input-limit allot
: tib-setup   input-buffer 'tib ! ;
: refill   tib-setup tib input-limit accept #tib ! 0 >in ! -1 ;

( Stack Guards )
sp0 'stack-cells @ 2 3 */ cells + constant sp-limit
: ?stack   sp@ sp0 < if ." STACK UNDERFLOW " -4 throw then
           sp-limit sp@ < if ." STACK OVERFLOW " -3 throw then ;

( REPL )
: default-prompt   ."  ok" cr raw.s ;  ' default-prompt is prompt
: evaluate-buffer   begin >in @ #tib @ < while ?stack +evaluate1 repeat ?stack ;
: evaluate ( a n -- ) 'tib @ >r #tib @ >r >in @ >r
                      #tib ! 'tib ! 0 >in ! evaluate-buffer
                      r> >in ! r> #tib ! r> 'tib ! ;
: evaluate&fill
   begin >in @ #tib @ >= if prompt refill drop then evaluate-buffer again ;
: quit
   #tib @ >in !
   begin ['] evaluate&fill catch
         if 0 state ! sp0 sp! fp0 fp! rp0 rp! ."  ERROR " cr then
   again ;
variable boot-prompt
: free. ( nf nu -- ) 2dup swap . ." free + " . ." used = " 2dup + . ." total ("
                     over + 100 -rot */ n. ." % free)" ;
: raw-ok   ."  v{{VERSION}} - rev {{REVISION}}" cr
           boot-prompt @ if boot-prompt @ execute then
           ." Forth dictionary: " remaining used free. cr
           ." 3 x Forth stacks: " 'stack-cells @ cells . ." bytes each" cr
           quit ;
( Interpret time conditionals )

: DEFINED? ( "name" -- xt|0 )
   begin bl parse dup 0= while 2drop refill 0= throw repeat
   find state @ if aliteral then ; immediate
defer [SKIP]
: [THEN] ; immediate
: [ELSE] [SKIP] ; immediate
: [IF] 0= if [SKIP] then ; immediate
: [SKIP]' 0 begin postpone defined? dup if
    dup ['] [IF] = if swap 1+ swap then
    dup ['] [ELSE] = if swap dup 0 <= if 2drop exit then swap then
    dup ['] [THEN] = if swap 1- dup 0< if 2drop exit then swap then
  then drop again ;
' [SKIP]' is [SKIP]
( Implement Vocabularies )
( normal: link, flags&len, code )
( vocab:  link, flags&len, code | link , len=0, voclink )
variable last-vocabulary
: vocabulary ( "name" )
  create current @ 2 cells + , 0 , last-vocabulary @ ,
  current @ @ last-vocabulary !
  does> context ! ;
: definitions   context @ current ! ;
vocabulary FORTH
' forth >body @ >link ' forth >body !
forth definitions

( Make it easy to transfer words between vocabularies )
: xt-find& ( xt -- xt& ) context @ begin 2dup @ <> while @ >link& repeat nip ;
: xt-hide ( xt -- ) xt-find& dup @ >link swap ! ;
8 constant BUILTIN_MARK
: xt-transfer ( xt --  ) dup >flags BUILTIN_MARK and if drop exit then
  dup xt-hide   current @ @ over >link& !   current @ ! ;
: transfer ( "name" ) ' xt-transfer ;
: }transfer ;
: transfer{ begin ' dup ['] }transfer = if drop exit then xt-transfer again ;

( Watered down versions of these )
: only   forth 0 context cell+ ! ;
: voc-stack-end ( -- a ) context begin dup @ while cell+ repeat ;
: also   context context cell+ voc-stack-end over - 2 cells + cmove> ;
: previous
  voc-stack-end context cell+ = throw
  context cell+ context voc-stack-end over - cell+ cmove ;
: sealed   0 last-vocabulary @ >body ! ;

( Hide some words in an internals vocabulary )
vocabulary internals   internals definitions

( Vocabulary chain for current scope, place at the -1 position )
variable scope   scope context cell - !

transfer{
  xt-find& xt-hide xt-transfer
  voc-stack-end last-vocabulary notfound
  *key *emit wascr eat-till-cr default-prompt
  immediate? input-buffer ?echo ?arrow. arrow
  evaluate-buffer evaluate&fill aliteral value-bind
  leaving( )leaving leaving leaving,
  parse-quote digit $@ raw.s
  tib-setup input-limit sp-limit ?stack
  [SKIP] [SKIP]' raw-ok boot-prompt free.
  $place zplace BUILTIN_MARK
  nest-depth handler +evaluate1 do-notfound interpret0
}transfer

( Move branching opcodes to separate vocabulary. )
vocabulary internalized  internalized definitions
: cleave   ' >link xt-transfer ;
cleave begin   cleave again   cleave until
cleave ahead   cleave then    cleave if
cleave else    cleave while   cleave repeat
cleave aft     cleave for     cleave next
cleave do      cleave ?do     cleave +loop
cleave loop    cleave leave
forth definitions

( Move recognizers to separate vocabulary )
vocabulary recognizers  recognizers definitions
transfer{
  REC-FIND REC-NUM
  RECTYPE: RECTYPE-NONE RECTYPE-WORD RECTYPE-IMM RECTYPE-NUM
  SET-RECOGNIZERS GET-RECOGNIZERS
  -RECOGNIZER +RECOGNIZER RECSTACK
  RECOGNIZE
}transfer
forth definitions

( Make DOES> switch to compile mode when interpreted )
(
forth definitions internals
' does>
: does>   state @ if postpone does> exit then
          ['] constant @ current @ @ dup >r !
          here r> cell+ ! postpone ] ; immediate
xt-hide
forth definitions
)
: sf, ( r -- ) here sf! sfloat allot ;

: afliteral ( r -- ) ['] DOFLIT , sf, align ;
: fliteral   afliteral ; immediate

: fconstant ( r "name" ) create sf, align does> sf@ ;
: fvariable ( "name" ) create sfloat allot align ;

6 value precision
: set-precision ( n -- ) to precision ;

( Add recognizer for floats. )
also recognizers definitions
: REC-FNUM ( c-addr len -- f addr1 | addr2 )
  s>float? if
    ['] afliteral RECTYPE-NUM
  else
    RECTYPE-NONE
  then
;
' REC-FNUM +RECOGNIZER
previous definitions

internals definitions
: #f+s ( r -- ) fdup precision for aft 10e f* then next
                precision for aft fdup f>s 10 mod [char] 0 + hold 0.1e f* then next
                [char] . hold fdrop f>s #s ;
forth definitions internals
: #fs ( r -- ) fdup f0< if fnegate #f+s [char] - hold else #f+s then ;
: f. ( r -- ) <# #fs #> type space ;
internals definitions
: rawf.s   fdepth 0 max for aft fp@ r@ sfloats - sf@ f. then next ;
: default-fprompt default-prompt fdepth if ."  F: " rawf.s then ;
' default-fprompt is prompt
forth definitions internals
: f.s   ." <" fdepth n. ." > " rawf.s ;
forth definitions
( Vocabulary for building C-style structures )

vocabulary structures   structures definitions

variable last-align
variable last-typer
: typer ( xt@ xt! sz "name" -- )
   create dup , 1 max cell min , , ,
   does> dup last-typer !  dup cell+ @ last-align !  @ ;
: sc@ ( a -- c ) c@ dup 127 > if 256 - then ;
' sc@ ' c! 1 typer i8
' c@  ' c! 1 typer u8
' sw@ ' w! 2 typer i16
' uw@ ' w! 2 typer u16
' sl@ ' l! 4 typer i32
' ul@ ' l! 4 typer u32
' @   ' !  8 typer i64  ( Wrong on 32-bit! )
' @   ' !  cell typer ptr
long-size cell = [IF]
  : long   ptr ;
[ELSE]
  : long   i32 ;
[THEN]

variable last-struct
: struct ( "name" ) 0 0 0 typer latestxt >body last-struct !
                    1 last-align ! ;
: align-by ( a n -- a ) 1- dup >r + r> invert and ;
: max! ( n a -- ) swap over @ max swap ! ;
: struct-align ( n -- )
  dup last-struct @ cell+ max!
  last-struct @ @ swap align-by last-struct @ ! ;
: field ( n "name" )
  last-align @ struct-align
  create last-struct @ @ ,   last-struct @ +!  last-typer @ ,
  does> @ + ;

: field-op ( n "name" -- )
  >r ' dup >body cell+ @ r> cells + @
  state @ if >r , r> , else >r execute r> execute then ;
: !field ( n "name" -- ) 2 field-op ; immediate
: @field ( "name" -- n ) 3 field-op ; immediate

forth definitions
( Words with OS assist )
: allocate ( n -- a ior ) malloc dup 0= ;
: free ( a -- ior ) sysfree 0 ;
: resize ( a n -- a ior ) realloc dup 0= ;
( Words built after boot )

( For tests and asserts )
: assert ( f -- ) 0= throw ;

( Print spaces )
: spaces ( n -- ) 0 max for aft space then next ;

internals definitions

( Temporary for platforms without CALLCODE )
DEFINED? CALLCODE 0= [IF]
  create CALLCODE
[THEN]

( Safe memory access, i.e. aligned )
cell 1- constant cell-mask
: cell-base ( a -- a ) cell-mask invert and ;
: cell-shift ( a -- a ) cell-mask and 8 * ;
: ca@ ( a -- n ) dup cell-base @ swap cell-shift rshift 255 and ;

( Print address line leaving room )
: dump-line ( a -- a ) cr <# #s #> 20 over - >r type r> spaces ;

( Semi-dangerous word to trim down the system heap )
DEFINED? realloc [IF]
: relinquish ( n -- ) negate 'heap-size +! 'heap-start @ 'heap-size @ realloc drop ;
[THEN]

forth definitions internals

( Examine Memory )
: dump ( a n -- )
   over 15 and if over dump-line over 15 and 3 * spaces then
   for aft
     dup 15 and 0= if dup dump-line then
     dup ca@ <# # #s #> type space 1+
   then next drop cr ;

( Remove from Dictionary )
: forget ( "name" ) ' dup >link current @ !  >name drop here - allot ;

internals definitions
1 constant IMMEDIATE_MARK
2 constant SMUDGE
4 constant BUILTIN_FORK
16 constant NONAMED
32 constant +TAB
64 constant -TAB
128 constant ARGS_MARK
: mem= ( a a n -- f)
   for aft 2dup c@ swap c@ <> if 2drop rdrop 0 exit then 1+ swap 1+ then next 2drop -1 ;
forth definitions also internals
: :noname ( -- xt ) 0 , current @ @ , NONAMED SMUDGE or ,
                    here dup current @ ! ['] mem= @ , postpone ] ;
: str= ( a n a n -- f) >r swap r@ <> if rdrop 2drop 0 exit then r> mem= ;
: startswith? ( a n a n -- f ) >r swap r@ < if rdrop 2drop 0 exit then r> mem= ;
: .s   ." <" depth n. ." > " raw.s cr ;
only forth definitions

( Tweak indent on branches )
internals internalized definitions

: flags'or! ( n -- ) ' >flags& dup >r c@ or r> c! ;
+TAB flags'or! BEGIN
-TAB flags'or! AGAIN
-TAB flags'or! UNTIL
+TAB flags'or! AHEAD
-TAB flags'or! THEN
+TAB flags'or! IF
+TAB -TAB or flags'or! ELSE
+TAB -TAB or flags'or! WHILE
-TAB flags'or! REPEAT
+TAB flags'or! AFT
+TAB flags'or! FOR
-TAB flags'or! NEXT
+TAB flags'or! DO
ARGS_MARK +TAB or flags'or! ?DO
ARGS_MARK -TAB or flags'or! +LOOP
ARGS_MARK -TAB or flags'or! LOOP
ARGS_MARK flags'or! LEAVE

forth definitions 

( Definitions building to SEE and ORDER )
internals definitions
variable indent
: see. ( xt -- ) >name type space ;
: icr   cr indent @ 0 max 4* spaces ;
: indent+! ( n -- ) indent +! icr ;
: see-one ( xt -- xt+1 )
   dup cell+ swap @
   dup ['] DOLIT = if drop dup @ . cell+ exit then
   dup ['] DOSET = if drop ." TO " dup @ cell - see. cell+ icr exit then
   dup ['] DOFLIT = if drop dup sf@ <# [char] e hold #fs #> type space cell+ exit then
   dup ['] $@ = if drop ['] s" see.
                   dup @ dup >r >r dup cell+ r> type cell+ r> 1+ aligned +
                   [char] " emit space exit then
   dup ['] DOES> = if icr then
   dup >flags -TAB AND if -1 indent+! then
   dup see.
   dup >flags +TAB AND if
     1 indent+!
   else
     dup >flags -TAB AND if icr then
   then
   dup ['] ! = if icr then
   dup ['] +! = if icr then
   dup  @ ['] BRANCH @ =
   over @ ['] 0BRANCH @ = or
   over @ ['] DONEXT @ = or
   over >flags ARGS_MARK and or
       if swap cell+ swap then
   drop
;
: see-loop   dup >body swap >params 1- cells over +
             begin 2dup < while swap see-one swap repeat 2drop ;
: ?see-flags   >flags IMMEDIATE_MARK and if ." IMMEDIATE " then ;
: see-xt ( xt -- )
  dup @ ['] see-loop @ = if
    ['] : see.  dup see.
    1 indent ! icr
    dup see-loop
    -1 indent+! ['] ; see.
    ?see-flags cr
    exit
  then
  dup >flags BUILTIN_FORK and if ." Built-in-fork: " see. exit then
  dup @ ['] input-buffer @ = if ." CREATE/VARIABLE: " see. cr exit then
  dup @ ['] SMUDGE @ = if ." DOES>/CONSTANT: " see. cr exit then
  dup @ ['] callcode @ = if ." Code: " see. cr exit then
  dup >params 0= if ." Built-in: " see. cr exit then
  ." Unsupported: " see. cr ;

: nonvoc? ( xt -- f )
  dup 0= if exit then dup >name nip swap >flags NONAMED BUILTIN_FORK or and or ;
: see-vocabulary ( voc )
  @ begin dup nonvoc? while dup see-xt >link repeat drop cr ;
: >vocnext ( xt -- xt ) >body 2 cells + @ ;
: see-all
  last-vocabulary @ begin dup while
    ." VOCABULARY " dup see. cr ." ------------------------" cr
    dup >body see-vocabulary
    >vocnext
  repeat drop cr ;
: voclist-from ( voc -- ) begin dup while dup see. cr >vocnext repeat drop ;
: voclist   last-vocabulary @ voclist-from ;
: voc. ( voc -- ) 2 cells - see. ;
: vocs. ( voc -- ) dup voc. @ begin dup while
    dup nonvoc? 0= if ." >> " dup 2 cells - voc. then
    >link
  repeat drop cr ;

( Words to measure size of things )
: size-vocabulary ( voc )
  @ begin dup nonvoc? while
    dup >params . dup >size . dup . dup see. cr >link
  repeat drop ;
: size-all
  last-vocabulary @ begin dup while
    0 . 0 . 0 . dup see. cr
    dup >body size-vocabulary
    >vocnext
  repeat drop cr ;

forth definitions also internals
: see   ' see-xt ;
: order   context begin dup @ while dup @ vocs. cell+ repeat drop ;
only forth definitions

( List words in Dictionary / Vocabulary )
internals definitions
70 value line-width
0 value line-pos
: onlines ( xt -- xt )
   line-pos line-width > if cr 0 to line-pos then
   dup >name nip 1+ line-pos + to line-pos ;
: vins. ( voc -- )
  >r 'builtins begin dup >link while
    dup >params r@ = if dup onlines see. then
    3 cells +
  repeat drop rdrop ;
: ins. ( n xt -- n ) cell+ @ vins. ;
: ?ins. ( xt -- xt ) dup >flags BUILTIN_FORK and if dup ins. then ;
forth definitions also internals
: vlist   0 to line-pos context @ @
          begin dup nonvoc? while ?ins. dup onlines see. >link repeat drop cr ;
: words   0 to line-pos context @ @
          begin dup while ?ins. dup onlines see. >link repeat drop cr ;
only forth definitions
( Lazy loaded code words )
: asm r|

also forth definitions
vocabulary asm
internals definitions

: ca! ( n a -- ) dup cell-base >r cell-shift swap over lshift
                 swap 255 swap lshift invert r@ @ and or r> ! ;

also asm definitions

variable code-start
variable code-at

DEFINED? posix [IF]
also posix
: reserve ( n -- )
  0 swap PROT_READ PROT_WRITE PROT_EXEC or or
  MAP_ANONYMOUS MAP_PRIVATE or -1 0 mmap code-start ! ;
previous
4096 reserve
[THEN]

DEFINED? esp [IF]
also esp
: reserve ( n -- ) MALLOC_CAP_EXEC heap_caps_malloc code-start ! ;
previous
1024 reserve
[THEN]

code-start @ code-at !

: chere ( -- a ) code-at @ ;
: callot ( n -- ) code-at +! ;
: code1, ( n -- ) chere ca! 1 callot ;
: code2, ( n -- ) dup code1, 8 rshift code1, ;
: code3, ( n -- ) dup code2, 16 rshift code1, ;
: code4, ( n -- ) dup code2, 16 rshift code2, ;
cell 8 = [IF]
: code,  dup code4, 32 rshift code4, ;
[ELSE]
: code,  code4, ;
[THEN]
: end-code   previous ;

also forth definitions

: code ( "name" ) create ['] callcode @ latestxt !
                  code-at @ latestxt cell+ ! also asm ;

previous previous previous
asm

| evaluate ;
( Local Variables )

( NOTE: These are not yet gforth compatible )

internals definitions

( Leave a region for locals definitions )
1024 constant locals-capacity  128 constant locals-gap
create locals-area locals-capacity allot
variable locals-here  locals-area locals-here !
: <>locals   locals-here @ here locals-here ! here - allot ;

: local@ ( n -- ) rp@ + @ ;
: local! ( n -- ) rp@ + ! ;
: local+! ( n -- ) rp@ + +! ;

variable scope-depth
variable local-op   ' local@ local-op !
: scope-exit   scope-depth @ for aft postpone rdrop then next ;
: scope-clear
   scope-exit
   scope-depth @ negate nest-depth +!
   0 scope-depth !   0 scope !   locals-area locals-here ! ;
: do-local ( n -- ) nest-depth @ + cells negate aliteral
                    local-op @ ,  ['] local@ local-op ! ;
: scope-create ( a n -- )
   dup >r $place align ( name )
   scope @ , r> 8 lshift 1 or , ( IMMEDIATE ) here scope ! ( link, flags&length )
   ['] scope-clear @ ( docol) ,
   nest-depth @ negate aliteral postpone do-local ['] exit ,
   1 scope-depth +!  1 nest-depth +!
;

: ?room   locals-here @ locals-area - locals-capacity locals-gap - >
          if scope-clear -1 throw then ;

: }? ( a n -- ) 1 <> if drop 0 exit then c@ [char] } = ;
: --? ( a n -- ) s" --" str= ;
: (to) ( xt -- ) ['] local! local-op ! execute ;
: (+to) ( xt -- ) ['] local+! local-op ! execute ;

also forth definitions

: (local) ( a n -- )
   dup 0= if 2drop exit then 
   ?room <>locals scope-create <>locals postpone >r ;
: {   bl parse
      dup 0= if scope-clear -1 throw then
      2dup --? if 2drop [char] } parse 2drop exit then
      2dup }? if 2drop exit then
      recurse (local) ; immediate
( TODO: Hide the words overriden here. )
: ;   scope-clear postpone ; ; immediate
: exit   scope-exit postpone exit ; immediate
: to ( n -- ) ' dup >flags if (to) else ['] ! value-bind then ; immediate
: +to ( n -- ) ' dup >flags if (+to) else ['] +! value-bind then ; immediate

only forth definitions
internals definitions
variable cases
forth definitions internals

: CASE ( n -- ) cases @  0 cases ! ; immediate
: ENDCASE   postpone drop cases @ for aft postpone then then next
            cases ! ; immediate
: OF ( n -- ) postpone over postpone = postpone if postpone drop ; immediate
: ENDOF   1 cases +! postpone else ; immediate

forth definitions

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
)""";
