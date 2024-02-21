include_directories(../lib)
set(SOURCES
wpf_handler.h
wpf_handler.c
main.c)
add_executable(wiimote_file_manager ${SOURCES})
target_link_libraries(wiimote_file_manager wiiuse)

if(INSTALL_MANAGER)
    install(TARGETS wiimote_file_manager
        RUNTIME DESTINATION bin COMPONENT manager)
endif()