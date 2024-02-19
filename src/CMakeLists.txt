include_directories(../lib)
add_executable(wiimote_file_manager main.c)
target_link_libraries(wiimote_file_manager wiiuse)

if(INSTALL_MANAGER)
    install(TARGETS wiimote_file_manager
        RUNTIME DESTINATION bin COMPONENT manager)
endif()