file(REMOVE_RECURSE ${HOME})
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env OPENUT_GAME_DATA=${DATA} XDG_DATA_HOME=${HOME} SDL_VIDEODRIVER=offscreen
        ${GAME} ${MAP} --frames 600 --at "30:exec addbots 4" --at "60:press LeftMouse" --at "64:release LeftMouse"
    RESULT_VARIABLE code
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
    TIMEOUT 300)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "game exited with ${code}\n${output}")
endif()
if(NOT output MATCHES "${MARKER}")
    message(FATAL_ERROR "missing \"${MARKER}\"\n${output}")
endif()
if(output MATCHES "script warning|unimplemented native|could not be loaded|Critical")
    message(FATAL_ERROR "script problem\n${output}")
endif()
