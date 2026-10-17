cmake_minimum_required(VERSION 3.15)
project(lofi100 C ASM)
include(GenerateG3A)
find_package(Gint 2.9 REQUIRED)
add_executable(lofi100 src/main.c src/platform.c src/cg50-serial.S src/overclock.c src/wav.c src/library.c src/ui.c src/transport.c src/flashmap.c)
target_compile_options(lofi100 PRIVATE -Wall -Wextra -Werror -Os)
# Release output: dist/LoFi100.g3a (git-ignored; attached to a release). The
# OS draws the menu name over the icon, so the name stays empty to keep the
# icon's AUDIO unobstructed; the internal name is set below.
set(G3A_OUTPUT "dist/LoFi100.g3a")
set(G3A_NAME "")
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/dist)
# The stream and decoder glue, kept apart so that its code can go to RAM with
# the decoders (below).
add_library(stream STATIC src/mp3-decode.c src/mp3-live.c src/tx-feed.c src/sdm-stream.c src/opus-file.c src/nor-source.c)
target_compile_options(stream PRIVATE -Wall -Wextra -Werror -O2)
set(MAD_DIR vendor/libmad)
set(MAD_SOURCES fixed bit timer stream frame synth layer12 layer3 huffman)
foreach(f IN LISTS MAD_SOURCES)
  list(APPEND MAD_FILES ${MAD_DIR}/${f}.c)
endforeach()
add_library(mad STATIC ${MAD_FILES})
target_include_directories(mad PUBLIC ${MAD_DIR})
target_compile_definitions(mad PUBLIC FPM_64BIT HAVE_LIMITS_H HAVE_ASSERT_H MAD_WORK_XYRAM)
target_compile_options(mad PRIVATE -O2 -fwrapv -Wno-overflow)
# libopus 1.3.1, decoder subset, fixed point (vendor/opus/UPSTREAM.md)
set(OPUS_DIR vendor/opus)
set(OPUS_CELT bands celt celt_decoder cwrs entcode entdec entenc kiss_fft laplace mathops mdct modes pitch celt_lpc quant_bands rate vq)
set(OPUS_SILK CNG code_signs init_decoder decode_core decode_frame decode_parameters decode_indices decode_pulses decoder_set_fs dec_API gain_quant interpolate LP_variable_cutoff NLSF_decode PLC shell_coder tables_gain tables_LTP tables_NLSF_CB_NB_MB tables_NLSF_CB_WB tables_other tables_pitch_lag tables_pulses_per_block NLSF_unpack stereo_MS_to_LR bwexpander_32 bwexpander decode_pitch lin2log log2lin LPC_analysis_filter LPC_inv_pred_gain table_LSF_cos NLSF2A NLSF_stabilize pitch_est_tables resampler resampler_down2_3 resampler_down2 resampler_private_AR2 resampler_private_down_FIR resampler_private_IIR_FIR resampler_private_up2_HQ resampler_rom sort sum_sqr_shift stereo_decode_pred LPC_fit biquad_alt)
foreach(f IN LISTS OPUS_CELT)
  list(APPEND OPUS_FILES ${OPUS_DIR}/celt/${f}.c)
endforeach()
foreach(f IN LISTS OPUS_SILK)
  list(APPEND OPUS_FILES ${OPUS_DIR}/silk/${f}.c)
endforeach()
list(APPEND OPUS_FILES ${OPUS_DIR}/src/opus.c ${OPUS_DIR}/src/opus_decoder.c)
add_library(opus STATIC ${OPUS_FILES})
target_include_directories(opus PUBLIC ${OPUS_DIR}/include PRIVATE ${OPUS_DIR} ${OPUS_DIR}/celt ${OPUS_DIR}/silk ${OPUS_DIR}/silk/fixed)
target_compile_definitions(opus PRIVATE HAVE_CONFIG_H)
target_compile_options(opus PRIVATE -O2 -w)
target_link_libraries(stream PRIVATE mad opus Gint::Gint)
# Private RAM copies of the runtime helpers for the code below (see the
# ramrt objects further down). Renamed in both the copies and their callers:
# gint and fxlibc keep calling the ROM originals, some of them before main()
# (driver setup, interrupt vectors, on-chip reload after each world switch),
# when .data is not yet copied or not yet written back for instruction fetch.
# 1.1.5 moved the global symbols themselves and crashed at startup
# (System ERROR, ADDRESS (R), PC 00000498).
set(RAMRT_SYMS _memcpy _memmove _memset _memcmp ___udivsi3_i4i ___sdivsi3_i4i
  ___clzsi2 ___clz_tab ___udivdi3 ___divdi3 ___udiv_qrnnd_16
  ___movmem_i4_even ___movmem_i4_odd ___movmemSI12_i4
  ___movstr_i4_even ___movstr_i4_odd ___movstrSI12_i4)
