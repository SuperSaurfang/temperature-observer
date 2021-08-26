file(GLOB SOURCES "private/*.cpp")

set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES )

set(COMPONENT_SRCS  "main.cpp"
                    ${SOURCES})
set(COMPONENT_ADD_INCLUDEDIRS "public")

register_component()

