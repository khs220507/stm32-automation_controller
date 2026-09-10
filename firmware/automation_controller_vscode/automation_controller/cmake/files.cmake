# Add sources to executable/library
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/app_state.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/board_io.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/hcsr04.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/i2c1.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/mpu6050.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/main.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/protocol.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/spi2.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/syscalls.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/sysmem.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/system_stm32f4xx.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/timebase.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/uart2.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/uart_diag.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/w5500.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/tcp_link.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Startup/startup_stm32f401retx.s"
)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/STM32F401RETX_FLASH.ld" "${CMAKE_CURRENT_BINARY_DIR}" COPYONLY)

set_target_properties(${PROJECT_NAME} PROPERTIES LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/STM32F401RETX_FLASH.ld")