set(RAMRT_REDEFINE "")
foreach(sym IN LISTS RAMRT_SYMS)
  list(APPEND RAMRT_REDEFINE --redefine-sym ${sym}=_ram${sym})
endforeach()
# Playback code and tables in RAM on both models. The add-in image is linked at
# 0x00300000: MPM (fx-CG100) copies it into SDRAM, while the fx-CG50 OS maps it
# straight from NOR, one page per TLB miss through an OS syscall that runs with
# interrupts blocked. That held off the DMA re-arm past what the SCIF FIFO
# covers (clicks in MP3) and paged CELT/SILK code in and out on every Opus
# packet, all on the CG50 only. gint's linker script places .data.* in user RAM
# (0x08100000, 512 KiB SDRAM behind wired TLB entries on both models) and copies
# it there at startup; main() then writes it back for instruction fetch
# (ram_code_sync). The DMA interrupt path (gint's VBR handlers, also in user
# RAM, and tx-feed.c) then never touches ROM. String literals stay in ROM.
foreach(lib mad opus stream)
  add_custom_command(TARGET ${lib} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY}
      --rename-section .text=.data.${lib}_text
      --rename-section .text.unlikely=.data.${lib}_text_unlikely
      --rename-section .text.startup=.data.${lib}_text_startup
      --rename-section .rodata=.data.${lib}_rodata
      ${RAMRT_REDEFINE}
      $<TARGET_FILE:${lib}>
    VERBATIM)
endforeach()
# The runtime helpers the playback code calls, also in RAM: libgcc's division,
# clz and block move (Opus calls __clzsi2 and __udivsi3_i4i per entropy symbol,
# __clz_tab sits on yet another page) and fxlibc's assembly mem*. Left in ROM,
# the CG50 kept them on a handful of OS-mapped pages that evicted each other
# (1.1.4: MP3 fine, Opus still noisy). Copied out of the toolchain's archives
# and renamed like the libraries above, symbols included (RAMRT_SYMS), so only
# the stream, mad and opus code reaches them; everything else links the ROM
# originals from -lgcc / -lc as before.
execute_process(COMMAND ${CMAKE_C_COMPILER} -m4-nofpu -mb -print-libgcc-file-name
  OUTPUT_VARIABLE LIBGCC_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${CMAKE_C_COMPILER} -m4-nofpu -mb -print-file-name=libc.a
  OUTPUT_VARIABLE LIBC_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
set(RAMRT_GCC _div_table.o _clzsi2.o _clz.o _udivdi3.o _divdi3.o _udiv_qrnnd_16.o _movmem_i4.o)
set(RAMRT_LIBC memcpy.S.obj memmove.S.obj memset.S.obj memcmp.S.obj)
set(RAMRT_DIR ${CMAKE_CURRENT_BINARY_DIR}/ramrt)
file(MAKE_DIRECTORY ${RAMRT_DIR})
# Linked as objects of the executable, not as an archive (the toolchain puts a
# -lgcc right after the executable's own objects, ahead of every library).
set(RAMRT_OBJS "")
foreach(m IN LISTS RAMRT_GCC RAMRT_LIBC)
  if(m IN_LIST RAMRT_GCC)
    set(src ${LIBGCC_PATH})
  else()
    set(src ${LIBC_PATH})
  endif()
  set(obj ${RAMRT_DIR}/ram-${m}.o)
  add_custom_command(OUTPUT ${obj}
    COMMAND ${CMAKE_AR} x ${src} ${m}
    COMMAND ${CMAKE_OBJCOPY}
      --rename-section .text=.data.ramrt_text
      --rename-section .rodata=.data.ramrt_rodata
      ${RAMRT_REDEFINE}
      ${m} ${obj}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${m}
    WORKING_DIRECTORY ${RAMRT_DIR}
    DEPENDS ${src}
    VERBATIM)
  list(APPEND RAMRT_OBJS ${obj})
endforeach()
set_source_files_properties(${RAMRT_OBJS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
target_sources(lofi100 PRIVATE ${RAMRT_OBJS})
target_link_libraries(lofi100 stream mad opus Gint::Gint)
generate_g3a(TARGET lofi100 OUTPUT "${G3A_OUTPUT}"
  NAME "${G3A_NAME}" VERSION "01.01.1100"
  ICONS assets/icons/lofi100.png assets/icons/lofi100-selected.png)
if(G3A_NAME STREQUAL "")
  # fxgxa derives the internal identifier from NAME; preserve it when hiding the label.
  add_custom_command(TARGET lofi100 POST_BUILD
    COMMAND fxgxa --edit "${CMAKE_CURRENT_SOURCE_DIR}/${G3A_OUTPUT}" "--internal=@LOFI"
    COMMAND fxgxa --repair "${CMAKE_CURRENT_SOURCE_DIR}/${G3A_OUTPUT}"
    VERBATIM)
endif()
