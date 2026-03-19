SECTIONS {
  .text : { *(.text) }
  .text.hot : { *(.text.hot) }
  .text.split : { *(.text.split) }
  .text_large1 : { *(.text_large1) }
  .text.unlikely : { *(.text.unlikely) }
  .text_large2 : { *(.text_large2) }
}

INSERT AFTER .data
